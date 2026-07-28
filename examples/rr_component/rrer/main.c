/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "sel4/simple_types.h"
#include <microkit.h>
#include <sddf/util/printf.h>
#include <vspace.h>
#include <gdb.h>
#include <pmu.h>


// stuff required for libgdb.
char gdb_output_buf[1024] = {0};
// stuff required for libvspace
uintptr_t small_mapping_mr = 0;
uintptr_t large_mapping_mr = 0;

#define LOG(...) sddf_printf("RRER | " __VA_ARGS__)

// Move this outta here.
#define NUM_DEBUGEES 2

#define INST_SIZE     0x4
typedef uint32_t rrer_inst_t;

// Debug
// For aarch64
// ret = (seL4_Word) AARCH64_BREAK_KGDB_DYN_DBG | (0xFF_FF_FF_FF_00_00_00_00 & ret);
// 
// err = gdb_write_word(inferior->id, address, ret);

// For now.
uintptr_t pingch = 9999;
uintptr_t pongch = 9999;

// do i expose some sort of serial api to allow things to stop / finish recording
void init()
{
	assert(pingch != 9999 && "pingch needs to be set");
	assert(pongch != 9999 && "pongch needs to be set");

    assert(small_mapping_mr && "small_mapping_mr has to be set!");
    assert(large_mapping_mr && "large_mapping_mr has to be set!");
    for (int i = 0; i < NUM_DEBUGEES; i++) {
        gdb_register_inferior(i, BASE_VSPACE_CAP + i);
        gdb_register_thread(i, 0, BASE_TCB_CAP + i, gdb_output_buf);
    }

    libvspace_init_mapping_regions(small_mapping_mr, large_mapping_mr);
    // Stop all child threads.
    suspend_system();
    LOG("Initialised\n");
    // now we must have fun with pmu binding.
}

// Store notifications into array.
// Forward the notification it receives
void notified(microkit_channel ch)
{
    if (ch == pingch) {
        // Forward the notification
        microkit_notify(pongch);
    } else if (ch == pongch) {
        microkit_notify(pingch);
    }
    LOG("Notified! %d\n", ch);
}

// shouldn't get called.
microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    LOG("Protected!\n");
    return msginfo;
}

// We only enter here whenever we hit a breakpoint, unless a crash happens.
seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo) {
    return false;
}


// required to fulfill for libgdb.
void _putchar(char character) {
    microkit_dbg_putc(character);
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
