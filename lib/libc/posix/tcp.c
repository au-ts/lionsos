/*
 * Copyright 2023, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <lions/posix/posix.h>
#include <microkit.h>
#include <libmicrokitco.h>

#include <string.h>
#include <arpa/inet.h>
#include <errno.h>

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

#define MAX_LISTEN_BACKLOG 10

enum socket_state {
    socket_state_unallocated,
    socket_state_allocated,
    socket_state_bound,
    socket_state_connecting,
    socket_state_connected,
    socket_state_closing,
    socket_state_closed_by_peer,
    socket_state_error,
    socket_state_listening,
};

typedef struct {
    struct tcp_pcb *pending_pcbs[MAX_LISTEN_BACKLOG];
    int head;
    int tail;
    microkit_cothread_sem_t accept_sem;
} accept_queue_t;

typedef struct {
    struct tcp_pcb *sock_tpcb;
    enum socket_state state;

    char rx_buf[SOCKET_BUF_SIZE];
    ssize_t rx_head;
    ssize_t rx_len;

    accept_queue_t accept_queue;
} socket_t;

extern timer_client_config_t timer_config;
extern net_client_config_t net_config;
extern lib_sddf_lwip_config_t lib_sddf_lwip_config;

net_queue_handle_t rx_handle;
net_queue_handle_t tx_handle;

socket_t sockets[MAX_SOCKETS] = { 0 };

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

    case socket_state_allocated:
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

int tcp_socket_allocate() {
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

    socket->state = socket_state_allocated;

    return free_index;
}

int tcp_socket_init(int index) {
    socket_t *socket = &sockets[index];

    assert(socket->state == socket_state_allocated);
    socket->sock_tpcb = tcp_new_ip_type(IPADDR_TYPE_V4);

    if (socket->sock_tpcb == NULL) {
        dlog("couldn't init socket");
        return -1;
    }

    socket->sock_tpcb->so_options |= SOF_KEEPALIVE;

    tcp_err(socket->sock_tpcb, socket_err_func);
    tcp_arg(socket->sock_tpcb, socket);

    return 0;
}

int tcp_socket_connect(int index, uint32_t addr, uint16_t port) {
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
    case socket_state_listening:
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

static err_t tcp_socket_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err) {
    socket_t *listen_socket = (socket_t *)arg;
    assert(listen_socket->state == socket_state_listening);

    if (err != ERR_OK) {
        return -1;
    }

    accept_queue_t *q = &listen_socket->accept_queue;

    int next_head = (q->head + 1) % MAX_LISTEN_BACKLOG;

    if (next_head == q->tail) {
        tcp_close(newpcb);
        // Wake the accept() call to handle the insufficient backlog case
        // TODO: communicate errors properly
        microkit_cothread_semaphore_signal(&listen_socket->accept_queue.accept_sem);
        return ERR_MEM;
    }

    q->pending_pcbs[q->head] = newpcb;
    q->head = next_head;

    microkit_cothread_semaphore_signal(&listen_socket->accept_queue.accept_sem);

    return ERR_OK;
}

int tcp_socket_listen(int index, int backlog) {
    socket_t *socket = &sockets[index];

    // lwIP docs: The tcp_listen() function returns a new connection identifier,
    // and the one passed as an argument to the function will be deallocated.
    struct tcp_pcb *newpcb = tcp_listen_with_backlog(socket->sock_tpcb, backlog);
    assert(newpcb != NULL);
    socket->sock_tpcb = newpcb;
    socket->state = socket_state_listening;
    assert(socket->sock_tpcb->state == LISTEN);

    socket->accept_queue.head = 0;
    socket->accept_queue.tail = 0;
    microkit_cothread_semaphore_init(&socket->accept_queue.accept_sem);

    tcp_accept(socket->sock_tpcb, tcp_socket_accept_cb);

    return 0;
}

int tcp_socket_accept(int index) {
    socket_t *listen_socket = &sockets[index];
    assert(listen_socket->state == socket_state_listening);

    accept_queue_t *q = &listen_socket->accept_queue;

    // Block until there is a pending connection, signalled by the accept callback
    microkit_cothread_semaphore_wait(&(q->accept_sem));

    if (q->head == q->tail) {
        // Not enough backlog space
        return -ENOMEM;
    }

    struct tcp_pcb *new_conn_pcb = q->pending_pcbs[q->tail];
    q->tail = (q->tail + 1) % MAX_LISTEN_BACKLOG;

    int new_socket_index = tcp_socket_allocate();
    if (new_socket_index < 0) {
        tcp_close(new_conn_pcb);
        return -1;
    }

    socket_t *socket = &sockets[new_socket_index];

    socket->sock_tpcb = new_conn_pcb;
    tcp_err(socket->sock_tpcb, socket_err_func);
    tcp_arg(socket->sock_tpcb, socket);
    tcp_sent(new_conn_pcb, socket_sent_callback);
    tcp_recv(new_conn_pcb, socket_recv_callback);

    return new_socket_index;
}

int tcp_socket_bind(int index, uint32_t addr, uint16_t port) {
    socket_t *sock = &sockets[index];

    ip_addr_t ipaddr;
    ip4_addr_set_u32(&ipaddr, addr);

    err_t err = tcp_bind(sock->sock_tpcb, &ipaddr, port);
    if (err != ERR_OK) {
        dlog("error binding (%d)", err);
        return 1;
    }

    sock->state = socket_state_bound;

    return 0;
}

int tcp_socket_getsockname(int index, uint32_t *addr, uint16_t *port) {
    socket_t *socket = &sockets[index];

    *addr = ip4_addr_get_u32(&socket->sock_tpcb->local_ip);
    *port = socket->sock_tpcb->local_port;

    return 0;
}

int tcp_socket_getpeername(int index, uint32_t *addr, uint16_t *port) {
    socket_t *socket = &sockets[index];

    *addr = ip4_addr_get_u32(&socket->sock_tpcb->remote_ip);
    *port = socket->sock_tpcb->remote_port;

    return 0;
}
