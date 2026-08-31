/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sel4/sel4.h>
#include <microkit.h>
#include <sddf/util/printf.h>
#include "rrer.h"

#define microkit_notify(ch) do {LOG("send %d\n", ch); microkit_notify(ch); } while (0)

typedef struct table_meta_data {
    uint64_t table_data_base;
    uint64_t pgd[64];
} table_metadata_t;

table_metadata_t table_metadata;

seL4_Word* prefill_data = NULL;

// We never exit this.
void init()
{
    LOG("INIT\n");
    rrer_init(prefill_data);
    rrer_main();
}

// Store notifications into array.
// Forward the notification it receives
void notified(microkit_channel ch)
{
    LOG("Notified! %d\n", ch);
}

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    LOG("Protected!\n");
    return msginfo;
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo) {
    *reply_msginfo = microkit_msginfo_new(0, 0);
    return false;
}
