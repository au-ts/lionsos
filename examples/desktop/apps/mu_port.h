/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * The few C library functions microui uses that sDDF's minimal libc lacks.
 * Force-included when compiling microui.c, see desktop.mk.
 */

#pragma once

#include <stddef.h>

int sprintf(char *buf, const char *fmt, ...);

/* microui's internal assertions report to stderr and abort */
#define stderr ((void *)0)
int fprintf(void *stream, const char *fmt, ...);
void abort(void);

double strtod(const char *str, char **end);
void qsort(void *base, size_t num, size_t size, int (*compare)(const void *, const void *));
