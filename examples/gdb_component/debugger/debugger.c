/*
 * Copyright 2024, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>
#include <sel4/sel4_arch/types.h>
#include <gdb.h>
#include <util.h>
#include <stddef.h>
#include <sddf/serial/config.h>
#include <sddf/serial/queue.h>
#include <sddf/util/printf.h>
#include <sddf/serial/config.h>
#include <vspace.h>

__attribute__((__section__(".serial_client_config"))) serial_client_config_t config;

typedef enum event_state {
    eventState_none = 0,
    eventState_waitingForInputEventLoop,
    eventState_waitingForInputFault
} event_state_t;

// TODO - DO NOT DEFINE THIS IN MULTIPLE PLACES
#define NUM_DEBUGEES 2

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

uint32_t gdb_read_word(uint16_t client, uintptr_t addr, char *val)
{
    libvspace_read_word(client, addr, val);
}

uint32_t gdb_write_word(uint16_t client, uintptr_t addr, seL4_Word val)
{
    libvspace_write_word(client, addr, val);
}

uint32_t gdb_read_bytes(uint16_t client, uintptr_t start_addr, char *buff, uint64_t nbytes)
{
    libvspace_read_bytes(client, start_addr, buff, nbytes);
}

uint32_t gdb_write_bytes(uint16_t client, uintptr_t start_addr, char *buff, uint64_t nbytes)
{
    libvspace_write_bytes(client, start_addr, buff, nbytes);
}

void _putchar(char character) {
    microkit_dbg_putc(character);
}

void put_char(char c) {
    sddf_putchar_unbuffered(c);
}


char get_char()
{
    while (serial_queue_empty(&rx_queue_handle, rx_queue_handle.queue->head)) {
        seL4_Yield();
    }

    char c;
    serial_dequeue(&rx_queue_handle, &c);

    return c;
}


char* get_transmission() {
    // TODO: consider timeouts for transmission?

    // $packet-data#checksum
    // checksum is 2 digits hex.
    char* in = input;
    int count = 0;
    // state 1: waiting for $
    do {
        in[0] = get_char();
    } while (in[0] != '$');
    count += 1;

	// state 2: getting packet data
    int i = 1;
    do {
        in[i] = get_char();
        count++;
    } while (in[i++] != '#');

    // state 3: getting checksum
    for (size_t j = i; j < i + 2; j++)
    {
        in[j] = get_char();
        count++;
    }
    // null terminate
    in[count] = '\0';
    microkit_dbg_puts("Got transmission: ");
    microkit_dbg_puts(in);
    microkit_dbg_puts("\n");
    return in;
}

char* try_get_packet()
{
    return get_transmission();
}

void ack_transmission() {
    put_char('+');
}

void nack_transmission() {
    put_char('-');
}

char* retry_get_transmission()
{
    nack_transmission();
    return get_transmission();
}

bool verify_transmission(const char* transmission) {
    return true;
}

void put_transmission(const char* transmission)
{
    const char* cstar = transmission;
    while (*cstar != '\0')
        put_char(*cstar++);
}

bool check_transmission() {
    // Yield to quickly process stuff.
    seL4_Yield();
    seL4_Yield();
    char c = get_char();
    return c == '+';
}

static void event_loop() {
    bool resume = false;
    /* The event loop runs perpetually if we are in the standard event loop phase */
    while (true) {
        char* transmission = get_transmission();
        if (!resume || detached) {
            // put_packet(output, eventState_waitingForInputEventLoop);
        }

        if (resume) {
            resume_system();
        }
    }
}

void init() {
    microkit_dbg_puts("Initialising debugger...\n");
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

    microkit_dbg_puts("Awaiting GDB connection...\n");

    microkit_dbg_puts("Debugger initialiser complete\n");
    event_loop();
}


void fault_message() {
    // put_packet(output, eventState_waitingForInputFault);
    // Go back to waiting for normal input after we send the fault packet to the host
    state = eventState_waitingForInputEventLoop;
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

    fault_message();

    if (have_reply) {
        *reply_msginfo = microkit_msginfo_new(0, 0);
        return true;
    }

    return false;
}

void notified(microkit_channel ch) {
    if (state == eventState_waitingForInputFault) {
        state = eventState_none;
    }


    /* This is not an else if because we want to switch to the event loop after
       handling the fault message. We could probably do this unconditionally?  */
    if (state == eventState_waitingForInputEventLoop) {
        state = eventState_none;
    }
}
