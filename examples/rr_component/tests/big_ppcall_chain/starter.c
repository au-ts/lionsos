/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "generic.h"
#include "microkit.h"

seL4_Word next_caller = UNSET_VALUE;

void init()
{
    LOG("init\n");
    assert(next_caller != UNSET_VALUE);

    microkit_msginfo to_send = microkit_msginfo_new(1ul, sizeof(starter_msg));
    for (int i = 0; i < sizeof(starter_msg); i++) {
        microkit_mr_set(i, starter_msg[i]);
    }
    LOG("STARTER UP - label 1 '%s'\n", starter_msg);
    microkit_msginfo recv = microkit_ppcall(next_caller, to_send);

    // Add one to the label and continue calling.
    seL4_Word label = microkit_msginfo_get_label(recv);
    LOG("LAST DOWN - label %lu\n", label);
    seL4_Word length = microkit_msginfo_get_count(recv);
    LOG("Contents: ");
    for (int i = 0; i < length; i++) {
        sddf_printf("%c", (char) microkit_mr_get(i));
    }
    sddf_printf("\n");
}

void notified(microkit_channel ch) {
    assert(!"unreachable");
}
