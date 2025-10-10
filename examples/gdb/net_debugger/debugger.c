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
#include <sddf/util/string.h>
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

// The user provides the following mapping regions.
// The small mapping region must be of page_size 0x1000
// THe large mapping region must be of page_size 0x200000
uintptr_t small_mapping_mr;
uintptr_t large_mapping_mr;

serial_queue_handle_t serial_tx_queue_handle;

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;

__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;

__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;

__attribute__((__section__(".lib_sddf_lwip_config"))) lib_sddf_lwip_config_t lib_sddf_lwip_config;


int setup_tcp_socket(void);

typedef enum event_state {
    eventState_none = 0,
    eventState_waitingForInputEventLoop,
    eventState_waitingForInputFault
} event_state_t;
event_state_t state = eventState_none;
static bool detached = false;

cothread_t t_event, t_main, t_fault;

// TODO - DO NOT DEFINE THIS IN MULTIPLE PLACES
#define NUM_DEBUGEES 2

#define STACK_SIZE 4096
static char t_main_stack[STACK_SIZE];
static char t_fault_stack[STACK_SIZE];

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
static bool debugger_initialized = false;

uint32_t gdb_read_word(uint16_t client, uintptr_t addr, seL4_Word *val)
{
    libvspace_read_word(client, addr, val);
}

uint32_t gdb_write_word(uint16_t client, uintptr_t addr, seL4_Word val)
{
    libvspace_write_word(client, addr, val);
}

void _putchar(char character) {
    microkit_dbg_putc(character);
}

/**
 * Netif status callback function that output's client's Microkit name and
 * obtained IP address.
 *
 * @param ip_addr ip address of the client.
 */
void netif_status_callback(char *ip_addr)
{
    sddf_printf("DHCP request finished, IP address for netif %s is: %s\n", microkit_name, ip_addr);
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

/**
 * Sets a timeout for the next lwip tick.
 */
void set_timeout(void)
{
    sddf_timer_set_timeout(timer_config.driver_id, LWIP_TICK_MS * NS_IN_MS);
}

void transmit(void)
{
    bool reprocess = true;
    while (reprocess) {
        while (head != NULL && !net_queue_empty_free(&net_tx_handle)) {
            net_sddf_err_t err = sddf_lwip_transmit_pbuf(head);
            if (err == SDDF_LWIP_ERR_PBUF) {
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

char gdb_get_char(event_state_t new_state) {
    while (char_queue_empty(&tcp_input_queue, tcp_input_queue.head)) {
        // Wait for the virt to tell us some input has come through
        state = new_state;
        co_switch(t_event);
    }

    char c;
    char_dequeue(&tcp_input_queue, &c);
    return c;
}

char *get_packet(event_state_t new_state) {
    char c;
    int count;
    /* Checksum and expected checksum */
    unsigned char cksum, xcksum;
    char *buf = input;
    (void) buf;

    while (1) {
        /* Wait for the start character - ignoring all other characters */
        c = gdb_get_char(new_state);
        while (c != '$') {
            /* Ctrl-C character - should result in an interrupt */
            if (c == 3) {
                buf[0] = c;
                buf[1] = 0;
                return buf;
            }
            c = gdb_get_char(new_state);
        }
        retry:
        /* Initialize cksum variables */
        cksum = 0;
        xcksum = -1;
        count = 0;

        /* Read until we see a # or the buffer is full */
        while (count < BUFSIZE - 1) {
            c = gdb_get_char(new_state);

            if (c == '$') {
                goto retry;
            } else if (c == '#') {
                break;
            }
            cksum += c;
            buf[count++] = c;
        }

        /* Null terminate the string */
        buf[count] = 0;

        if (c == '#') {
            c = gdb_get_char(new_state);
            xcksum = hexchar_to_int(c) << 4;
            c = gdb_get_char(new_state);
            xcksum += hexchar_to_int(c);

            if (cksum != xcksum) {
                tcp_send("-", 1);
            } else {
                tcp_send("+", 1);

                if (buf[2] == ':') {
                    tcp_send(&input[1], 1);
                    tcp_send(&input[2], 1);

                    return &buf[3];
                }

                return buf;
            }
        }
    }

    return NULL;
}

int put_packet(char *output, event_state_t new_state) {
    uint8_t cksum;
    char *tcp_output_tmp = tcp_output_buf;
    for (;;) {
        *(tcp_output_tmp++) = '$';
        for (cksum = 0; *output; tcp_output_tmp++, output++) {
            cksum += *output;
            *tcp_output_tmp = *output;
        }
        *(tcp_output_tmp++) = '#';
        *(tcp_output_tmp++) = int_to_hexchar(cksum >> 4);
        *(tcp_output_tmp++) = int_to_hexchar(cksum % 16);
        *(tcp_output_tmp++) = 0;
        tcp_send(tcp_output_buf, strnlen(tcp_output_buf, BUFSIZE));
        char c = gdb_get_char(new_state);
        if (c == '+') break;
    }
}

void event_loop(){
    bool resume = false;
    while (1) {
        char *input = get_packet(eventState_waitingForInputEventLoop);
        if (detached || input[0] == 3) {
            /* If we got a ctrl-c packet, we should suspend the whole system */
            suspend_system();
            detached = false;
        }

        resume = gdb_handle_packet(input, output, &detached);

        if (!resume || detached) {
            put_packet(output, eventState_waitingForInputEventLoop);
        }

        if (resume) {
            resume_system();
        }
    }
}


err_t gdb_connected() {
    /* Set up the coroutines */
    t_event = co_active();
    t_main = co_derive((void *) t_main_stack, STACK_SIZE, event_loop);

    /* We have accepted a connection, so we are ready */
    debugger_initialized = true;
    co_switch(t_main);
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

    sddf_lwip_init(&lib_sddf_lwip_config, &net_config, &timer_config, net_rx_handle, net_tx_handle, NULL,
                   netif_status_callback, enqueue_pbufs);
    set_timeout();

    setup_tcp_socket();

    sddf_lwip_maybe_notify();

    // Setup the mapping regions for libvspace to use.
    libvspace_set_small_mapping_region(small_mapping_mr);
    libvspace_set_large_mapping_region(large_mapping_mr);
}

void fault_message() {
    put_packet(output, eventState_waitingForInputFault);
    // Go back to waiting for normal input after we send the fault packet to the host
    state = eventState_waitingForInputEventLoop;
    co_switch(t_event);
}

seL4_Bool fault(microkit_child ch, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo) {
    seL4_Word reply_mr = 0;

    suspend_system();

    bool have_reply;
    DebuggerError err = gdb_handle_fault(ch, 0, microkit_msginfo_get_label(msginfo), &reply_mr, output, &have_reply);
    if (err) {
        microkit_dbg_puts("GDB: Internal assertion failed. Could not find faulting thread");
    }

    // Start a coroutine for dealing with the fault and transmitting a message to the host
    t_event = co_active();
    t_fault = co_derive((void *) t_fault_stack, STACK_SIZE, fault_message);
    co_switch(t_fault);

    if (have_reply) {
        *reply_msginfo = microkit_msginfo_new(0, 0);
        return true;
    }

    return false;
}

void notified(microkit_channel ch) {
    if (ch == net_config.rx.id) {
        sddf_lwip_process_rx();
        if (debugger_initialized) {
            if (state == eventState_waitingForInputFault) {
                state = eventState_none;
                co_switch(t_fault);
            }

            /* This is not an else if because we want to switch to the event loop after
               handling the fault message. We could probably do this unconditionally?  */
            if (state == eventState_waitingForInputEventLoop) {
                state = eventState_none;
                co_switch(t_main);
            }
        }
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

    if (tcp_initialized && !debugger_initialized) {
        gdb_connected();
    }

    sddf_lwip_maybe_notify();
}
