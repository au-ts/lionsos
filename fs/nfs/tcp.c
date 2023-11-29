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

typedef enum
{
    ORIGIN_RX_QUEUE,
    ORIGIN_TX_QUEUE,
} ethernet_buffer_origin_t;

typedef struct ethernet_buffer
{
    /* The actual underlying memory of the buffer */
    uintptr_t buffer;
    /* The physical size of the buffer */
    size_t size;
    /* Queue from which the buffer was allocated */
    char origin;
    /* Index into buffer_metadata array */
    unsigned int index;
    /* in use */
    bool in_use;
} ethernet_buffer_t;

typedef struct state
{
    struct netif netif;
    /* mac address for this client */
    uint8_t mac[6];

    /* Pointers to shared buffers */
    ring_handle_t rx_ring;
    ring_handle_t tx_ring;
    /*
     * Metadata associated with buffers
     */
    ethernet_buffer_t buffer_metadata[NUM_BUFFERS * 2];
} state_t;

typedef struct {
    struct tcp_pcb *sock_tpcb;
    int port;
    int connected;
    int used;

    char rx_buf[SOCKET_BUF_SIZE];
    int rx_head;
    int rx_len;
} socket_t;

typedef struct lwip_custom_pbuf
{
    struct pbuf_custom custom;
    ethernet_buffer_t *buffer;
    state_t *state;
} lwip_custom_pbuf_t;

state_t state;

LWIP_MEMPOOL_DECLARE(
    RX_POOL,
    NUM_BUFFERS * 2,
    sizeof(lwip_custom_pbuf_t),
    "Zero-copy RX pool");

ring_buffer_t *rx_free;
ring_buffer_t *rx_used;
uintptr_t tx_free;
uintptr_t tx_used;
uintptr_t copy_rx;
uintptr_t shared_dma_vaddr;

// Should only need 1 at any one time, accounts for any reconnecting that might happen
socket_t sockets[MAX_SOCKETS] = {0};

bool network_ready;

int tcp_ready(void) {
    return network_ready;
}

uint32_t sys_now(void) {
    return sddf_timer_time_now() * US_IN_MS;
}

static void get_mac(void) {
    microkit_ppcall(ETHERNET_INIT_CHANNEL, microkit_msginfo_new(0, 0));
    uint32_t palr = microkit_mr_get(0);
    uint32_t paur = microkit_mr_get(1);
    state.mac[3] = palr >> 24;
    state.mac[2] = palr >> 16 & 0xff;
    state.mac[1] = palr >> 8 & 0xff;
    state.mac[0] = palr & 0xff;
    state.mac[5] = paur >> 8 & 0xff;
    state.mac[4] = paur & 0xff;
}

static void netif_status_callback(struct netif *netif) {
    if (dhcp_supplied_address(netif)) {
        dlog("DHCP request finished, IP address for netif %s is: %s", netif->name, ip4addr_ntoa(netif_ip4_addr(netif)));
        network_ready = true;
    }
}

/**
 * Allocate an empty TX buffer from the empty pool
 *
 * @param state client state data.
 * @param length length of buffer required
 *
 */
static inline ethernet_buffer_t *alloc_tx_buffer(state_t *state, size_t length) {
    if (BUF_SIZE < length) {
        dlog("Requested buffer size too large.");
        return NULL;
    }

    uintptr_t addr;
    unsigned int len;
    ethernet_buffer_t *buffer;

    dequeue_free(&state->tx_ring, &addr, &len, (void **)&buffer);
    if (!buffer) {
        dlog("lwip: dequeued a null ethernet buffer");
    }

    return buffer;
}


static err_t lwip_eth_send(struct netif *netif, struct pbuf *p) {
    /* Grab an available TX buffer, copy pbuf data over,
    add to used tx ring, notify server */
    err_t ret = ERR_OK;

    if (p->tot_len > BUF_SIZE) {
        return ERR_MEM;
    }

    state_t *state = (state_t *)netif->state;

    ethernet_buffer_t *buffer = alloc_tx_buffer(state, p->tot_len);
    if (buffer == NULL) {
        return ERR_MEM;
    }
    unsigned char *frame = (unsigned char *)buffer->buffer;

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

    int err = seL4_ARM_VSpace_Clean_Data(3, (uintptr_t)frame, (uintptr_t)frame + copied);
    if (err) {
        dlog("ARM Vspace clean failed: %d\n", err);
    }

    /* insert into the used tx queue */
    int error = enqueue_used(&state->tx_ring, (uintptr_t)frame, copied, buffer);
    if (error) {
        dlog("TX used ring full\n");
        enqueue_free(&(state->tx_ring), (uintptr_t)frame, BUF_SIZE, buffer);
        return ERR_MEM;
    }

    /* Notify the server for next time we recv() */
    have_signal = true;
    signal_msg = seL4_MessageInfo_new(0, 0, 0, 0);
    signal = (BASE_OUTPUT_NOTIFICATION_CAP + ETHERNET_TX_CHANNEL);
    /* NOTE: If driver is passive, we want to Call instead. */

    return ret;
}

static inline void return_buffer(state_t *state, ethernet_buffer_t *buffer) {
    /* As the rx_freeable ring is the size of the number of buffers we have,
    the ring should never be full. */
    enqueue_free(&(state->rx_ring), buffer->buffer, BUF_SIZE, buffer);
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
    return_buffer(custom_pbuf->state, custom_pbuf->buffer);
    LWIP_MEMPOOL_FREE(RX_POOL, custom_pbuf);
    SYS_ARCH_UNPROTECT(old_level);
}

/**
 * Create a pbuf structure to pass to the network interface.
 *
 * @param state client state data.
 * @param buffer ethernet buffer containing metadata for the actual buffer
 * @param length length of data
 *
 * @return the newly created pbuf.
 */
static struct pbuf *create_interface_buffer(state_t *state, ethernet_buffer_t *buffer, size_t length) {
    lwip_custom_pbuf_t *custom_pbuf = (lwip_custom_pbuf_t *)LWIP_MEMPOOL_ALLOC(RX_POOL);

    custom_pbuf->state = state;
    custom_pbuf->buffer = buffer;
    custom_pbuf->custom.custom_free_function = interface_free_buffer;

    return pbuf_alloced_custom(
        PBUF_RAW,
        length,
        PBUF_REF,
        &custom_pbuf->custom,
        (void *)buffer->buffer,
        buffer->size);
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
        ethernet_buffer_t *buffer;

        dequeue_used(&state.rx_ring, &addr, &len, (void **)&buffer);

        if (addr != buffer->buffer) {
            dlog("sanity check failed");
        }

        /* Invalidate the memory */
        int err = seL4_ARM_VSpace_Invalidate_Data(3, buffer->buffer, buffer->buffer + ETHER_MTU);
        if (err) {
            dlog("ARM Vspace invalidate failed: %d", err);
        }

        struct pbuf *p = create_interface_buffer(&state, (void *)buffer, len);

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
    ring_init(&state.rx_ring, rx_free, (ring_buffer_t *)rx_used, 1, SIZE, SIZE);
    ring_init(&state.tx_ring, (ring_buffer_t *)tx_free, (ring_buffer_t *)tx_used, 1, SIZE, SIZE);

    for (int i = 0; i < NUM_BUFFERS - 1; i++) {
        ethernet_buffer_t *buffer = &state.buffer_metadata[i];
        *buffer = (ethernet_buffer_t){
            .buffer = shared_dma_vaddr + (BUF_SIZE * i),
            .size = BUF_SIZE,
            .origin = ORIGIN_RX_QUEUE,
            .index = i,
            .in_use = false,
        };
        enqueue_free(&state.rx_ring, buffer->buffer, BUF_SIZE, buffer);
    }

    for (int i = 0; i < NUM_BUFFERS - 1; i++) {
        ethernet_buffer_t *buffer = &state.buffer_metadata[i + NUM_BUFFERS];
        *buffer = (ethernet_buffer_t){
            .buffer = shared_dma_vaddr + (BUF_SIZE * (i + NUM_BUFFERS)),
            .size = BUF_SIZE,
            .origin = ORIGIN_TX_QUEUE,
            .index = i + NUM_BUFFERS,
            .in_use = false,
        };
        enqueue_free(&state.tx_ring, buffer->buffer, BUF_SIZE, buffer);
    }

    lwip_init();
    LWIP_MEMPOOL_INIT(RX_POOL);
    get_mac();

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
        dlog("Netif add returned NULL\n");
    }

    netif_set_default(&(state.netif));

    microkit_notify(ETHERNET_INIT_CHANNEL);
}

void tcp_init_1(void)
{
    netif_set_status_callback(&(state.netif), netif_status_callback);
    netif_set_up(&(state.netif));

    int err = dhcp_start(&(state.netif));
    dlogp(err, "failed to start DHCP negotiation");
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
