/*
 * Copyright 2023, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>

#include <string.h>

#include <sddf/timer/client.h>
#include <sddf/network/shared_ringbuffer.h>

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
#include <netif/etharp.h>

#include "util.h"
#include "tcp.h"

#define LINK_SPEED 1000000000 // Gigabit
#define ETHER_MTU 1500
#define NUM_BUFFERS 512
#define BUF_SIZE 2048

typedef struct state
{
    struct netif netif;
    /* mac address for this client */
    uint8_t mac[6];

    /* Pointers to shared buffers */
    ring_handle_t rx_ring;
    ring_handle_t tx_ring;
} state_t;

typedef struct lwip_custom_pbuf
{
    struct pbuf_custom custom;
    uintptr_t buffer;
} lwip_custom_pbuf_t;

typedef struct {
    struct tcp_pcb *sock_tpcb;
    int port;
    int connected;
    int used;

    char rx_buf[SOCKET_BUF_SIZE];
    int rx_head;
    int rx_len;
} socket_t;

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

// Should only need 1 at any one time, accounts for any reconnecting that might happen
socket_t sockets[MAX_SOCKETS] = {0};

static bool network_ready;
static bool notify_tx = false;
static bool notify_rx = false;

int tcp_ready(void) {
    return network_ready;
}

void tcp_maybe_notify(void) {
    if (notify_rx && state.rx_ring.free_ring->notify_reader) {
        state.rx_ring.free_ring->notify_reader = false;
        notify_rx = false;
        if (!have_signal) {
            microkit_notify_delayed(ETHERNET_RX_CHANNEL);
        } else if (signal_cap != BASE_OUTPUT_NOTIFICATION_CAP + ETHERNET_RX_CHANNEL) {
            microkit_notify(ETHERNET_RX_CHANNEL);
        }
    }

    if (notify_tx && state.tx_ring.used_ring->notify_reader) {
        state.tx_ring.used_ring->notify_reader = false;
        notify_tx = false;
        if (!have_signal) {
            microkit_notify_delayed(ETHERNET_TX_CHANNEL);
        } else if (signal_cap != BASE_OUTPUT_NOTIFICATION_CAP + ETHERNET_TX_CHANNEL) {
            microkit_notify(ETHERNET_TX_CHANNEL);
        }
    }
}

#define ROUND_DOWN(n, b) (((n) >> (b)) << (b))
#define LINE_START(a) ROUND_DOWN(a, CONFIG_L1_CACHE_LINE_SIZE_BITS)
#define LINE_INDEX(a) (LINE_START(a)>>CONFIG_L1_CACHE_LINE_SIZE_BITS)

static inline void
dsb(void)
{
    asm volatile("dsb sy" ::: "memory");
}

static inline void 
dmb(void)
{
    asm volatile("dmb sy" ::: "memory");
}

static inline void
cleanInvalByVA(unsigned long vaddr)
{
    asm volatile("dc civac, %0" : : "r"(vaddr));
    dsb();
}

static inline void
cleanByVA(unsigned long vaddr)
{
    asm volatile("dc cvac, %0" : : "r"(vaddr));
    dmb();
}

void
cleanInvalidateCache(unsigned long start, unsigned long end)
{
    unsigned long line;
    unsigned long index;
    /* Clean the L1 range */

    /* Finally clean and invalidate the L1 range. The extra clean is only strictly neccessary
     * in a multiprocessor environment to prevent a write being lost if another core is
     * attempting a store at the same time. As the range should already be clean asking
     * it to clean again should not affect performance */
    for (index = LINE_INDEX(start); index < LINE_INDEX(end) + 1; index++) {
        line = index << CONFIG_L1_CACHE_LINE_SIZE_BITS;
        cleanInvalByVA(line);
    }
}

void
cleanCache(unsigned long start, unsigned long end)
{
    unsigned long line;
    unsigned long index;

    for (index = LINE_INDEX(start); index < LINE_INDEX(end) + 1; index++) {
        line = index << CONFIG_L1_CACHE_LINE_SIZE_BITS;
        cleanByVA(line);
    }
}


uint32_t sys_now(void) {
    return sddf_timer_time_now() * US_IN_MS;
}

static void get_mac(void) {
    state.mac[0] = 0x52;
    state.mac[1] = 0x54;
    state.mac[2] = 0x1;
    state.mac[3] = 0;
    state.mac[4] = 0;
    state.mac[5] = 10;
}

static void netif_status_callback(struct netif *netif) {
    if (dhcp_supplied_address(netif)) {
        dlog("DHCP request finished, IP address for netif %s is: %s", netif->name, ip4addr_ntoa(netif_ip4_addr(netif)));

        microkit_mr_set(0, ip4_addr_get_u32(netif_ip4_addr(netif)));
        microkit_mr_set(1, (state.mac[0] << 24) | (state.mac[1] << 16) | (state.mac[2] << 8) | (state.mac[3]));
        microkit_mr_set(2, (state.mac[4] << 24) | (state.mac[5] << 16));
        microkit_ppcall(ETHERNET_ARP_CHANNEL, microkit_msginfo_new(0, 3));

        network_ready = true;
    }
}

static err_t lwip_eth_send(struct netif *netif, struct pbuf *p) {
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

    cleanCache((unsigned long) frame, (unsigned long) frame + copied);

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

/**
 * Free a pbuf. This also returns the underlying buffer to
 * the appropriate place.
 *
 * @param buf pbuf to free.
 *
 */
static void interface_free_buffer(struct pbuf *buf) {
    SYS_ARCH_DECL_PROTECT(old_level);
    lwip_custom_pbuf_t *custom_pbuf = (lwip_custom_pbuf_t *)buf;
    SYS_ARCH_PROTECT(old_level);
    enqueue_free(&state.rx_ring, custom_pbuf->buffer, BUF_SIZE, NULL);
    notify_rx = true;
    LWIP_MEMPOOL_FREE(RX_POOL, custom_pbuf);
    SYS_ARCH_UNPROTECT(old_level);
}

/**
 * Initialise the network interface data structure.
 *
 * @param netif network interface data structuer.
 */
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
    netif->linkoutput = lwip_eth_send;
    NETIF_INIT_SNMP(netif, snmp_ifType_ethernet_csmacd, LINK_SPEED);
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_IGMP;

    return ERR_OK;
}

void tcp_process_rx(void) {
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

void tcp_update(void)
{
    sys_check_timeouts();
}

void tcp_init_0(void)
{
    /* Set up shared memory regions */
    ring_init(&state.rx_ring, rx_free, rx_used, 1, NUM_BUFFERS, NUM_BUFFERS);
    ring_init(&state.tx_ring, tx_free, tx_used, 0, NUM_BUFFERS, NUM_BUFFERS);

    for (int i = 0; i < NUM_BUFFERS - 1; i++) {
        uintptr_t addr = shared_dma_vaddr_rx + (BUF_SIZE * i);
        enqueue_free(&state.rx_ring, addr, BUF_SIZE, NULL);
    }

    get_mac();
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
            microkit_notify_delayed(ETHERNET_RX_CHANNEL);
        } else if (signal_cap != BASE_OUTPUT_NOTIFICATION_CAP + ETHERNET_RX_CHANNEL) {
            microkit_notify(ETHERNET_RX_CHANNEL);
        }
    }

    if (notify_tx && state.tx_ring.used_ring->notify_reader) {
        notify_tx = false;
        if (!have_signal) {
            microkit_notify_delayed(ETHERNET_TX_CHANNEL);
        } else if (signal_cap != BASE_OUTPUT_NOTIFICATION_CAP + ETHERNET_TX_CHANNEL) {
            microkit_notify(ETHERNET_TX_CHANNEL);
        }
    }
}

void socket_err_func(void *arg, err_t err)
{
    dlog("error %d with socket", err);
}

static err_t socket_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    dlogp(err, "error %d", err);

    if (p == NULL) {
        dlog("closing connection...");
        tcp_close(tpcb);
        return ERR_OK;
    }

    socket_t *socket = arg;

    int copied = 0, remaining = p->tot_len;
    while (remaining != 0) {
        int rx_tail = (socket->rx_head + socket->rx_len) % SOCKET_BUF_SIZE;
        int to_copy = MIN(remaining, SOCKET_BUF_SIZE - MAX(socket->rx_len, rx_tail));
        pbuf_copy_partial(p, socket->rx_buf + rx_tail, to_copy, copied);
        socket->rx_len += to_copy;
        copied += to_copy;
        remaining -= to_copy;
    }
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static err_t socket_sent_callback(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    return ERR_OK;
}

// Connected function
err_t socket_connected(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    ((socket_t *)arg)->connected = 1;
    tcp_sent(tpcb, socket_sent_callback);
    tcp_recv(tpcb, socket_recv_callback);
    return ERR_OK;
}

int tcp_socket_create(void) {
    int free_index = 0;

    socket_t *socket = NULL;
    for (free_index = 0; free_index < MAX_SOCKETS; free_index++) {
        if (!sockets[free_index].used) {
            socket = &sockets[free_index];
            break;
        }
    }
    if (socket == NULL) {
        dlog("no free sockets");
        return -1;
    }

    socket->sock_tpcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (socket->sock_tpcb == NULL) {
        dlog("couldn't create socket");
        return -1;
    }

    socket->used = 1;
    tcp_err(socket->sock_tpcb, socket_err_func);
    tcp_arg(socket->sock_tpcb, socket);

    for (int i = 512; ; i++) {
        if (tcp_bind(socket->sock_tpcb, IP_ADDR_ANY, i) == ERR_OK) {
            return free_index;
        }
    }

    socket->used = 0;
    return -1;
}

int tcp_socket_connect(int index, int port) {
    socket_t *sock = &sockets[index];
    sock->port = port;

    ip_addr_t ipaddr;
    ip4_addr_set_u32(&ipaddr, ipaddr_addr(NFS_SERVER));

    err_t err = tcp_connect(sock->sock_tpcb, &ipaddr, port, socket_connected);
    if (err != ERR_OK) {
        dlog("error connecting (%d)", err);
        return 1;
    }

    return 0;
}

int tcp_socket_close(int index)
{
    socket_t *sock = &sockets[index];

    if (sock->used) {
        int err = tcp_close(sock->sock_tpcb);
        if (err != ERR_OK) {
            dlog("error closing socket (%d)", err);
            return -1;
        }
    }

    sock->sock_tpcb = NULL;
    sock->port = 0;
    sock->connected = 0;
    sock->used = 0;
    sock->rx_head = 0;
    sock->rx_len = 0;
    return 0;
}

int tcp_socket_dup(int index_old, int index_new)
{
    socket_t *sock_old = &sockets[index_old];

    if (!(0 <= index_new && index_new < MAX_SOCKETS)) {
        return -1;
    }
    socket_t *sock_new = &sockets[index_new];

    tcp_close(sock_new->sock_tpcb);

    if (sock_old->used) {
        sock_new->sock_tpcb = sock_old->sock_tpcb;
        tcp_arg(sock_new->sock_tpcb, sock_new);
        sock_new->used = 1;
        sock_new->port = sock_old->port;
        return index_new;
    }
    return -1;
}

int tcp_socket_write(int index, const char *buf, int len) {
    socket_t *sock = &sockets[index];
    int to_write = MIN(len, tcp_sndbuf(sock->sock_tpcb));
    int err = tcp_write(sock->sock_tpcb, (void *)buf, to_write, 1);
    if (err != ERR_OK) {
        dlog("tcp_write failed (%d)", err);
        return -1;
    }
    err = tcp_output(sock->sock_tpcb);
    if (err != ERR_OK) {
        dlog("tcp_output failed (%d)", err);
        return -1;
    }
    return to_write;
}

int tcp_socket_recv(int index, char *buf, int len) {
    socket_t *sock = &sockets[index];
    int remaining = len;
    while (remaining != 0) {
        int to_copy = MIN(len, MIN(sock->rx_len, SOCKET_BUF_SIZE - sock->rx_head));
        if (to_copy == 0) {
            return len - remaining;
        }
        memcpy(buf, sock->rx_buf + sock->rx_head, to_copy);
        sock->rx_head = (sock->rx_head + to_copy) % SOCKET_BUF_SIZE;
        sock->rx_len -= to_copy;
        remaining -= to_copy;
    }
    return len;
}

int tcp_socket_readable(int index) {
    socket_t *socket = &sockets[index];
    return socket->rx_len;
}

int tcp_socket_writable(int index) {
    return !ring_empty(state.tx_ring.free_ring);
}
