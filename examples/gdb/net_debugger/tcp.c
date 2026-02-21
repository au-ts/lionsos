/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <string.h>
#include <microkit.h>

#include "lwip/ip.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include <sddf/util/util.h>
#include <gdb.h>

#include "char_queue.h"

extern char_queue_t tcp_input_queue;
extern bool tcp_initialized;

static struct tcp_pcb *gdb_pcb;

static err_t tcp_sent_gdb(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    // tcp_recved is only for increasing the TCP window, and isn't required to
    // ACK incoming packets (that is done automatically on receive).
    tcp_recved(pcb, len);

    return ERR_OK;
}

static void tcp_err_gdb(void *arg, err_t err)
{
    sddf_printf("tcp_echo: %s\n", lwip_strerr(err));
}

static err_t tcp_recv_gdb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    if (p == NULL) {
        // closing
        sddf_printf("tcp_echo[%s:%d]: closing\n",
                    ipaddr_ntoa(&pcb->remote_ip), pcb->remote_port
                   );

        tcp_arg(pcb, NULL);

        err = tcp_close(pcb);
        if (err) {
            sddf_printf("tcp_echo[%s:%d]: close error: %s\n",
                        ipaddr_ntoa(&pcb->remote_ip), pcb->remote_port,
                        lwip_strerr(err)
                       );
            return err;
        }
        return ERR_OK;
    }

    if (err) {
        sddf_printf("tcp_echo[%s:%d]: recv error: %s\n",
                    ipaddr_ntoa(&pcb->remote_ip), pcb->remote_port,
                    lwip_strerr(err)
                   );
        return err;
    }

    char tmp[BUFSIZE];
    pbuf_copy_partial(p, (void *) tmp, p->tot_len, 0);

    /* Null terminate the string*/
    tmp[p->tot_len] = 0;

    char_queue_enqueue_batch(&tcp_input_queue, strnlen(tmp, BUFSIZE), tmp);

    pbuf_free(p);
    return ERR_OK;
}

int tcp_send(void *buf, uint32_t len) {
    err_t error = tcp_write(gdb_pcb, buf, len, TCP_WRITE_FLAG_COPY);
    if (error) {
        sddf_printf("Failed to send message");
        return 1;
    }

    error = tcp_output(gdb_pcb);
    if (error) {
        sddf_printf("Failed to output message");
    }

    return 0;
}


static err_t tcp_accept_gdb(void *arg, struct tcp_pcb *pcb, err_t err)
{
    tcp_nagle_disable(pcb);
    tcp_sent(pcb, tcp_sent_gdb);
    tcp_recv(pcb, tcp_recv_gdb);
    tcp_err(pcb, tcp_err_gdb);
    gdb_pcb = pcb;

    tcp_initialized = true;

    return ERR_OK;
}


int setup_tcp_socket(void)
{
    gdb_pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (gdb_pcb == NULL) {
        sddf_printf("Failed to open TCP echo socket\n");
        return -1;
    }

    err_t error = tcp_bind(gdb_pcb, IP_ANY_TYPE, 1234);
    if (error) {
        sddf_printf("Failed to bind TCP echo socket: %s\n", lwip_strerr(error));
        return -1;
    }

    gdb_pcb = tcp_listen_with_backlog_and_err(gdb_pcb, 1, &error);
    if (error) {
        sddf_printf("Failed to listen on TCP echo socket: %s\n", lwip_strerr(error));
        return -1;
    }

    tcp_accept(gdb_pcb, tcp_accept_gdb);

    return 0;
}
