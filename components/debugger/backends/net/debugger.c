/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>
#include <sel4/sel4.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sddf/util/util.h>
#include <sddf/util/custom_libc/string.h>
#include <sddf/util/printf.h>
#include <sddf/network/lib_sddf_lwip.h>
#include <sddf/network/queue.h>
#include <sddf/network/config.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <sddf/benchmark/sel4bench.h>
#include <sddf/benchmark/config.h>
#include "lwip/pbuf.h"
#include <libco.h>
#include <gdb.h>
#include <util.h>
#include <vspace.h>
#include "tcp.h"
#include "char_queue.h"
#include "gdbcomp.h"

serial_queue_handle_t serial_tx_queue_handle;

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;

__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;

__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;

__attribute__((__section__(".lib_sddf_lwip_config"))) lib_sddf_lwip_config_t lib_sddf_lwip_config;


int setup_tcp_socket(void);
static bool detached = false;

// TODO - DO NOT DEFINE THIS IN MULTIPLE PLACES
#define NUM_DEBUGEES 2

char_queue_t tcp_input_queue = {
    .tail = 0,
    .head = 0,
    .buf = {0}
};
char input[BUFSIZE];

/* Output buffer */
static char output[BUFSIZE];
static char tcp_output_buf[BUFSIZE];

net_queue_handle_t net_rx_handle;
net_queue_handle_t net_tx_handle;

struct pbuf *head;
struct pbuf *tail;

#define LWIP_TICK_MS 100

static int socket_fd;
static int socket_fd;
bool tcp_initialized = false;

void _putchar(char character) {
    microkit_dbg_putc(character);
}

/**
 * Netif status callback function that output's client's name and
 * obtained IP address.
 *
 * @param ip_addr ip address of the client.
 */
void netif_status_callback(char *ip_addr)
{
    sddf_printf("DHCP request finished, IP address for netif %s is: %s\n", sddf_get_pd_name(), ip_addr);
}

/**
 * Sets a timeout for the next lwip tick.
 */
void set_timeout(void)
{
    sddf_timer_set_timeout(timer_config.driver_id, LWIP_TICK_MS * NS_IN_MS);
}

/**
 * Stores a pbuf to be transmitted upon available transmit buffers.
 *
 * @param p pbuf to be stored.
 */
net_sddf_err_t enqueue_pbufs(struct pbuf *p)
{
    /* Indicate to the tx virt that we wish to be notified about free tx buffers */
    net_request_signal_free(&net_tx_handle);

    if (head == NULL) {
        head = p;
    } else {
        tail->next_chain = p;
    }
    tail = p;

    /* Increment reference count to ensure this pbuf is not freed by lwip */
    pbuf_ref(p);

    return SDDF_LWIP_ERR_OK;
}

void transmit(void)
{
    bool reprocess = true;
    while (reprocess) {
        while (head != NULL && !net_queue_empty_free(&net_tx_handle)) {
            net_sddf_err_t err = sddf_lwip_transmit_pbuf(head);
            if (err == SDDF_LWIP_ERR_LARGE_PBUF) {
                sddf_dprintf("LWIP|ERROR: attempted to send a packet of size %u > BUFFER SIZE %u\n", head->tot_len,
                             NET_BUFFER_SIZE);
            } else if (err != SDDF_LWIP_ERR_OK) {
                sddf_dprintf("LWIP|ERROR: unkown error when trying to send pbuf %p\n", head);
            }

            struct pbuf *temp = head;
            head = temp->next_chain;
            if (head == NULL) {
                tail = NULL;
            }
            pbuf_free(temp);
        }

        /* Only request a signal if there are more pending pbufs to send */
        if (head == NULL || !net_queue_empty_free(&net_tx_handle)) {
            net_cancel_signal_free(&net_tx_handle);
        } else {
            net_request_signal_free(&net_tx_handle);
        }
        reprocess = false;

        if (head != NULL && !net_queue_empty_free(&net_tx_handle)) {
            net_cancel_signal_free(&net_tx_handle);
            reprocess = true;
        }
    }
}

static size_t output_buf_ind = 0;
void gdb_put_char(char c) {
    tcp_output_buf[output_buf_ind++] = c;
}

void gdb_flush() {
    tcp_output_buf[output_buf_ind] = '\0';
    tcp_send(tcp_output_buf, output_buf_ind);
    output_buf_ind = 0;
}

int gdb_get_char(char* c) {
    return char_dequeue(&tcp_input_queue, c);
}


void init(void)
{
    /* Register all the debugee PDs */
    for (int i = 0; i < NUM_DEBUGEES; i++) {
        gdb_register_inferior(i, BASE_VSPACE_CAP + i);
        gdb_register_thread(i, 0, BASE_TCB_CAP + i, output);
    }

    /* Suspend all the debugee PDs */
    suspend_system();

    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size,
                  serial_config.tx.data.vaddr);
    serial_putchar_init(serial_config.tx.id, &serial_tx_queue_handle);

    net_queue_init(&net_rx_handle, net_config.rx.free_queue.vaddr, net_config.rx.active_queue.vaddr,
                   net_config.rx.num_buffers);
    net_queue_init(&net_tx_handle, net_config.tx.free_queue.vaddr, net_config.tx.active_queue.vaddr,
                   net_config.tx.num_buffers);
    net_buffers_init(&net_tx_handle, 0);

    sddf_lwip_init(&lib_sddf_lwip_config, &net_config, &timer_config, net_rx_handle, net_tx_handle, NULL, NULL,
                   netif_status_callback, enqueue_pbufs, NULL, NULL);
    set_timeout();

    setup_tcp_socket();

    sddf_lwip_maybe_notify();

    // Setup the mapping regions for libvspace to use.
	microkit_dbg_puts("Finished setting up debugger!\n");
	gdb_init();
	gdb_start();
}

seL4_Bool fault(microkit_child ch, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo) {
    return gdb_fault(ch, msginfo, reply_msginfo);
}

void notified(microkit_channel ch) {
    gdb_notified();
    // Probably need a better way for yielding, as there is a chance this PD might context switch before being able to run
    // rx and tx processing.
    if (ch == net_config.rx.id) {
        sddf_lwip_process_rx();
    } else if (ch == net_config.tx.id) {
        transmit();
    } else if (ch == timer_config.driver_id) {
        sddf_lwip_process_timeout();
        set_timeout();
    } else if (ch == serial_config.tx.id) {
        // Nothing to do
    } else {
        sddf_dprintf("LWIP|LOG: received notification on unexpected channel: %u\n", ch);
    }

    sddf_lwip_maybe_notify();
}
