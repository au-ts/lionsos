/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sel4/sel4.h>
#include <microkit.h>
#include <sddf/util/printf.h>
#include "rrer.h"

#define microkit_notify(ch) do {LOG("send %d\n", ch); microkit_notify(ch); } while (0)

// We never exit this.
void init()
{
    LOG("INIT\n");
    rrer_init();
    rrer_main();
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
