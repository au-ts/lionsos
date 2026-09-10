/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <microkit.h>
#include <sddf/util/printf.h>
#define LOG(...) sddf_printf("CALLEE | " __VA_ARGS__)
#define microkit_notify(ch) do {LOG("send\n"); microkit_notify(ch); } while (0)
#define INPUT_CAP 1
#define REPLY_CAP 4
const char funny[] = "funny guy";
char resbuffer[0xff] = { 0 };

void init()
{
    LOG("Init!\n");
}

uintptr_t callerch = 9999;

void notified(microkit_channel ch)
{
    LOG("Notified! %d\n", ch);
}

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    LOG("Protected!\n");
    LOG("RECEIVED | Channel: %u, label: %lx, length: %lu\n", ch, microkit_msginfo_get_label(msginfo),
        microkit_msginfo_get_count(msginfo));

    for (int i = 0; i < microkit_msginfo_get_count(msginfo); i++) {
        resbuffer[i] = (char)microkit_mr_get(i);
    }

    LOG("RECEIVED | Message contents: %s\n", resbuffer);

    microkit_msginfo res = microkit_msginfo_new(0xbeefdeaf, sizeof(funny));
    for (int i = 0; i < sizeof(funny); i++) {
        microkit_mr_set(i, funny[i]);
    }

    return res;
}
