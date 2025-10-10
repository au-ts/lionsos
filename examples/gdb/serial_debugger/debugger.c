/*
 * Copyright 2024, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>
#include <sel4/sel4_arch/types.h>
#include <gdb.h>
#include <util.h>
#include <libco.h>
#include <stddef.h>
#include <sddf/serial/config.h>
#include <sddf/serial/queue.h>
#include <sddf/util/printf.h>
#include <sddf/serial/config.h>

__attribute__((__section__(".serial_client_config"))) serial_client_config_t config;

typedef enum event_state {
    eventState_none = 0,
    eventState_waitingForInputEventLoop,
    eventState_waitingForInputFault
} event_state_t;

cothread_t t_event, t_main, t_fault;

// TODO - DO NOT DEFINE THIS IN MULTIPLE PLACES
#define NUM_DEBUGEES 2

#define STACK_SIZE 4096
static char t_main_stack[STACK_SIZE];
static char t_fault_stack[STACK_SIZE];

/* Input buffer */
static char input[BUFSIZE];

/* Output buffer */
static char output[BUFSIZE];

serial_queue_t *rx_queue;
serial_queue_t *tx_queue;

char *rx_data;
char *tx_data;

serial_queue_handle_t rx_queue_handle;
serial_queue_handle_t tx_queue_handle;

/* The current event state and phase */
event_state_t state = eventState_none;
static bool detached = false;

void _putchar(char character) {
    microkit_dbg_putc(character);
}

/* @alwin: surely there is a less disgusting way of doing this */
void gdb_put_char(char c) {
    sddf_putchar_unbuffered(c);
}

char gdb_get_char(event_state_t new_state) {
    while (serial_queue_empty(&rx_queue_handle, rx_queue_handle.queue->head)) {
        // Wait for the virt to tell us some input has come through
        state = new_state;
        co_switch(t_event);
    }

    char c;
    serial_dequeue(&rx_queue_handle, &c);

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
                gdb_put_char('-');   /* checksum failed */
            } else {
                gdb_put_char('+');   /* checksum success, ack*/

                if (buf[2] == ':') {
                    gdb_put_char(buf[0]);
                    gdb_put_char(buf[1]);

                    return &buf[3];
                }

                return buf;
            }
        }
    }

    return NULL;
}

/*
 * Send a packet, computing it's checksum, waiting for it's acknoledge.
 * If there is not ack, packet will be resent.
 */
static void put_packet(char *buf, event_state_t new_state)
{
    uint8_t cksum;
    for (;;) {
        gdb_put_char('$');
        char *buf2 = buf;
        for (cksum = 0; *buf2; buf2++) {
            cksum += *buf2;
            gdb_put_char(*buf2);
        }
        gdb_put_char('#');
        gdb_put_char(int_to_hexchar(cksum >> 4));
        gdb_put_char(int_to_hexchar(cksum % 16));
        char c = gdb_get_char(new_state);
        if (c == '+') break;
    }
}

static void event_loop();
static void init_phase2();

static void event_loop() {
    bool resume = false;
    /* The event loop runs perpetually if we are in the standard event loop phase */
    while (true) {
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

void init() {
    assert(serial_config_check_magic(&config));

    /* Register all of the inferiors  */
    for (int i = 0; i < NUM_DEBUGEES; i++) {
        gdb_register_inferior(i, BASE_VSPACE_CAP + i);
        gdb_register_thread(i, 0, BASE_TCB_CAP + i, output);
    }

    /* First, we suspend all the debugeee PDs*/
    suspend_system();

    /* Set up sDDF ring buffers */
    serial_queue_init(&rx_queue_handle, config.rx.queue.vaddr, config.rx.data.size, config.rx.data.vaddr);
    serial_queue_init(&tx_queue_handle, config.tx.queue.vaddr, config.tx.data.size, config.tx.data.vaddr);

    serial_putchar_init(config.tx.id, &tx_queue_handle);

    microkit_dbg_puts("Awaiting GDB connection...");

    /* Make a coroutine for the rest of the initialization */
    t_event = co_active();
    t_main = co_derive((void *) t_main_stack, STACK_SIZE, event_loop);

    co_switch(t_main);
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

    // @alwin: I'm not entirely convinced there is a point having reply_mr here still
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
