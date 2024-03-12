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

#include <sddf/timer/client.h>
#include <sddf/network/shared_ringbuffer.h>
#include <sddf/util/cache.h>

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
#define NUM_BUFFERS 512
#define BUF_SIZE 2048

#define dlogp(pred, fmt, ...) do { \
    if (pred) { \
        printf("%s: %s:%d:%s: " fmt "\n", microkit_name, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
    } \
} while (0);

#define dlog(fmt, ...) do { \
    printf("%s: %s:%d:%s: " fmt "\n", microkit_name, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
} while (0);

typedef struct lwip_custom_pbuf
{
    struct pbuf_custom custom;
    uintptr_t buffer;
} lwip_custom_pbuf_t;

typedef struct state
{
    struct netif netif;
    /* mac address for this client */
    uint8_t mac[6];

    /* Pointers to shared buffers */
    ring_handle_t rx_ring;
    ring_handle_t tx_ring;
} state_t;

state_t state;

LWIP_MEMPOOL_DECLARE(
    RX_POOL,
    NUM_BUFFERS * 2,
    sizeof(lwip_custom_pbuf_t),
    "Zero-copy RX pool");

uintptr_t rx_free;
uintptr_t rx_used;
uintptr_t tx_free;
uintptr_t tx_used;
uintptr_t shared_dma_vaddr_rx;
uintptr_t shared_dma_vaddr_tx;

static bool notify_tx = false;
static bool notify_rx = false;

#define ROUND_DOWN(n, b) (((n) >> (b)) << (b))
#define LINE_START(a) ROUND_DOWN(a, CONFIG_L1_CACHE_LINE_SIZE_BITS)
#define LINE_INDEX(a) (LINE_START(a)>>CONFIG_L1_CACHE_LINE_SIZE_BITS)

u32_t sys_now(void) {
    return mp_hal_ticks_ms();
}

static void interface_free_buffer(struct pbuf *buf) {
    SYS_ARCH_DECL_PROTECT(old_level);
    lwip_custom_pbuf_t *custom_pbuf = (lwip_custom_pbuf_t *)buf;
    SYS_ARCH_PROTECT(old_level);
    enqueue_free(&state.rx_ring, custom_pbuf->buffer, BUF_SIZE, NULL);
    notify_rx = true;
    LWIP_MEMPOOL_FREE(RX_POOL, custom_pbuf);
    SYS_ARCH_UNPROTECT(old_level);
}

static err_t netif_output(struct netif *netif, struct pbuf *p) {
    /* Grab an available TX buffer, copy pbuf data over,
    add to used tx ring, notify server */
    err_t ret = ERR_OK;

    if (p->tot_len > BUF_SIZE) {
        return ERR_MEM;
    }
    uintptr_t addr;
    unsigned int len;
    void *cookie;
    int err = dequeue_free(&state.tx_ring, &addr, &len, &cookie);
    if (err) {
        return ERR_MEM;
    }
    uintptr_t buffer = addr;
    unsigned char *frame = (unsigned char *)buffer;

    /* Copy all buffers that need to be copied */
    unsigned int copied = 0;
    for (struct pbuf *curr = p; curr != NULL; curr = curr->next) {
        unsigned char *buffer_dest = &frame[copied];
        if ((uintptr_t)buffer_dest != (uintptr_t)curr->payload) {
            /* Don't copy memory back into the same location */
            memcpy(buffer_dest, curr->payload, curr->len);
        }
        copied += curr->len;
    }

    cache_clean((unsigned long) frame, (unsigned long) frame + copied);

    /* insert into the used tx queue */
    err = enqueue_used(&state.tx_ring, (uintptr_t)frame, copied, NULL);
    if (err) {
        dlog("TX used ring full");
        enqueue_free(&(state.tx_ring), (uintptr_t)frame, BUF_SIZE, NULL);
        return ERR_MEM;
    }

    notify_tx = true;

    return ret;
}

static void netif_status_callback(struct netif *netif) {
    if (dhcp_supplied_address(netif)) {
        dlog("DHCP request finished, IP address for netif %s is: %s", netif->name, ip4addr_ntoa(netif_ip4_addr(netif)));

        microkit_mr_set(0, ip4_addr_get_u32(netif_ip4_addr(netif)));
        microkit_mr_set(1, (state.mac[0] << 24) | (state.mac[1] << 16) | (state.mac[2] << 8) | (state.mac[3]));
        microkit_mr_set(2, (state.mac[4] << 24) | (state.mac[5] << 16));
        microkit_ppcall(ETH_ARP_CH, microkit_msginfo_new(0, 3));
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
    ring_init(&state.rx_ring, (ring_buffer_t *)rx_free, (ring_buffer_t *)rx_used, 1, NUM_BUFFERS, NUM_BUFFERS);
    ring_init(&state.tx_ring, (ring_buffer_t *)tx_free, (ring_buffer_t *)tx_used, 0, NUM_BUFFERS, NUM_BUFFERS);

    for (int i = 0; i < NUM_BUFFERS - 1; i++) {
        uintptr_t addr = shared_dma_vaddr_rx + (BUF_SIZE * i);
        enqueue_free(&state.rx_ring, addr, BUF_SIZE, NULL);
    }

    state.mac[0] = 0x52;
    state.mac[1] = 0x54;
    state.mac[2] = 0x1;
    state.mac[3] = 0;
    state.mac[4] = 0;
    state.mac[5] = 11;

    lwip_init();
    LWIP_MEMPOOL_INIT(RX_POOL);

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

    state.rx_ring.free_ring->notify_reader = true;
    state.rx_ring.used_ring->notify_reader = true;
    state.tx_ring.free_ring->notify_reader = true;
    state.tx_ring.used_ring->notify_reader = true;

    if (notify_rx && state.rx_ring.free_ring->notify_reader) {
        notify_rx = false;
        if (!have_signal) {
            microkit_notify_delayed(ETH_RX_CH);
        } else if (signal_cap != BASE_OUTPUT_NOTIFICATION_CAP + ETH_RX_CH) {
            microkit_notify(ETH_RX_CH);
        }
    }

    if (notify_tx && state.tx_ring.used_ring->notify_reader) {
        notify_tx = false;
        if (!have_signal) {
            microkit_notify_delayed(ETH_TX_CH);
        } else if (signal_cap != BASE_OUTPUT_NOTIFICATION_CAP + ETH_TX_CH) {
            microkit_notify(ETH_TX_CH);
        }
    }
}

void process_rx(void) {
    while (!ring_empty(state.rx_ring.used_ring)) {
        uintptr_t addr;
        unsigned int len;
        void *cookie;

        dequeue_used(&state.rx_ring, &addr, &len, &cookie);

        lwip_custom_pbuf_t *custom_pbuf = (lwip_custom_pbuf_t *)LWIP_MEMPOOL_ALLOC(RX_POOL);
        custom_pbuf->buffer = addr;
        custom_pbuf->custom.custom_free_function = interface_free_buffer;

        struct pbuf *p = pbuf_alloced_custom(
            PBUF_RAW,
            len,
            PBUF_REF,
            &custom_pbuf->custom,
            (void *)addr,
            BUF_SIZE
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
