/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>

#include <stdio.h>
#include <stdint.h>
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

#include <lions/firewall/protocols.h>

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
#include <netif/ppp/ppp.h>
#include <netif/ppp/pppos.h>
#include <netif/ppp/pppapi.h>
#include <netif/ppp/ppp_impl.h>

#define LINK_SPEED 1000000000 // Gigabit
#define ETHER_MTU 1500

extern net_client_config_t net_config;

uint8_t mac_addr[6] = {0x00, 0x01, 0xc0, 0x39, 0xd5, 0x15};

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
} pbuf_custom_offset_t;

typedef struct state
{
    struct ppp_pcb_s *pcb_client;
    struct ppp_pcb_s *pcb_server;
    struct netif netif;
    struct netif client_netif;
    /* mac address for this client */
    uint8_t mac[6];

    /* Pointers to shared buffers */
    net_queue_handle_t rx_queue;
    net_queue_handle_t tx_queue;
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

#define ROUND_DOWN(n, b) (((n) >> (b)) << (b))
#define LINE_START(a) ROUND_DOWN(a, CONFIG_L1_CACHE_LINE_SIZE_BITS)
#define LINE_INDEX(a) (LINE_START(a)>>CONFIG_L1_CACHE_LINE_SIZE_BITS)

u32_t sys_now(void) {
    /* LWIP expects the current time in milliseconds */
    return mp_obj_get_int(mp_time_time_get());
}

u32_t sys_jiffies(void) {
    return sys_now();
}

static void interface_free_buffer(struct pbuf *buf) {
    SYS_ARCH_DECL_PROTECT(old_level);
    pbuf_custom_offset_t *custom_pbuf_offset = (pbuf_custom_offset_t *)buf;
    SYS_ARCH_PROTECT(old_level);
    net_buff_desc_t buffer = { custom_pbuf_offset->offset, 0 };
    net_enqueue_free(&state.rx_queue, buffer);
    notify_rx = true;
    LWIP_MEMPOOL_FREE(RX_POOL, custom_pbuf_offset);
    SYS_ARCH_UNPROTECT(old_level);
}

static err_t netif_output(struct netif *netif, struct pbuf *p) {
    printf("Outputting data from micropython");
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

static err_t ppp_output(ppp_pcb *pcb, u8_t *data, u32_t len, void *ctx)
{
    sddf_dprintf("In output server\n");
    sddf_dprintf("---------Buffer-------\n");
    struct ppp_header *p_head = (struct ppp_header *) data;
    sddf_dprintf("Flag: 0x%02x\n", p_head->flag);
    sddf_dprintf("Addr: 0x%02x\n", p_head->address);
    sddf_dprintf("Control: 0x%02x\n", p_head->control);
    sddf_dprintf("Protocol: 0x%04x\n", p_head->protocol);
    sddf_dprintf("----------------------\n");
    if (p_head->protocol == 0xc023) {
        sddf_dprintf("We are in the link-establishment phase!\n");
        // pppos_input(state.pcb_server, data, len);
    } else if(p_head->protocol == 0x0021) {
        sddf_dprintf("A normal packet to output\n");
    }

}

static err_t ppp_output_client(ppp_pcb *pcb, u8_t *data, u32_t len, void *ctx)
{
    sddf_dprintf("In oputput client\n");
    sddf_dprintf("---------Buffer-------\n");
    struct ppp_header *p_head = (struct ppp_header *) data;
    sddf_dprintf("Flag: 0x%02x\n", p_head->flag);
    sddf_dprintf("Addr: 0x%02x\n", p_head->address);
    sddf_dprintf("Control: 0x%02x\n", p_head->control);
    sddf_dprintf("Protocol: 0x%04x\n", p_head->protocol);
    sddf_dprintf("----------------------\n");
    if (p_head->protocol == 0xc023) {
        sddf_dprintf("We are in the link-establishment phase!\n");
        pppos_input(state.pcb_client, data, len);
    }

}


static void netif_status_callback(struct netif *netif) {
    dlog("Netif is up now! IP address for netif %s is: %s", netif->name, ip4addr_ntoa(netif_ip4_addr(netif)));
        //     dlog("DHCP request finished, IP address for netif %s is: %s", netif->name, ip4addr_ntoa(netif_ip4_addr(netif)));

    // if (dhcp_supplied_address(netif)) {
    //     dlog("DHCP request finished, IP address for netif %s is: %s", netif->name, ip4addr_ntoa(netif_ip4_addr(netif)));
    // }
}


// err_t packet_route(struct netif *netif, struct pbuf *q, const ip4_addr_t *ipaddr)
// {
//     etharp_query(netif, ipaddr, q);
// }

void dumpy_cb()
{
    sddf_dprintf("We are in the dumpy cb from PPP\n");
}

static err_t ethernet_init(struct netif *netif)
{
    if (netif->state == NULL)
    {
        return ERR_ARG;
    }

    state_t *data = netif->state;
    sddf_memcpy(netif->hwaddr, mac_addr, 6);

    // netif->hwaddr[0] = data->mac[0];
    // netif->hwaddr[1] = data->mac[1];
    // netif->hwaddr[2] = data->mac[2];
    // netif->hwaddr[3] = data->mac[3];
    // netif->hwaddr[4] = data->mac[4];
    // netif->hwaddr[5] = data->mac[5];
    netif->mtu = ETHER_MTU;
    netif->hwaddr_len = 6;
    netif->output = netif_output;
    netif->linkoutput = netif_output;
    NETIF_INIT_SNMP(netif, snmp_ifType_ethernet_csmacd, LINK_SPEED);
    netif->input = dumpy_cb;
    netif->flags = NETIF_FLAG_LINK_UP | NETIF_FLAG_IGMP;

    return ERR_OK;
}

void init_networking(void) {
    /* Set up shared memory regions */
    net_queue_init(&state.rx_queue, net_config.rx.free_queue.vaddr, net_config.rx.active_queue.vaddr, net_config.rx.num_buffers);
    net_queue_init(&state.tx_queue, net_config.tx.free_queue.vaddr, net_config.tx.active_queue.vaddr, net_config.tx.num_buffers);
    net_buffers_init(&state.tx_queue, 0);

    lwip_init();
    LWIP_MEMPOOL_INIT(RX_POOL);

    sddf_memcpy(state.mac, mac_addr, 6);

    /* Set some dummy IP configuration values to get lwIP bootstrapped  */
    struct ip4_addr netmask, ipaddr, gw, multicast, macbook;
    ipaddr_aton("0.0.0.0", &gw);
    // @kwinter: Unsure of what to set this IP address to?
    ipaddr_aton("192.168.33.0", &ipaddr);
    ipaddr_aton("0.0.0.0", &multicast);
    ipaddr_aton("255.255.255.0", &netmask);
    ipaddr_aton("192.168.33.6", &macbook);
    struct eth_addr mac_addr = ETH_ADDR(0x10, 0x82, 0x86, 0x2b, 0x5a, 0xf4);

    // state.netif.name[0] = 'e';
    // state.netif.name[1] = '0';

    // if (!netif_add(&(state.netif), &ipaddr, &netmask, &gw, &state,
    //             pppoe_init, pppoe_data_input)) {
    //     dlog("Netif add returned NULL");
    // }
    // netif_set_default(&(state.netif));
    // netif_set_status_callback(&(state.netif), netif_status_callback);
    // netif_set_up(&(state.netif));
    // int err = dhcp_start(&(state.netif));
    // dlogp(err, "failed to start DHCP negotiation");

    state.pcb_server = pppos_create(&(state.client_netif), ppp_output_client, netif_status_callback, dumpy_cb);
    assert(state.pcb_server != NULL);

    ppp_set_silent(state.pcb_server, 1);
    int err = ppp_listen(state.pcb_server);
    state.pcb_server = PPP_PHASE_RUNNING;
    assert(!err);
    state.pcb_server->lcp_fsm.state = PPP_FSM_OPENED;

    state.pcb_client = pppos_create(&(state.netif), ppp_output, netif_status_callback, dumpy_cb);
    assert(state.pcb_client != NULL);

    ppp_set_passive(state.pcb_client, 1);
    err = ppp_connect(state.pcb_client, 0);
    state.pcb_client->phase = PPP_PHASE_RUNNING;
    ppp_start(state.pcb_client);
    state.pcb_client->lcp_fsm.state = PPP_FSM_OPENED;
    // assert(!err);
    netif_set_default(&(state.netif));

    if (notify_rx && net_require_signal_free(&state.rx_queue)) {
        net_cancel_signal_free(&state.rx_queue);
        notify_rx = false;
        if (!microkit_have_signal) {
            microkit_deferred_notify(net_config.rx.id);
        } else if (microkit_signal_cap != BASE_OUTPUT_NOTIFICATION_CAP + net_config.rx.id) {
            microkit_notify(net_config.rx.id);
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
}

void mpnet_process_rx(void) {
    bool reprocess = true;
    while (reprocess) {
        while (!net_queue_empty_active(&state.rx_queue)) {
            sddf_dprintf("Looooping through queue in mpnetwork\n");
            net_buff_desc_t buffer;
            net_dequeue_active(&state.rx_queue, &buffer);

            struct ppp_header *temp = (struct ppp_header *)((buffer.io_or_offset + net_config.rx_data.vaddr) + ((sizeof(struct ethernet_header) - sizeof(struct ppp_header))));

            temp->flag = 0x7E;
            temp->address = 0xFF;
            temp->control = 0x03;
            temp->protocol = 0x21;

            // pppos_input_tcpip(state.pcb_client, (uint8_t *)temp, buffer.len - ((sizeof(struct ethernet_header) - sizeof(struct ppp_header))));
            pbuf_custom_offset_t *custom_pbuf_offset = (pbuf_custom_offset_t *)LWIP_MEMPOOL_ALLOC(RX_POOL);
            custom_pbuf_offset->offset = (buffer.io_or_offset + net_config.rx_data.vaddr) + ((sizeof(struct ethernet_header) - sizeof(struct ppp_header)));
            custom_pbuf_offset->custom.custom_free_function = interface_free_buffer;

            struct pbuf *p = pbuf_alloced_custom(
                PBUF_RAW,
                buffer.len - ((sizeof(struct ethernet_header) - sizeof(struct ppp_header))),
                PBUF_REF,
                &custom_pbuf_offset->custom,
                (void *)(temp),
                NET_BUFFER_SIZE
            );

            ppp_input(state.pcb_client, p);
            sddf_dprintf("Finished inputting packet into the pcb server!\n");

            // if (state.netif.input(p, &state.netif) != ERR_OK) {
            //     // If it is successfully received, the receiver controls whether or not it gets freed.
            //     dlog("netif.input() != ERR_OK");
            //     pbuf_free(p);
            // }
        }

        net_request_signal_active(&state.rx_queue);
        reprocess = false;

        if (!net_queue_empty_active(&state.rx_queue)) {
            net_cancel_signal_active(&state.rx_queue);
            reprocess = true;
        }
    }
}

void pyb_lwip_poll(void) {
    // Run the lwIP internal updates
    sys_check_timeouts();
}

void mpnet_handle_notify(void) {
    if (notify_rx && net_require_signal_free(&state.rx_queue)) {
        net_cancel_signal_free(&state.rx_queue);
        notify_rx = false;
        if (!microkit_have_signal) {
            microkit_deferred_notify(net_config.rx.id);
        } else if (microkit_signal_cap != BASE_OUTPUT_NOTIFICATION_CAP + net_config.rx.id) {
            microkit_notify(net_config.rx.id);
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
}
