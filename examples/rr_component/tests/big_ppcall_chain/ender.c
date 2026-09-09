/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "generic.h"
#include "microkit.h"
seL4_Word prev_caller = UNSET_VALUE;

void init()
{
    LOG("Init!\n");
    assert(prev_caller != UNSET_VALUE);
}

void notified(microkit_channel ch) {
    assert(!"unreachable");
}

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    assert(ch == prev_caller);
    seL4_Word label = microkit_msginfo_get_label(msginfo);
    LOG("ENDER UP - label %lu\n", label);
    seL4_Word length = microkit_msginfo_get_count(msginfo);
    LOG("Contents: ");
    for (int i = 0; i < length; i++) {
        sddf_printf("%c", (char) microkit_mr_get(i));
    }
    sddf_printf("\n");

    msginfo = microkit_msginfo_new(final_label, sizeof(ender_msg));
    for (int i = 0; i < sizeof(ender_msg); i++) {
        microkit_mr_set(i, ender_msg[i]);
    }
    LOG("ENDER DOWN - label %lx msg %s\n", final_label, ender_msg);
    return msginfo;
}
