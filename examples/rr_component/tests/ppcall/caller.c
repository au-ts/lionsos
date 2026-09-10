/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <microkit.h>
#include <sddf/util/printf.h>
#define LOG(...) do {sddf_printf("CALLER | " __VA_ARGS__);} while (0)
#define microkit_notify(ch) do {LOG("send\n"); microkit_notify(ch); } while (0)
uintptr_t calleech = 9999;
const char balls[] = "balls";
char resbuffer[0xff] = { 0 };

void init()
{
    LOG("Init!\n");
    microkit_msginfo msg = microkit_msginfo_new(0xdeadbeef, sizeof(balls));
    for (int i = 0; i < sizeof(balls); i++) {
        microkit_mr_set(i, balls[i]);
        assert((char)microkit_mr_get(i) == balls[i]);
    }
    LOG("Calling!\n");
    microkit_msginfo result = microkit_ppcall(calleech, msg);

    LOG("RESULT | Channel: %lu, label: %lx, length: %lu\n", calleech, microkit_msginfo_get_label(result),
        microkit_msginfo_get_count(result));

    for (int i = 0; i < microkit_msginfo_get_count(result); i++) {
        resbuffer[i] = (char)microkit_mr_get(i);
    }

    LOG("RESULT | Message contents: %s\n", resbuffer);
}

void notified(microkit_channel ch)
{
    LOG("Notified! %d\n", ch);
}
