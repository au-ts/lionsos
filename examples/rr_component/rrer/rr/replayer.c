/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "base.h"
#include "init.h"
#include "interfaces/sel4_client.h"
#include "microkit.h"
#include "scheduler.h"
#include "ipc.h"
#include "sel4/shared_types_gen.h"
#include "sel4/simple_types.h"
#include "storage_backend/base.h"
#include "storage_backend/block.h"
#include "types.h"
#include "fault.h"
#include <sddf/util/printf.h>
#include <sddf/serial/config.h>
#include <sddf/serial/queue.h>

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;

serial_queue_t *tx_queue;

serial_queue_handle_t serial_tx_queue_handle;

void init()
{
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size,
              serial_config.tx.data.vaddr);
    INFO("INIT\n");
    rr_init(false);
    INFO("Init complete!\n");
    INFO("Instructions:\n");
    // Now we should begin running and doing some expecting things using the instructions we got.
    for (int i = 0; i < _rr_storage_handle.metadata.inst_num; i++) {
        rr_storage_unit_t inst = rr_storage_get_next_inst(&_rr_storage_handle);
        rr_storage_unit_print(&inst);
    }
}

void notified(microkit_channel ch)
{
    INFO("Notified! %d\n", ch);
}

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    INFO("Protected!\n");
    return msginfo;
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    *reply_msginfo = microkit_msginfo_new(0, 0);
    return false;
}
