/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "generic.h"
#include "microkit.h"
#include "sel4/shared_types_gen.h"
seL4_Word next_caller = UNSET_VALUE;
seL4_Word prev_caller = UNSET_VALUE;

void init()
{
    LOG("init\n");
    assert(next_caller != UNSET_VALUE);
    assert(prev_caller != UNSET_VALUE);
}

void notified(microkit_channel ch)
{
    assert(!"unreachable");
}

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    assert(ch == prev_caller);
    seL4_Word label = microkit_msginfo_get_label(msginfo);
    LOG("Protected UP - label %lu\n", label);
    seL4_Word length = microkit_msginfo_get_count(msginfo);
    LOG("Contents: ");
    for (int i = 0; i < length; i++) {
        sddf_printf("%c", (char)microkit_mr_get(i));
    }
    sddf_printf("\n");

    // Add one to the label and continue calling.
    msginfo = seL4_MessageInfo_set_label(msginfo, label + 1);
    microkit_msginfo recv = microkit_ppcall(next_caller, msginfo);
    label = microkit_msginfo_get_label(recv);
    LOG("Protected DOWN - label %lu\n", label);
    length = microkit_msginfo_get_count(recv);
    LOG("Contents: ");
    for (int i = 0; i < length; i++) {
        sddf_printf("%c", (char)microkit_mr_get(i));
    }
    sddf_printf("\n");

    // now just return what we received
    return recv;
}
