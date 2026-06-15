#pragma once
/*
 * Copyright 2021, Breakaway Consulting Pty. Ltd.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <sel4/sel4.h>
#include <stdint.h>

static void putc(uint8_t ch)
{
#if defined(CONFIG_PRINTING)
    seL4_DebugPutChar(ch);
#endif
}

static void puts(const char *s)
{
    while (*s) {
        putc(*s);
        s++;
    }
}

static char hexchar(unsigned int v)
{
    return v < 10 ? '0' + v : ('a' - 10) + v;
}

static void puthex32(uint32_t val)
{
    char buffer[8 + 3];
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[8 + 3 - 1] = 0;
    for (unsigned i = 8 + 1; i > 1; i--) {
        buffer[i] = hexchar(val & 0xf);
        val >>= 4;
    }
    puts(buffer);
}

static void puthex64(uint64_t val)
{
    char buffer[16 + 3];
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[16 + 3 - 1] = 0;
    for (unsigned i = 16 + 1; i > 1; i--) {
        buffer[i] = hexchar(val & 0xf);
        val >>= 4;
    }
    puts(buffer);
}

static void fail(char *s)
{
    puts("FAIL: ");
    puts(s);
    puts("\n");
    for (;;) {}
}

static char *sel4_strerror(seL4_Word err)
{
    switch (err) {
    case seL4_NoError:
        return "seL4_NoError";
    case seL4_InvalidArgument:
        return "seL4_InvalidArgument";
    case seL4_InvalidCapability:
        return "seL4_InvalidCapability";
    case seL4_IllegalOperation:
        return "seL4_IllegalOperation";
    case seL4_RangeError:
        return "seL4_RangeError";
    case seL4_AlignmentError:
        return "seL4_AlignmentError";
    case seL4_FailedLookup:
        return "seL4_FailedLookup";
    case seL4_TruncatedMessage:
        return "seL4_TruncatedMessage";
    case seL4_DeleteFirst:
        return "seL4_DeleteFirst";
    case seL4_RevokeFirst:
        return "seL4_RevokeFirst";
    case seL4_NotEnoughMemory:
        return "seL4_NotEnoughMemory";
    }

    return "<invalid seL4 error>";
}
