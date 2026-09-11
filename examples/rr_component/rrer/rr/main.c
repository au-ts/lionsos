/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sel4/sel4.h>
#include <microkit.h>
#include <sddf/util/printf.h>
#include <sddf/serial/config.h>
#include <sddf/serial/queue.h>
#include "recorder.h"

#define microkit_notify(ch) do {LOG("send %d\n", ch); microkit_notify(ch); } while (0)
__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;

serial_queue_t *tx_queue;

serial_queue_handle_t serial_tx_queue_handle;

// We never exit this.
void init()
{
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size,
                  serial_config.tx.data.vaddr);
    serial_putchar_init(serial_config.tx.id, &serial_tx_queue_handle);
    LOG("INIT\n");

    rec_init();
    rec_main();
}

// Should not be called
void notified(microkit_channel ch)
{
    LOG("Notified! %d\n", ch);
}

// Should not be called
microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    LOG("Protected!\n");
    return msginfo;
}

// Should not be called.
seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    *reply_msginfo = microkit_msginfo_new(0, 0);
    return false;
}
