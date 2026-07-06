/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>
#include <sel4/sel4_arch/types.h>
#include <gdb.h>
#include "gdbcomp.h"
#include <util.h>
#include <stddef.h>
#include <sddf/serial/config.h>
#include <sddf/serial/queue.h>
#include <stdbool.h>
#include <stdint.h>
__attribute__((__section__(".serial_client_config"))) serial_client_config_t config;

serial_queue_t *rx_queue;
serial_queue_t *tx_queue;

serial_queue_handle_t rx_queue_handle;
serial_queue_handle_t tx_queue_handle;

void _putchar(char character) {
    microkit_dbg_putc(character);
}

void gdb_put_char(char c) {
    serial_enqueue(&tx_queue_handle, c);
}

void gdb_flush() {
    sddf_notify(config.tx.id);
}

int gdb_get_char(char* c) {
    return serial_dequeue(&rx_queue_handle, c);
}

void init() {
    assert(serial_config_check_magic(&config));

    /* Set up sDDF ring buffers */
    serial_queue_init(&rx_queue_handle, config.rx.queue.vaddr, config.rx.data.size, config.rx.data.vaddr);
    serial_queue_init(&tx_queue_handle, config.tx.queue.vaddr, config.tx.data.size, config.tx.data.vaddr);
    gdb_init();
    gdb_start();
}

seL4_Bool fault(microkit_child ch, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo) {
    return gdb_fault(ch, msginfo, reply_msginfo);
}

void notified(microkit_channel ch) {
    gdb_notified();
}
