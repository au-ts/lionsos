/*
 * Copyright 2023, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>

#include <string.h>

#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <sddf/network/queue.h>
#include <sddf/network/util.h>
#include <sddf/network/constants.h>
#include <sddf/network/config.h>

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

#include "config.h"
#include "nfs.h"
#include "util.h"
#include "tcp.h"
#include "config.h"

#define LINK_SPEED 1000000000 // Gigabit
#define ETHER_MTU 1500
#define NUM_PBUFS 512

typedef struct state
{
    struct netif netif;
    /* mac address for this client */
    uint8_t mac[6];

    /* Pointers to shared buffers */
    net_queue_handle_t rx_queue;
    net_queue_handle_t tx_queue;
} state_t;

typedef struct pbuf_custom_offset {
    struct pbuf_custom custom;
    uintptr_t offset;
} pbuf_custom_offset_t;

enum socket_state {
    socket_state_unallocated,
    socket_state_bound,
    socket_state_connecting,
    socket_state_connected,
    socket_state_closing,
    socket_state_closed_by_peer,
    socket_state_error,
};

typedef struct {
    struct tcp_pcb *sock_tpcb;
    enum socket_state state;

    char rx_buf[SOCKET_BUF_SIZE];
    ssize_t rx_head;
    ssize_t rx_len;
} socket_t;

state_t state;

extern timer_client_config_t timer_config;
extern net_client_config_t net_config;
extern nfs_config_t nfs_config;

LWIP_MEMPOOL_DECLARE(
    RX_POOL,
    NUM_PBUFS * 2,
    sizeof(pbuf_custom_offset_t),
    "Zero-copy RX pool");

// Should only need 1 at any one time, accounts for any reconnecting that might happen
socket_t sockets[MAX_SOCKETS] = {0};

static bool network_ready;
static bool notify_tx;
static bool notify_rx;

int tcp_ready(void) {
    return network_ready;
}

void tcp_maybe_notify(void) {
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

uint32_t sys_now(void) {
    return sddf_timer_time_now(timer_config.driver_id) / NS_IN_MS;
}

static void netif_status_callback(struct netif *netif) {
    if (dhcp_supplied_address(netif)) {
        dlog("DHCP request finished, IP address for netif %s is: %s", netif->name, ip4addr_ntoa(netif_ip4_addr(netif)));

        network_ready = true;
    }
}

static err_t lwip_eth_send(struct netif *netif, struct pbuf *p) {
    err_t ret = ERR_OK;

    if (p->tot_len > NET_BUFFER_SIZE) {
        return ERR_MEM;
    }

    net_buff_desc_t buffer;
    int err = net_dequeue_free(&(state.tx_queue), &buffer);
    if (err) {
        return ERR_MEM;
    }
    
    unsigned char *frame = (unsigned char *)(buffer.io_or_offset + net_config.tx_data.vaddr);
    unsigned int copied = 0;
    for (struct pbuf *curr = p; curr != NULL; curr = curr->next) {
        memcpy(frame + copied, curr->payload, curr->len);
        copied += curr->len;
    }

    buffer.len = copied;
    err = net_enqueue_active(&state.tx_queue, buffer);
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
    pbuf_custom_offset_t *custom_pbuf_offset = (pbuf_custom_offset_t *)buf;
    SYS_ARCH_PROTECT(old_level);
    net_buff_desc_t buffer = {custom_pbuf_offset->offset, 0};
    int err = net_enqueue_free(&(state.rx_queue), buffer);
    assert(!err);
    notify_rx = true;
    LWIP_MEMPOOL_FREE(RX_POOL, custom_pbuf_offset);
    SYS_ARCH_UNPROTECT(old_level);
}

/**
 * Initialise the network interface data structure.
 *
 * @param netif network interface data structuer.
 */
static err_t ethernet_init(struct netif *netif)
{
    if (netif->state == NULL) return ERR_ARG;
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

void tcp_update(void)
{
    sys_check_timeouts();
}

void tcp_init_0(void)
{
    net_queue_init(&state.rx_queue, net_config.rx.free_queue.vaddr, net_config.rx.active_queue.vaddr,
                   net_config.rx.num_buffers);
    net_queue_init(&state.tx_queue, net_config.tx.free_queue.vaddr, net_config.tx.active_queue.vaddr,
                   net_config.tx.num_buffers);
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
        if (!microkit_have_signal) microkit_deferred_notify(net_config.rx.id);
        else if (microkit_signal_cap != BASE_OUTPUT_NOTIFICATION_CAP + net_config.rx.id) microkit_notify(net_config.rx.id);
    }

    if (notify_tx && net_require_signal_active(&state.tx_queue)) {
        net_cancel_signal_active(&state.tx_queue);
        notify_tx = false;
        if (!microkit_have_signal) microkit_deferred_notify(net_config.tx.id);
        else if (microkit_signal_cap != BASE_OUTPUT_NOTIFICATION_CAP + net_config.tx.id) microkit_notify(net_config.tx.id);
    }
}

int socket_id(socket_t *socket) {
    return (int)(socket - sockets);
}

void socket_err_func(void *arg, err_t err)
{
    socket_t *socket = arg;
    if (socket == NULL) {
        dlog("error %d with closed socket", err);
    } else {
        dlog("error %d with socket %d which is in state %d", err, socket_id(socket), socket->state);
        socket->state = socket_state_error;
    }
}

static err_t socket_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    dlogp(err, "error %d", err);

    socket_t *socket = arg;
    assert(socket != NULL);
    int socket_index = socket_id(socket);
    dlogp(err, "error %d with socket %d", err, socket_index);

    switch (socket->state) {

    case socket_state_connected: {
        if (p != NULL) {
            int capacity = SOCKET_BUF_SIZE - socket->rx_len;
            if (capacity < p->tot_len) {
                return ERR_MEM;
            }

            int copied = 0, remaining = p->tot_len;
            while (remaining != 0) {
                int rx_tail = (socket->rx_head + socket->rx_len) % SOCKET_BUF_SIZE;
                int to_copy = MIN(remaining, SOCKET_BUF_SIZE - MAX(socket->rx_len, rx_tail));
                pbuf_copy_partial(p, socket->rx_buf + rx_tail, to_copy, copied);
                socket->rx_len += to_copy;
                copied += to_copy;
                remaining -= to_copy;
            }        
            pbuf_free(p);
        } else {
            socket->state = socket_state_closed_by_peer;
            tcp_close(tpcb);
            tcp_arg(socket->sock_tpcb, NULL);
        }
        return ERR_OK;
    }

    case socket_state_closing: {
        if (p != NULL) {
            pbuf_free(p);
        } else {
            tcp_arg(socket->sock_tpcb, NULL);
            socket->state = socket_state_unallocated;
            socket->sock_tpcb = NULL;
            socket->rx_head = 0;
            socket->rx_len = 0;
        }
        return ERR_OK;
    }

    default:
        dlog("called on invalid socket state: %d (socket=%d)", socket->state, socket_index);
        assert(false);
        return ERR_OK;
    }
}

static err_t socket_sent_callback(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    socket_t *socket = arg;
    int socket_index = socket - sockets;
    return ERR_OK;
}

err_t socket_connected(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    socket_t *socket = arg;
    assert(socket != NULL);
    assert(socket->state == socket_state_connecting);

    socket->state = socket_state_connected;
    
    tcp_sent(tpcb, socket_sent_callback);
    tcp_recv(tpcb, socket_recv_callback);

    tpcb->so_options |= SOF_KEEPALIVE;

    return ERR_OK;
}

int tcp_socket_create(void) {
    int free_index;
    socket_t *socket = NULL;
    for (free_index = 0; free_index < MAX_SOCKETS; free_index++) {
        if (sockets[free_index].state == socket_state_unallocated) {
            socket = &sockets[free_index];
            break;
        }
    }
    if (socket == NULL) {
        dlog("no free sockets");
        return -1;
    }

    assert(socket->sock_tpcb == NULL);
    assert(socket->rx_head == 0);
    assert(socket->rx_len == 0);

    socket->sock_tpcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (socket->sock_tpcb == NULL) {
        dlog("couldn't create socket");
        return -1;
    }

    socket->sock_tpcb->so_options |= SOF_KEEPALIVE;

    tcp_err(socket->sock_tpcb, socket_err_func);
    tcp_arg(socket->sock_tpcb, socket);

    for (int i = 512; ; i++) {
        if (tcp_bind(socket->sock_tpcb, IP_ADDR_ANY, i) == ERR_OK) {
            socket->state = socket_state_bound;
            return free_index;
        }
    }

    return -1;
}

int tcp_socket_connect(int index, int port) {
    socket_t *sock = &sockets[index];
    assert(sock->state == socket_state_bound);

    ip_addr_t ipaddr;
    ip4_addr_set_u32(&ipaddr, ipaddr_addr(nfs_config.server));

    err_t err = tcp_connect(sock->sock_tpcb, &ipaddr, port, socket_connected);
    if (err != ERR_OK) {
        dlog("error connecting (%d)", err);
        return 1;
    }
    sock->state = socket_state_connecting;

    return 0;
}

int tcp_socket_close(int index)
{
    socket_t *socket = &sockets[index];

    switch (socket->state) {

    case socket_state_connected: {
        socket->state = socket_state_closing;
        int err = tcp_close(socket->sock_tpcb);
        dlogp(err != ERR_OK, "error closing socket (%d)", err);
        return err != ERR_OK;
    }
    
    case socket_state_bound:
    case socket_state_error:
    case socket_state_closed_by_peer: {
        socket->state = socket_state_unallocated;
        socket->sock_tpcb = NULL;
        socket->rx_head = 0;
        socket->rx_len = 0;

        return 0;
    }

    default:
        dlog("called on invalid socket state: %d", socket->state);
        assert(false);
        return 0;
    }
}

int tcp_socket_write(int index, const char *buf, size_t len) {
    socket_t *sock = &sockets[index];
    int available = tcp_sndbuf(sock->sock_tpcb);

    if (available == 0) {
        dlog("no space available");
        return -2;
    }
    int to_write = MIN(len, available);
    int err = tcp_write(sock->sock_tpcb, (void *)buf, to_write, 1);
    if (err == ERR_MEM) {
        dlog("tcp_write returned ERR_MEM");
        return -2;
    } else if (err != ERR_OK) {
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

ssize_t tcp_socket_recv(int index, char *buf, ssize_t len) {
    socket_t *sock = &sockets[index];
    if (sock->state != socket_state_connected) {
        return -1;
    }
    ssize_t copied = 0;
    while (copied != len) {
        ssize_t to_copy = MIN(len - copied, MIN(sock->rx_len, SOCKET_BUF_SIZE - sock->rx_head));
        if (to_copy == 0) {
            break;
        }
        memcpy(buf + copied, sock->rx_buf + sock->rx_head, to_copy);
        sock->rx_head = (sock->rx_head + to_copy) % SOCKET_BUF_SIZE;
        sock->rx_len -= to_copy;
        copied += to_copy;
    }
    tcp_recved(sock->sock_tpcb, copied);
    return copied;
}

int tcp_socket_readable(int index) {
    socket_t *socket = &sockets[index];
    return socket->rx_len;
}

int tcp_socket_writable(int index) {
    return !net_queue_empty_free(&state.tx_queue);
}

int tcp_socket_hup(int index) {
    return sockets[index].state == socket_state_closed_by_peer;
}

int tcp_socket_err(int index) {
    return sockets[index].state == socket_state_error;
}
