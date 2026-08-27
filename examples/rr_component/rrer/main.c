/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sel4/sel4.h>
#include <microkit.h>
#include <sddf/util/printf.h>

#define LOG(...) sddf_printf("RRER | " __VA_ARGS__)
#define microkit_notify(ch) do {LOG("send %d\n", ch); microkit_notify(ch); } while (0)

typedef struct {
    seL4_Word id;
    seL4_Word priority;
} rr_Child_t;

// The index is the id
rr_Child_t* children_arr = NULL;
seL4_Word children_num = 0;

void init()
{
    LOG("INIT\n");
}

static inline microkit_channel get_target_ch(microkit_channel ch) {
    // if odd, then - 1, if even then + 1
    if (ch % 2 == 0)
        return ch + 1;
    return ch - 1;
}

// Store notifications into array.
// Forward the notification it receives
void notified(microkit_channel ch)
{
    LOG("Notified! %d\n", ch);
    // find the original target
    microkit_notify(get_target_ch(ch));
}

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    LOG("Protected!\n");
    return msginfo;
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo) {
}
