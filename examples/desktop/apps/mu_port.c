/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <sddf/util/printf.h>
#include "mu_port.h"
#include "microui/microui.h"

/* Defined in sDDF's util/printf.c; <sddf/util/printf.h> only provides the sddf_vsnprintf macro */
int sddf_vsnprintf_(char *buffer, size_t count, const char *format, va_list va);

/* microui only formats numbers into buffers of at least MU_MAX_FMT bytes */
int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = sddf_vsnprintf(buf, MU_MAX_FMT, fmt, ap);
    va_end(ap);
    return n;
}

int fprintf(void *stream, const char *fmt, ...)
{
    (void)stream;
    va_list ap;
    va_start(ap, fmt);
    int n = sddf_vprintf(fmt, ap);
    va_end(ap);
    return n;
}

/* Fault the PD, which the Microkit monitor reports */
void abort(void)
{
    __builtin_trap();
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

/* Decimal numbers with an optional sign, fraction and exponent */
double strtod(const char *str, char **end)
{
    const char *p = str;
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    double sign = 1.0;
    if (*p == '-' || *p == '+') {
        sign = *p == '-' ? -1.0 : 1.0;
        p++;
    }

    double value = 0.0;
    bool any = false;
    while (is_digit(*p)) {
        value = value * 10.0 + (*p++ - '0');
        any = true;
    }
    if (*p == '.') {
        p++;
        double scale = 0.1;
        while (is_digit(*p)) {
            value += (*p++ - '0') * scale;
            scale *= 0.1;
            any = true;
        }
    }
    if (!any) {
        if (end) {
            *end = (char *)str;
        }
        return 0.0;
    }

    if (*p == 'e' || *p == 'E') {
        const char *q = p + 1;
        int exp_sign = 1;
        if (*q == '-' || *q == '+') {
            exp_sign = *q == '-' ? -1 : 1;
            q++;
        }
        if (is_digit(*q)) {
            int exp = 0;
            while (is_digit(*q) && exp < 400) {
                exp = exp * 10 + (*q++ - '0');
            }
            while (is_digit(*q)) {
                q++;
            }
            for (int i = 0; i < exp; i++) {
                value = exp_sign > 0 ? value * 10.0 : value / 10.0;
            }
            p = q;
        }
    }

    if (end) {
        *end = (char *)p;
    }
    return sign * value;
}

/* Insertion sort: microui only sorts its handful of root containers */
void qsort(void *base, size_t num, size_t size, int (*compare)(const void *, const void *))
{
    uint8_t *items = base;
    for (size_t i = 1; i < num; i++) {
        for (size_t j = i; j > 0 && compare(items + (j - 1) * size, items + j * size) > 0; j--) {
            uint8_t *a = items + (j - 1) * size;
            uint8_t *b = items + j * size;
            for (size_t k = 0; k < size; k++) {
                uint8_t t = a[k];
                a[k] = b[k];
                b[k] = t;
            }
        }
    }
}
