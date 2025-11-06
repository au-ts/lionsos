/*
 * Copyright 2023, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
 
#include <lions/posix/posix.h>
#include <microkit.h>

#include <string.h>

#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <sddf/network/lib_sddf_lwip.h>
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

#include <lions/util.h>
#include <lions/posix/tcp.h>

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

extern timer_client_config_t timer_config;
extern net_client_config_t net_config;
extern lib_sddf_lwip_config_t lib_sddf_lwip_config;

net_queue_handle_t rx_handle;
net_queue_handle_t tx_handle;

// Should only need 1 at any one time, accounts for any reconnecting that might happen
socket_t sockets[MAX_SOCKETS] = {0};

static bool network_ready;

int tcp_ready(void) { return network_ready; }

static void netif_status_callback(char *ip_addr) {
    printf("%s: %s:%d:%s: DHCP request finished, IP address for %s is: %s\r\n", microkit_name, __FILE__, __LINE__,
           __func__, microkit_name, ip_addr);

    network_ready = true;
}

void tcp_init_0(void) {
    net_queue_init(&rx_handle, net_config.rx.free_queue.vaddr, net_config.rx.active_queue.vaddr,
                   net_config.rx.num_buffers);
    net_queue_init(&tx_handle, net_config.tx.free_queue.vaddr, net_config.tx.active_queue.vaddr,
                   net_config.tx.num_buffers);
    net_buffers_init(&tx_handle, 0);

    sddf_lwip_init(&lib_sddf_lwip_config, &net_config, &timer_config, rx_handle, tx_handle, NULL, printf,
                   netif_status_callback, NULL, NULL, NULL);

    sddf_lwip_maybe_notify();
}

int socket_id(socket_t *socket) { return (int)(socket - sockets); }

void socket_err_func(void *arg, err_t err) {
    socket_t *socket = arg;
    if (socket == NULL) {
        dlog("error %d with closed socket", err);
    } else {
        dlog("error %d with socket %d which is in state %d", err, socket_id(socket), socket->state);
        socket->state = socket_state_error;
    }
}

static err_t socket_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
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

static err_t socket_sent_callback(void *arg, struct tcp_pcb *pcb, u16_t len) { return ERR_OK; }

err_t socket_connected(void *arg, struct tcp_pcb *tpcb, err_t err) {
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

    for (int i = 512;; i++) {
        if (tcp_bind(socket->sock_tpcb, IP_ADDR_ANY, i) == ERR_OK) {
            socket->state = socket_state_bound;
            return free_index;
        }
    }

    return -1;
}

int tcp_socket_connect(int index, uint16_t port, uint32_t addr) {
    socket_t *sock = &sockets[index];
    assert(sock->state == socket_state_bound);

    ip_addr_t ipaddr;
    ip4_addr_set_u32(&ipaddr, addr);

    err_t err = tcp_connect(sock->sock_tpcb, &ipaddr, port, socket_connected);
    if (err != ERR_OK) {
        dlog("error connecting (%d)", err);
        return 1;
    }
    sock->state = socket_state_connecting;

    return 0;
}

int tcp_socket_close(int index) {
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

int tcp_socket_writable(int index) { return !net_queue_empty_free(&tx_handle); }

int tcp_socket_hup(int index) { return sockets[index].state == socket_state_closed_by_peer; }

int tcp_socket_err(int index) { return sockets[index].state == socket_state_error; }
