/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "micropython.h"
#include "py/objlist.h"
#include "py/runtime.h"
#include "py/mphal.h"
#include "shared/netutils/netutils.h"
#include "extmod/modnetwork.h"
#include "modtime_impl.h"

#include <sddf/timer/client.h>
#include <sddf/network/queue.h>
#include <sddf/network/config.h>
#include <sddf/network/util.h>
#include <sddf/util/cache.h>
#include <sddf/network/constants.h>

#include <lwip/dhcp.h>
#include <lwip/init.h>
#include <lwip/ip.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/snmp.h>
#include <lwip/stats.h>
#include <lwip/sys.h>
#include <lwip/tcp.h>
#include <lwip/timeouts.h>
#include <lwip/err.h>
#include <netif/etharp.h>

#include <lions/firewall/arp_queue.h>
#include <lions/firewall/config.h>
#include <lions/firewall/queue.h>

#define LINK_SPEED 1000000000 // Gigabit
#define ETHER_MTU 1500

arp_packet_t arp_response_pkt = {0};
extern net_client_config_t net_config;

__attribute__((__section__(".firewall_webserver_config"))) firewall_webserver_config_t firewall_config;

#define dlogp(pred, fmt, ...) do { \
    if (pred) { \
        printf("%s: %s:%d:%s: " fmt "\n", microkit_name, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
    } \
} while (0);

#define dlog(fmt, ...) do { \
    printf("%s: %s:%d:%s: " fmt "\n", microkit_name, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
} while (0);

typedef struct pbuf_custom_offset
{
    struct pbuf_custom custom;
    size_t offset;
    bool arp_response;
} pbuf_custom_offset_t;

typedef struct state
{
    struct netif netif;
    /* mac address for this client */
    uint8_t mac[6];

    /* Transmit packets out the transmit virtualiser */
    net_queue_handle_t tx_queue;

    /* Receive packets from the routing component */
    firewall_queue_handle_t rx_active;

    /* Return free buffers to the rx network virtualiser */
    firewall_queue_handle_t rx_free;

    /* ARP request/response queue for ARP queries */
    arp_queue_handle_t *arp_queue;
} state_t;

state_t state;

// DO NOT MERGE: need to figure out how to do this
LWIP_MEMPOOL_DECLARE(
    RX_POOL,
    512 * 2,
    sizeof(pbuf_custom_offset_t),
    "Zero-copy RX pool");

static bool notify_tx = false;
static bool notify_rx = false;
static bool notify_arp = false;

#define ROUND_DOWN(n, b) (((n) >> (b)) << (b))
#define LINE_START(a) ROUND_DOWN(a, CONFIG_L1_CACHE_LINE_SIZE_BITS)
#define LINE_INDEX(a) (LINE_START(a)>>CONFIG_L1_CACHE_LINE_SIZE_BITS)

u32_t sys_now(void) {
    /* LWIP expects the current time in milliseconds */
    return mp_obj_get_int(mp_time_time_get());
}

void fill_arp(uint32_t ip, uint8_t mac[ETH_HWADDR_LEN])
{
    memcpy(&arp_response_pkt.ethdst_addr, &firewall_config.mac_addr, ETH_HWADDR_LEN);
    memcpy(&arp_response_pkt.ethsrc_addr, &mac, ETH_HWADDR_LEN);
    arp_response_pkt.type = HTONS(ETH_TYPE_ARP);
    arp_response_pkt.hwtype = HTONS(ETH_HWADDR_LEN);
    arp_response_pkt.proto = HTONS(ETH_TYPE_IP);
    arp_response_pkt.hwlen = ETH_HWADDR_LEN;
    arp_response_pkt.protolen = IPV4_PROTO_LEN;
    arp_response_pkt.opcode = HTONS(ETHARP_OPCODE_REPLY);
    memcpy(&arp_response_pkt.hwsrc_addr, &mac, ETH_HWADDR_LEN);
    arp_response_pkt.ipsrc_addr = ip;
    memcpy(&arp_response_pkt.hwdst_addr, &firewall_config.mac_addr, ETH_HWADDR_LEN);
    arp_response_pkt.ipdst_addr = firewall_config.ip;
    memset(&arp_response_pkt.padding, 0, 10);
}

static void interface_free_buffer(struct pbuf *buf) {
    SYS_ARCH_DECL_PROTECT(old_level);
    pbuf_custom_offset_t *custom_pbuf_offset = (pbuf_custom_offset_t *)buf;
    SYS_ARCH_PROTECT(old_level);
    if (!custom_pbuf_offset->arp_response) {
        firewall_buff_desc_t buffer = { custom_pbuf_offset->offset, 0 };
        firewall_enqueue(&state.rx_free, buffer);
        notify_rx = true;
    }
    LWIP_MEMPOOL_FREE(RX_POOL, custom_pbuf_offset);
    SYS_ARCH_UNPROTECT(old_level);
}

static err_t netif_output(struct netif *netif, struct pbuf *p) {

    /* Check is this is an arp request before copying into a DMA buffer */
    struct ethernet_header *eth_hdr = (struct ethernet_header *)p->payload;
    if (eth_hdr->type == HTONS(ETH_TYPE_ARP)) {
        arphdr_t *arp_hdr = (arphdr_t *)p->next->payload;
        if (arp_hdr->opcode == HTONS(ETHARP_OPCODE_REQUEST)) {

                /* This is an arp request, don't transmit through the network */
                arp_request_t request = {arp_hdr->ipdst_addr, {0}, false};
                int err = arp_enqueue_request(state.arp_queue, request);
                if (err) {
                    dlog("Could not enqueue arp request, queue is full");
                    return ERR_MEM;
                }
                notify_arp = true;

                /* Successfully emulated sending out an ARP request */
                return ERR_OK;

        }
    }

    /* Grab an available TX buffer, copy pbuf data over,
    add to active tx queue, notify server */
    err_t ret = ERR_OK;

    if (p->tot_len > NET_BUFFER_SIZE) {
        return ERR_MEM;
    }
    net_buff_desc_t buffer;
    int err = net_dequeue_free(&state.tx_queue, &buffer);
    if (err) {
        return ERR_MEM;
    }
    unsigned char *frame = (unsigned char *)(buffer.io_or_offset + net_config.tx_data.vaddr);
    unsigned int copied = 0;
    for (struct pbuf *curr = p; curr != NULL; curr = curr->next) {
        memcpy(frame + copied, curr->payload, curr->len);
        copied += curr->len;
    }

    /* insert into the active tx queue */
    buffer.len = copied;
    err = net_enqueue_active(&state.tx_queue, buffer);
    assert(!err);
    notify_tx = true;

    // @alwin: Maybe instead of notify_tx = true, an explicit notify is more appropriate
    // microkit_notify(net_config.tx.id);

    return ret;
}

static void netif_status_callback(struct netif *netif) {
    dlog("Netif is up now! IP address for netif %s is: %s", netif->name, ip4addr_ntoa(netif_ip4_addr(netif)));
}

static err_t ethernet_init(struct netif *netif)
{
    if (netif->state == NULL)
    {
        return ERR_ARG;
    }

    state_t *data = netif->state;

    sddf_memcpy(netif->hwaddr, net_config.mac_addr, 6);

    netif->mtu = ETHER_MTU;
    netif->hwaddr_len = ETHARP_HWADDR_LEN;
    netif->output = etharp_output;
    netif->linkoutput = netif_output;
    NETIF_INIT_SNMP(netif, snmp_ifType_ethernet_csmacd, LINK_SPEED);
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_IGMP;

    return ERR_OK;
}

void init_networking(void) {
    /* Set up shared memory regions */
    firewall_queue_init(&state.rx_active, firewall_config.rx_active.queue.vaddr, firewall_config.rx_active.capacity);
    firewall_queue_init(&state.rx_free, firewall_config.rx_free.queue.vaddr, firewall_config.rx_free.capacity);
    net_queue_init(&state.tx_queue, net_config.tx.free_queue.vaddr, net_config.tx.active_queue.vaddr, net_config.tx.num_buffers);
    net_buffers_init(&state.tx_queue, 0);
    state.arp_queue = (arp_queue_handle_t *)firewall_config.arp_queue.queue.vaddr;
    arp_handle_init(state.arp_queue, firewall_config.arp_queue.capacity);
    lwip_init();
    LWIP_MEMPOOL_INIT(RX_POOL);

    sddf_memcpy(state.mac, net_config.mac_addr, 6);

    /* Set some dummy IP configuration values to get lwIP bootstrapped  */
    struct ip4_addr netmask, ipaddr, gw, multicast, macbook;
    ipaddr_aton("0.0.0.0", &gw);
    ipaddr_aton("192.168.33.0", &ipaddr);
    ipaddr_aton("0.0.0.0", &multicast);
    ipaddr_aton("255.255.255.0", &netmask);

    state.netif.name[0] = 'e';
    state.netif.name[1] = '0';

    if (!netif_add(&(state.netif), &ipaddr, &netmask, &gw, &state,
                   ethernet_init, ethernet_input)) {
        dlog("Netif add returned NULL");
    }
    netif_set_default(&(state.netif));
    netif_set_status_callback(&(state.netif), netif_status_callback);
    netif_set_up(&(state.netif));

    if (notify_rx) {
        notify_rx = false;
        if (!microkit_have_signal) {
            microkit_deferred_notify(firewall_config.rx_free.ch);
        } else if (microkit_signal_cap != BASE_OUTPUT_NOTIFICATION_CAP + firewall_config.rx_free.ch) {
            microkit_notify(firewall_config.rx_free.ch);
        }
    }

    if (notify_tx && net_require_signal_active(&state.tx_queue)) {
        net_cancel_signal_active(&state.tx_queue);
        notify_tx = false;
        if (!microkit_have_signal) {
            microkit_deferred_notify(net_config.tx.id);
        } else if (microkit_signal_cap != BASE_OUTPUT_NOTIFICATION_CAP + net_config.tx.id) {
            microkit_notify(net_config.tx.id);
        }
    }

    if (notify_arp) {
        notify_arp = false;
        if (!microkit_have_signal) {
            microkit_deferred_notify(firewall_config.arp_queue.ch);
        } else if (microkit_signal_cap != BASE_OUTPUT_NOTIFICATION_CAP + firewall_config.arp_queue.ch) {
            microkit_notify(firewall_config.arp_queue.ch);
        }
    }
}

// TODO: Add this to notified, as well as modifying channels inside notified
void mpnet_process_arp(void) {
    while (!arp_queue_empty_response(state.arp_queue)) {
        arp_request_t response;
        int err = arp_dequeue_response(state.arp_queue, &response);
        assert(!err);

        // TODO: Check validity of response
        if (response.state == ARP_STATE_REACHABLE) {
            // Contruct the ARP response packet
            fill_arp(response.ip, response.mac_addr);
            if (FIREWALL_DEBUG_OUTPUT) {
                // TODO: Add more logging output
                dlog("Dequeuing ARP response for ip %u -> obtained MAC[0] = %x, MAC[5] = %x\n", response.ip, response.mac_addr[0], response.mac_addr[5]);
            }

            /* Input packet into lwip stack */
            pbuf_custom_offset_t *custom_pbuf_offset = (pbuf_custom_offset_t *)LWIP_MEMPOOL_ALLOC(RX_POOL);
            custom_pbuf_offset->offset = 0;
            custom_pbuf_offset->arp_response = true;
            custom_pbuf_offset->custom.custom_free_function = interface_free_buffer;

            struct pbuf *p = pbuf_alloced_custom(
                PBUF_RAW,
                sizeof(arp_packet_t),
                PBUF_REF,
                &custom_pbuf_offset->custom,
                &arp_response_pkt,
                sizeof(arp_packet_t)
            );

            if (state.netif.input(p, &state.netif) != ERR_OK) {
                // If it is successfully received, the receiver controls whether or not it gets freed.
                dlog("netif.input() != ERR_OK");
                pbuf_free(p);
            }
        }
    }
}

void mpnet_process_rx(void) {
    while (!firewall_queue_empty(&state.rx_active)) {
        firewall_buff_desc_t buffer;
        firewall_dequeue(&state.rx_active, &buffer);

        pbuf_custom_offset_t *custom_pbuf_offset = (pbuf_custom_offset_t *)LWIP_MEMPOOL_ALLOC(RX_POOL);
        custom_pbuf_offset->offset = buffer.io_or_offset;
        custom_pbuf_offset->arp_response = false;
        custom_pbuf_offset->custom.custom_free_function = interface_free_buffer;

        struct pbuf *p = pbuf_alloced_custom(
            PBUF_RAW,
            buffer.len,
            PBUF_REF,
            &custom_pbuf_offset->custom,
            (void *)(buffer.io_or_offset + net_config.rx_data.vaddr),
            NET_BUFFER_SIZE
        );

        if (state.netif.input(p, &state.netif) != ERR_OK) {
            // If it is successfully received, the receiver controls whether or not it gets freed.
            dlog("netif.input() != ERR_OK");
            pbuf_free(p);
        }
    }
}

void pyb_lwip_poll(void) {
    // Run the lwIP internal updates
    sys_check_timeouts();
}

void mpnet_handle_notify(void) {
    if (notify_rx) {
        notify_rx = false;
        if (!microkit_have_signal) {
            microkit_deferred_notify(firewall_config.rx_free.ch);
        } else if (microkit_signal_cap != BASE_OUTPUT_NOTIFICATION_CAP + firewall_config.rx_free.ch) {
            microkit_notify(firewall_config.rx_free.ch);
        }
    }

    if (notify_tx && net_require_signal_active(&state.tx_queue)) {
        net_cancel_signal_active(&state.tx_queue);
        notify_tx = false;
        if (!microkit_have_signal) {
            microkit_deferred_notify(net_config.tx.id);
        } else if (microkit_signal_cap != BASE_OUTPUT_NOTIFICATION_CAP + net_config.tx.id) {
            microkit_notify(net_config.tx.id);
        }
    }

    if (notify_arp) {
        notify_arp = false;
        if (!microkit_have_signal) {
            microkit_deferred_notify(firewall_config.arp_queue.ch);
        } else if (microkit_signal_cap != BASE_OUTPUT_NOTIFICATION_CAP + firewall_config.arp_queue.ch) {
            microkit_notify(firewall_config.arp_queue.ch);
        }
    }
}
