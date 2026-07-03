/*
 * Copyright 2024, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// The main driver component of a debugger PD.
// Holds stuff for processing gdb packets and interacts with libgdb.

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
#include "gdbcomp.h"
#include <sddf/util/printf.h>
#include <printf.h>

#define MAX_PACKET_SIZE 1024
#define TIMEOUT_YIELDS 100
#define NUM_DEBUGEES 3

// Requires:
// a push char function (does not flush)
// flushing
// a get char function (which returns 0 on success and -1 on failure, non-blocking)

extern void gdb_put_char(char c);
extern void gdb_flush();
extern int gdb_get_char(char* c);

// The user provides the following mapping regions.
// The small mapping region must be of page_size 0x1000
// THe large mapping region must be of page_size 0x200000
uintptr_t small_mapping_mr;
uintptr_t large_mapping_mr;

void gdb_ack_transmission() ;
void gdb_nack_transmission() ;
void gdb_put_transmission(const char* transmission);
void gdb_event_loop() ;
void gdb_init() ;
void gdb_start() ;
void gdb_fault(microkit_child ch, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo);
void gdb_notified() ;
void gdb_ack_transmission() ;
char* gdb_try_get_packet();
char* gdb_retry_get_transmission();
char* gdb_get_transmission();

static bool initialised = false;
static bool detached = false;

/* Input buffer */
static char input[BUFSIZE];

/* Output buffer */
static char output[BUFSIZE];

#define STACK_SIZE 4096
static char t_main_stack[STACK_SIZE];

cothread_t t_suspended, t_main;

static void put_str(char* c)
{
    while (*c != '\0')
    {
        gdb_put_char(*(c++));
    }
}

static char get_char_or_suspend()
{
    char c;
    while (gdb_get_char(&c) != 0)
    {
        co_switch(t_suspended);
    }
    return c;
}

static bool get_char_or_yield(int num_yields, char *c)
{
    int i = 0;
    while (gdb_get_char(c) != 0 && i++ < num_yields)
    {
        seL4_Yield();
    }
    return num_yields != i;
}

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

char* gdb_try_get_packet()
{
    return gdb_get_transmission();
}

void gdb_ack_transmission() {
    gdb_put_char('+');
    gdb_flush();
}

void gdb_nack_transmission() {
    gdb_put_char('-');
    gdb_flush();
}

char* gdb_retry_get_transmission()
{
    gdb_nack_transmission();
    return gdb_get_transmission();
}

char* gdb_get_transmission() {
    static const int TIMEOUT = 100;

    // $packet-data#checksum
    // checksum is 2 digits hex.
    char* in = input;
    int count = 0;
    bool charRes = false;
    // state 1: waiting for $
    do {
        in[0] = get_char_or_suspend();
    } while (in[0] != '$');
    count += 1;

	// state 2: getting packet data
    int i = 1;
    do {
        charRes = get_char_or_yield(TIMEOUT, &in[i]);
        if (charRes) count++;
    } while (charRes && in[i++] != '#');

    // state 3: getting checksum
    for (size_t j = i; j < i + 2; j++)
    {
        charRes = get_char_or_yield(TIMEOUT, &in[j]);
        if (charRes) count++;
        else break;
    }
    // null terminate
    in[count] = '\0';
    GDB_LOG("Got transmission: '%s' of length %d\n", in, count);
    return in;
}

gdb_packet_t gdb_verify_transmission(char* transmission) {
    char* head = transmission;
    gdb_packet_t packet = {
        .valid = false,
        .data = NULL,
        .size = 0,
        .cksum = 0,
        .tcksum = 0,
    };
    // Check that the begin packet part exists.
    if (*head != '$') goto gdb_verify_transmission_ret;
    head++;
    // Do not support sequence-id from gdb version 5.0 or less.
    while (head[packet.size] != '#' && packet.size < MAX_PACKET_SIZE)
    {
        packet.cksum += head[packet.size];
        packet.size++;
    };
    if (packet.size == MAX_PACKET_SIZE) goto gdb_verify_transmission_ret;
    packet.data = head;
    head += packet.size;

	// Move past the # to the 2 digit checksum
	*(head++) = '\0';
	packet.tcksum += hexchar_to_int(head[0]) << 4;
	packet.tcksum += hexchar_to_int(head[1]);
	packet.valid = (packet.tcksum == packet.cksum);

gdb_verify_transmission_ret:

    GDB_LOG("Valid packet: %s", packet.valid ? "true\n" : "false\n");
	return packet;
}

bool gdb_check_transmission_success() {
    // Yield to quickly process stuff.
    char c;
    if (!get_char_or_yield(TIMEOUT_YIELDS, &c)) return false;
    return c == '+';
}

void gdb_put_transmission(const char* transmission)
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
        GDB_LOG("Packet size is too large!\n");
        return;
    }
    // Signal beginning of cksum
    outputbuf[i++] = '#';
    outputbuf[i++] = int_to_hexchar((cksum >> 4) & 0x0f);
    outputbuf[i++] = int_to_hexchar(cksum & 0x0f);
    outputbuf[i] = 0;
    GDB_LOG("Transmitting: '%s'\n", outputbuf);
    put_str(outputbuf);
}

void gdb_event_loop() {
    bool resume = false;
    /* The event loop runs perpetually if we are in the standard event loop phase */
    while (true) {
        char* transmission = gdb_get_transmission();
        gdb_packet_t res = gdb_verify_transmission(transmission);

        if (res.valid) gdb_ack_transmission();
        else {
            gdb_nack_transmission(); 
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
                gdb_put_transmission(output);
                gdb_flush();
                seL4_Yield();
                attempts++;
                // Give up after 5 attempts
            } while (!gdb_check_transmission_success() && attempts < 5);

            if (attempts == 5)
            {
                GDB_LOG("Transmission not accepted after 5 attempts!\n");
                continue;
            }
            else
                GDB_LOG("Transmission accepted!\n");
        }

        if (resume) {
            resume_system();
        }
    }
}

// Should return a gdb_handle_t or something like that?
void gdb_init() {
    GDB_LOG("Initialising debugger...\n");
    /* Register all of the inferiors  */
    for (int i = 0; i < NUM_DEBUGEES; i++) {
        gdb_register_inferior(i, BASE_VSPACE_CAP + i);
        gdb_register_thread(i, 0, BASE_TCB_CAP + i, output);
    }
    suspend_system();
    libvspace_init_mapping_regions(small_mapping_mr, large_mapping_mr);
    t_suspended = co_active();
    t_main = co_derive((void *) t_main_stack, STACK_SIZE, gdb_event_loop);
    GDB_LOG("Initialisation complete!\n");
    initialised = true;
}

void gdb_start() {
    if (!initialised)
    {
        GDB_LOG("Initialisation not complete!\n");
        *((volatile int*)(NULL));
    }
    co_switch(t_main);
}

void gdb_fault(microkit_child ch, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    GDB_LOG("Faulted!\n");
    suspend_system();
    seL4_Word reply_mr = 0;

    // @alwin: I'm not entirely convinced there is a point having reply_mr here still
    bool have_reply = false;
    DebuggerError err = gdb_handle_fault(ch, 0, microkit_msginfo_get_label(msginfo), &reply_mr, output, &have_reply);
    if (err) {
        GDB_LOG("GDB: Internal assertion failed. Could not find faulting thread");
    }

    int attempts = 0;
    do {
        gdb_put_transmission(output);
        gdb_flush();
        seL4_Yield();
        attempts++;
        // Give up after 5 attempts
    } while (!gdb_check_transmission_success() && attempts < 5);
    // if (have_reply) {
    //     *reply_msginfo = microkit_msginfo_new(0, 0);
    //     return true;
    // }

    // return false;
}

void gdb_notified() {
    co_switch(t_main);
}
