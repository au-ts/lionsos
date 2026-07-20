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
#include "gdb_interop.h"
#include <sddf/util/printf.h>
#include <printf.h>

// TODO: move num debuggees out of here.
#ifndef NUM_DEBUGEES
#error Please specify NUM_DEBUGEES.
#endif

#define MAX_PACKET_SIZE 1024
#define TIMEOUT_YIELDS 10
#define MAX_RETRANSMIT_ATTEMPTS 5
#define INTERRUPT ((char)0x03)

// Requires:
// a push char function (does not flush)
// flushing
// a get char function (which returns 0 on success and -1 on failure, non-blocking)
extern void debugger_put_char(char c);
extern void debugger_flush();
extern int debugger_get_char(char* c);

// The user provides the following mapping regions.
// The small mapping region must be of page_size 0x1000
// THe large mapping region must be of page_size 0x200000
uintptr_t small_mapping_mr = 0;
uintptr_t large_mapping_mr = 0;

void debugger_ack_transmission();
void debugger_nack_transmission();
void debugger_put_transmission(const char* transmission);
void debugger_event_loop();
void debugger_init();
void debugger_start();
seL4_Bool debugger_fault(microkit_child ch, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo);
void debugger_notified();
void debugger_ack_transmission();
char* debugger_try_get_packet();
char* debugger_retry_get_transmission();
char* debugger_get_transmission();

static void debugger_suspend();

static bool initialised = false;
static bool detached = false;

/* Input buffer */
static char input[MAX_PACKET_SIZE];

/* Output buffer */
static char output[MAX_PACKET_SIZE];

#define STACK_SIZE 8192
static char t_main_stack[STACK_SIZE];

cothread_t t_suspended = NULL;
cothread_t t_main = NULL;
cothread_t t_notified = NULL;

static bool debugger_fromNotification = false;

static void put_str(char* c)
{
    while (*c != '\0')
    {
        debugger_put_char(*(c++));
    }
}

static char get_char_or_suspend()
{
    char c;
    while (debugger_get_char(&c) != 0)
    {
        debugger_suspend();
    }
    return c;
}

static bool try_get_char(char* c, size_t maxAttempts) {
    size_t curAttempt = 0;
    while (debugger_get_char(c) != 0 && curAttempt < maxAttempts)
    {
        curAttempt++;
        debugger_suspend();
    }

    return curAttempt <= maxAttempts;
}

static void debugger_suspend() {
    if (debugger_fromNotification)
    {
        debugger_fromNotification = false;
        co_switch(t_notified);
    } else {
        co_switch(t_suspended);
    }
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

char* debugger_try_get_packet()
{
    return debugger_get_transmission();
}

void debugger_ack_transmission() {
    debugger_put_char('+');
    debugger_flush();
}

void debugger_nack_transmission() {
    debugger_put_char('-');
    debugger_flush();
}

char* debugger_retry_get_transmission()
{
    debugger_nack_transmission();
    return debugger_get_transmission();
}

char* debugger_get_transmission() {
    // $[DATA]#XX
    //         [2 Digit hex checksum]
    // Also have to consider interrupts.
    char* in = input;
    int count = 0;
    bool charRes = false;
    // state 1: waiting for $
    do {
        in[0] = get_char_or_suspend();
        // check interrupt
        if (in[0] == INTERRUPT)
        {
            in[1] = 0;
            return in;
        }
    } while (in[0] != '$');
    count += 1;

	// state 2: getting packet data
    int i = 1;
    do {
        charRes = try_get_char(&in[i], TIMEOUT_YIELDS);
        if (charRes) count++;
    } while (charRes && in[i++] != '#');

    // state 3: getting checksum
    for (size_t j = i; j < i + 2; j++)
    {
        charRes = try_get_char(&in[j], TIMEOUT_YIELDS);
        if (charRes) count++;
        else break;
    }
    // null terminate
    in[count] = '\0';
    DEBUGGER_LOG("Got transmission: '%s' of length %d\n", in, count);
    return in;
}

debugger_packet_t debugger_verify_transmission(char* transmission) {
    char* head = transmission;
    debugger_packet_t packet = {
        .valid = false,
        .data = "INVALID!",
        .size = 0,
        .cksum = 0,
        .tcksum = 0,
    };
    // check for interrupt
    if (*head == INTERRUPT)
    {
        packet.valid = true;
        packet.size = 1;
        packet.data = transmission;
        goto debugger_verify_transmission_ret;
    }
    // Check that the begin packet part exists.
    if (*head != '$') goto debugger_verify_transmission_ret;
    head++;
    // Do not support sequence-id from gdb version 5.0 or less.
    while (head[packet.size] != '#' && packet.size < MAX_PACKET_SIZE)
    {
        packet.cksum += head[packet.size];
        packet.size++;
    };
    if (packet.size == MAX_PACKET_SIZE) goto debugger_verify_transmission_ret;
    packet.data = head;
    head += packet.size;

	// Move past the # to the 2 digit checksum
	*(head++) = '\0';
	packet.tcksum += hexchar_to_int(head[0]) << 4;
	packet.tcksum += hexchar_to_int(head[1]);
	packet.valid = (packet.tcksum == packet.cksum);

debugger_verify_transmission_ret:
    DEBUGGER_LOG("Valid packet: %s, interrupt: %s\n", 
        packet.valid ? "true" : "false", 
        (*packet.data == INTERRUPT)? "true" : "false"
    );
	return packet;
}

bool debugger_check_transmission_success() {
    // Yield to quickly process stuff.
    char c;
    if (!try_get_char(&c, TIMEOUT_YIELDS)) return false;
    return c == '+';
}

static char output_buf[MAX_PACKET_SIZE] = {};
void debugger_put_transmission(const char* transmission)
{
    size_t i = 0;
    const char* cstar = transmission;
    uint8_t cksum = 0;
    // Signal beginning of packet
    output_buf[i++] = '$';

    // dump packet contents
    while (*cstar != '\0' && i < 1023)
    {
        output_buf[i++] = *cstar;
        cksum += *cstar;
        cstar++;
    }
    if (i >= 1023) 
    {
        DEBUGGER_LOG("Packet size is too large!\n");
        return;
    }
    // Signal beginning of cksum
    output_buf[i++] = '#';
    output_buf[i++] = int_to_hexchar((cksum >> 4) & 0x0f);
    output_buf[i++] = int_to_hexchar(cksum & 0x0f);
    output_buf[i] = 0;
    DEBUGGER_LOG("Transmitting: '%s'\n", output_buf);
    put_str(output_buf);
}

void debugger_event_loop() {
    bool resume = false;
    /* The event loop runs perpetually if we are in the standard event loop phase */
    while (true) {
        DEBUGGER_LOG("Awaiting transmission\n");
        char* transmission = debugger_get_transmission();
        if (*transmission == INTERRUPT)
        {
            suspend_system();
            detached = false;
        }
        debugger_packet_t res = debugger_verify_transmission(transmission);

        if (res.valid) debugger_ack_transmission();
        else {
            debugger_nack_transmission(); 
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
                debugger_put_transmission(output);
                debugger_flush();
                seL4_Yield();
                attempts++;
                // Give up after 5 attempts
            } while (!debugger_check_transmission_success() && attempts < 5);

            if (attempts == 5)
            {
                DEBUGGER_LOG("Transmission not accepted after 5 attempts!\n");
                continue;
            }
            else
                DEBUGGER_LOG("Transmission accepted!\n");
        }

        if (resume) {
            resume_system();
        }
    }
}

void debugger_init() {
    DEBUGGER_LOG("Initialising debugger...\n");

    if ((void*)small_mapping_mr == NULL)
        DEBUGGER_ERR("small_mapping_mr has not been given a memory region! small_mapping_mr : %p\n", (void*)small_mapping_mr);
    if ((void*)large_mapping_mr == NULL)
        DEBUGGER_ERR("large_mapping_mr has not been given a memory region! large_mapping_mr : %p\n", (void*)large_mapping_mr);

    /* Register all of the inferiors  */
    for (int i = 0; i < NUM_DEBUGEES; i++) {
        gdb_register_inferior(i, BASE_VSPACE_CAP + i);
        gdb_register_thread(i, 0, BASE_TCB_CAP + i, output);
    }
    // Suspends all children.
    suspend_system();
    // use mapping regions to be able to perform page mapping accesses as the parent.
    libvspace_init_mapping_regions(small_mapping_mr, large_mapping_mr);
    t_suspended = co_active();
    t_main = co_derive((void *) t_main_stack, STACK_SIZE, debugger_event_loop);
    t_notified = 0;
    DEBUGGER_LOG("Initialisation complete!\n");
    initialised = true;
}

void debugger_start() {
    if (!initialised)
    {
        DEBUGGER_ERR("debugger_start() called without debugger_init()!\n");
    }
    co_switch(t_main);
}

seL4_Bool debugger_fault(microkit_child ch, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    DEBUGGER_LOG("Faulted!\n");
    suspend_system();
    seL4_Word reply_mr = 0;

    // @alwin: I'm not entirely convinced there is a point having reply_mr here still
    bool have_reply = false;
    DebuggerError err = gdb_handle_fault(ch, 0, microkit_msginfo_get_label(msginfo), &reply_mr, output, &have_reply);
    if (err) {
        DEBUGGER_LOG("Internal assertion failed. Could not find faulting thread");
    }

    int attempts = 0;
    do {
        debugger_put_transmission(output);
        debugger_flush();
        seL4_Yield();
        attempts++;
        // Give up after 5 attempts
    } while (!debugger_check_transmission_success() && attempts < 5);
    if (have_reply)
    {
        *reply_msginfo = microkit_msginfo_new(0, 0);
        return true;
    }
    return false;
}

void debugger_notified() {
    debugger_fromNotification = true;

    // If we are switching to the main event loop from a notification, when
    // we are waiting for more input we need to switch back out, hence jumping to
    // the notification thread.
    t_notified = co_active();
    co_switch(t_main);
}
