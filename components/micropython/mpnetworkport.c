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

#define LINK_SPEED 1000000000 // Gigabit
#define ETHER_MTU 1500

extern net_client_config_t net_config;

int mp_mod_network_prefer_dns_use_ip_version = 4;

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
    struct netif netif;
    /* mac address for this client */
    uint8_t mac[6];

    /* Pointers to shared buffers */
    net_queue_handle_t rx_queue;
    net_queue_handle_t tx_queue;
} state_t;

state_t state;

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
    if (dhcp_supplied_address(netif)) {
        dlog("DHCP request finished, IP address for netif %s is: %s", netif->name, ip4addr_ntoa(netif_ip4_addr(netif)));
    }
}

static err_t ethernet_init(struct netif *netif)
{
    if (netif->state == NULL)
    {
        return ERR_ARG;
    }

    state_t *data = netif->state;

    netif->hwaddr[0] = data->mac[0];
    netif->hwaddr[1] = data->mac[1];
    netif->hwaddr[2] = data->mac[2];
    netif->hwaddr[3] = data->mac[3];
    netif->hwaddr[4] = data->mac[4];
    netif->hwaddr[5] = data->mac[5];
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
    net_queue_init(&state.rx_queue, net_config.rx.free_queue.vaddr, net_config.rx.active_queue.vaddr, net_config.rx.num_buffers);
    net_queue_init(&state.tx_queue, net_config.tx.free_queue.vaddr, net_config.tx.active_queue.vaddr, net_config.tx.num_buffers);
    net_buffers_init(&state.tx_queue, 0);

    lwip_init();
    LWIP_MEMPOOL_INIT(RX_POOL);

    sddf_memcpy(state.mac, net_config.mac_addr, 6);

    /* Set some dummy IP configuration values to get lwIP bootstrapped  */
    struct ip4_addr netmask, ipaddr, gw, multicast;
    ipaddr_aton("0.0.0.0", &gw);
    ipaddr_aton("0.0.0.0", &ipaddr);
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

    int err = dhcp_start(&(state.netif));
    dlogp(err, "failed to start DHCP negotiation");

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
            net_buff_desc_t buffer;
            net_dequeue_active(&state.rx_queue, &buffer);

            pbuf_custom_offset_t *custom_pbuf_offset = (pbuf_custom_offset_t *)LWIP_MEMPOOL_ALLOC(RX_POOL);
            custom_pbuf_offset->offset = buffer.io_or_offset;
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
