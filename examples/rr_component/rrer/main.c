/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <microkit.h>
#include <sddf/util/printf.h>
#include <vspace.h>
#include <gdb.h>
#define LOG(...) sddf_printf("RRER | " __VA_ARGS__)
#define NUM_DEBUGEES 2
char gdb_output_buf[1024] = {0};
// For now we set it so a fault will trigger the replaying.

// Fuck the setvarid is not capable enough unless i make the python generate c code.
// I need to be able to generate endpoint pairs for notifications.
uintptr_t pingch = 9999;
uintptr_t pongch = 9999;

uintptr_t small_mapping_mr = 0;
uintptr_t large_mapping_mr = 0;

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
    LOG("Initialised\n");
    suspend_system();
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

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    LOG("Protected!\n");
    return msginfo;
}
