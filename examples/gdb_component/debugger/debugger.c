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
#include <vspace.h>
#include <stdbool.h>
#include <stdint.h>
#include <libco.h>

// The user provides the following mapping regions.
// The small mapping region must be of page_size 0x1000
// THe large mapping region must be of page_size 0x200000
uintptr_t small_mapping_mr;
uintptr_t large_mapping_mr;

__attribute__((__section__(".serial_client_config"))) serial_client_config_t config;

typedef struct {
    bool valid;
    char* data;
    seL4_Word size;
    uint8_t cksum; // calculated checksum
    uint8_t tcksum; // transmitted checksum
} gdb_packet_t;

// TODO - DO NOT DEFINE THIS IN MULTIPLE PLACES
#define NUM_DEBUGEES 1

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

#define STACK_SIZE 4096
static char t_main_stack[STACK_SIZE];

cothread_t t_suspended, t_main;

/* The current event state and phase */
static bool detached = false;

uint32_t gdb_read_word(uint16_t client, uintptr_t addr, char *val)
{
    return libvspace_read_word(client, addr, val);
}

uint32_t gdb_write_word(uint16_t client, uintptr_t addr, seL4_Word val)
{
    return libvspace_write_word(client, addr, val);
}

uint32_t gdb_read_bytes(uint16_t client, uintptr_t start_addr, char *buff, uint64_t nbytes)
{
    return libvspace_read_bytes(client, start_addr, buff, nbytes);
}

uint32_t gdb_write_bytes(uint16_t client, uintptr_t start_addr, char *buff, uint64_t nbytes)
{
    return libvspace_write_bytes(client, start_addr, buff, nbytes);
}

void _putchar(char character) {
    microkit_dbg_putc(character);
}

void put_char(char c) {
    serial_enqueue_batch(&tx_queue_handle, 1, &c);
}


void put_str(const char* chars, size_t len) {
    serial_enqueue_batch(&tx_queue_handle, len, chars);
}

void flush()
{
    sddf_notify(config.tx.id);
}

char get_char()
{
    while (serial_queue_empty(&rx_queue_handle, rx_queue_handle.queue->head)) {
        // Switch to a suspended state
        co_switch(t_suspended);
    }

    char c;
    serial_dequeue(&rx_queue_handle, &c);

    return c;
}

bool get_char_yield_timeout(int numYields, char c[1])
{
    int attempts = 0;
    while (serial_dequeue(&rx_queue_handle, c) != 0 && attempts < numYields)
        seL4_Yield();
    if (attempts >= numYields) return false;
    return true;
}

char* get_transmission() {
    static const int TIMEOUT = 100;

    // $packet-data#checksum
    // checksum is 2 digits hex.
    char* in = input;
    int count = 0;
    bool charRes = false;
    // state 1: waiting for $
    do {
        in[0] = get_char();
    } while (in[0] != '$');
    count += 1;

	// state 2: getting packet data
    int i = 1;
    do {
        charRes = get_char_yield_timeout(TIMEOUT, &in[i]);
        if (charRes) count++;
    } while (charRes && in[i++] != '#');

    // state 3: getting checksum
    for (size_t j = i; j < i + 2; j++)
    {
        charRes = get_char_yield_timeout(TIMEOUT, &in[j]);
        if (charRes) count++;
        else break;
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
    flush();
}

void nack_transmission() {
    put_char('-');
    flush();
}

char* retry_get_transmission()
{
    nack_transmission();
    return get_transmission();
}

#define MAX_PACKET_SIZE 800

gdb_packet_t verify_transmission(char* transmission) {
    char* head = transmission;
    gdb_packet_t packet = {
        .valid = false,
        .data = NULL,
        .size = 0,
        .cksum = 0,
        .tcksum = 0,
    };
    // Check that the begin packet part exists.
    if (*head != '$') goto verify_transmission_ret;
    head++;
    // Do not support sequence-id from gdb version 5.0 or less.
    while (head[packet.size] != '#' && packet.size < MAX_PACKET_SIZE)
    {
        packet.cksum += head[packet.size];
        packet.size++;
    };
    if (packet.size == MAX_PACKET_SIZE) goto verify_transmission_ret;
    packet.data = head;
    head += packet.size;

	// Move past the # to the 2 digit checksum
	*(head++) = '\0';
	packet.tcksum += hexchar_to_int(head[0]) << 4;
	packet.tcksum += hexchar_to_int(head[1]);
	packet.valid = (packet.tcksum == packet.cksum);

verify_transmission_ret:

    microkit_dbg_puts("Valid packet: ");
    microkit_dbg_puts(packet.valid ? "true\n" : "false\n");
	return packet;
}

void put_transmission(const char* transmission)
{
    char outputbuf[1024] = {};
    size_t i = 0;
    const char* cstar = transmission;
    uint8_t cksum = 0;
    // Signal beginning of packet
    outputbuf[i++] = '$';

    // dump packet contents
    while (*cstar != '\0' && i < 1023)
    {
        outputbuf[i++] = *cstar;
        cksum += *cstar;
        cstar++;
    }
    if (i >= 1023) 
    {
        microkit_dbg_puts("Packet size is too large!\n");
        return;
    }
    // Signal beginning of cksum
    outputbuf[i++] = '#';
    outputbuf[i++] = int_to_hexchar((cksum >> 4) & 0x0f);
    outputbuf[i++] = int_to_hexchar(cksum & 0x0f);
    outputbuf[i] = 0;
    microkit_dbg_puts("Transmitting: '");
    microkit_dbg_puts(outputbuf);
    microkit_dbg_puts("'\n");
    put_str(outputbuf, i);
}

#define TIMEOUT_YIELDS 100
bool check_transmission() {
    // Yield to quickly process stuff.
    for (size_t i = 0; i < TIMEOUT_YIELDS; i++)
        seL4_Yield();
    char c = 0;
    serial_dequeue(&rx_queue_handle, &c);
    return c == '+';
}

static void event_loop() {
    bool resume = false;
    /* The event loop runs perpetually if we are in the standard event loop phase */
    while (true) {
        char* transmission = get_transmission();
        gdb_packet_t res = verify_transmission(transmission);

        if (res.valid) ack_transmission();
        else {
            nack_transmission(); 
            continue;
        }

        if (detached || res.data[0] == 3)
        {
            suspend_system();
            detached = false;
        }

        resume = gdb_handle_packet(res.data, output, &detached);
        if (!resume || detached)
        {
            int attempts = 0;
            do {
                put_transmission(output);
                flush();
                seL4_Yield();
                attempts++;
                // Give up after 5 attempts
            } while (!check_transmission() && attempts < 5);

            if (attempts == 5)
            {
                microkit_dbg_puts("Transmission not accepted after 5 attempts!\n");
                continue;
            }
            else
                microkit_dbg_puts("Transmission accepted!\n");
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

    libvspace_init_mapping_regions(small_mapping_mr, large_mapping_mr);

    microkit_dbg_puts("Debugger initialiser complete\n");
    microkit_dbg_puts("Awaiting GDB connection...\n");
    t_suspended = co_active();
    t_main = co_derive((void *) t_main_stack, STACK_SIZE, event_loop);

    co_switch(t_main);
}


seL4_Bool fault(microkit_child ch, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo) {
    microkit_dbg_puts("Faulted!\n");
    seL4_Word reply_mr = 0;

    suspend_system();

    // @alwin: I'm not entirely convinced there is a point having reply_mr here still
    bool have_reply;
    DebuggerError err = gdb_handle_fault(ch, 0, microkit_msginfo_get_label(msginfo), &reply_mr, output, &have_reply);
    if (err) {
        microkit_dbg_puts("GDB: Internal assertion failed. Could not find faulting thread");
    }

    int attempts = 0;
    do {
        put_transmission(output);
        flush();
        seL4_Yield();
        attempts++;
        // Give up after 5 attempts
    } while (!check_transmission() && attempts < 5);

    if (have_reply) {
        *reply_msginfo = microkit_msginfo_new(0, 0);
        return true;
    }

    return false;
}

void notified(microkit_channel ch) {
    co_switch(t_main);
}
