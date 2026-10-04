/*
 * Copyright 2026, LionsOS Contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Host-side tests for the surface relayout in examples/desktop/src/gfx.c.
 *
 * A window's surface has stride == width, so resizing genuinely moves pixels:
 * the rows are a different distance apart afterwards. gfx_relayout() copies the
 * overlapping region across that change in place, and paints everything the
 * resize exposes, since leaving it alone would show stale pixels from the old
 * layout. These tests pin both, and the sanitizer pins the bounds.
 *
 * gfx.c is freestanding, so it compiles for the host unchanged.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "gfx.h"

/* Room for the largest window either layout in these tests can produce */
#define MAX_W 64
#define MAX_H 64
#define PIXELS (MAX_W * MAX_H)

#define FILL 0xff00ff00u

static unsigned tests_run;
static unsigned tests_failed;

#define CHECK(cond, ...)                                                                       \
    do {                                                                                        \
        if (!(cond)) {                                                                          \
            printf("\n      FAIL %s:%d: ", __FILE__, __LINE__);                                 \
            printf(__VA_ARGS__);                                                                \
            printf("\n");                                                                       \
            fflush(stdout);                                                                     \
            return 1;                                                                           \
        }                                                                                        \
    } while (0)

static int run_test(const char *name, int (*fn)(void))
{
    printf("  %-52s", name);
    fflush(stdout);

    pid_t pid = fork();
    if (pid < 0) {
        printf(" FAIL (fork)\n");
        tests_failed++;
        tests_run++;
        return 1;
    }
    if (pid == 0) {
        fflush(stdout);
        _exit(fn() == 0 ? 0 : 1);
    }

    int status;
    if (waitpid(pid, &status, 0) != pid) {
        printf(" FAIL (waitpid)\n");
        tests_failed++;
        tests_run++;
        return 1;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        printf(" ok\n");
        return 0;
    }
    if (WIFSIGNALED(status)) {
        printf(" FAIL (killed by signal %d)\n", WTERMSIG(status));
    } else {
        printf(" FAIL (exit %d)\n", WEXITSTATUS(status));
    }
    tests_failed++;
    return 1;
}

#define RUN(fn)                                                                                \
    do {                                                                                        \
        tests_run++;                                                                            \
        run_test(#fn, fn);                                                                      \
    } while (0)

/* Every pixel gets a value derived from its coordinates, so a shifted copy shows up */
static void fill_pattern(uint32_t *p, uint32_t w, uint32_t h)
{
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            p[y * w + x] = (y << 16) | x;
        }
    }
}

/*
 * Check one relayout: the overlap must still hold the pattern, and every pixel
 * outside it must be the fill colour.
 */
static int check(uint32_t old_w, uint32_t old_h, uint32_t new_w, uint32_t new_h)
{
    uint32_t *p = malloc(PIXELS * sizeof(uint32_t));
    if (p == NULL) {
        return 1;
    }
    memset(p, 0xcd, PIXELS * sizeof(uint32_t));
    fill_pattern(p, old_w, old_h);

    gfx_relayout(p, old_w, old_h, new_w, new_h, FILL);

    uint32_t copy_w = old_w < new_w ? old_w : new_w;
    uint32_t copy_h = old_h < new_h ? old_h : new_h;

    for (uint32_t y = 0; y < new_h; y++) {
        for (uint32_t x = 0; x < new_w; x++) {
            uint32_t got = p[y * new_w + x];
            uint32_t want = (x < copy_w && y < copy_h) ? ((y << 16) | x) : FILL;
            if (got != want) {
                printf("\n      FAIL at %ux%u of %ux%u (from %ux%u): got 0x%08x want 0x%08x\n",
                       x, y, new_w, new_h, old_w, old_h, got, want);
                free(p);
                return 1;
            }
        }
    }

    free(p);
    return 0;
}

static int test_wider(void)
{
    return check(20, 10, 40, 10);
}

static int test_narrower(void)
{
    return check(40, 10, 20, 10);
}

static int test_taller(void)
{
    return check(20, 10, 20, 30);
}

static int test_shorter(void)
{
    return check(20, 30, 20, 10);
}

static int test_both_dimensions_change(void)
{
    return check(30, 12, 12, 30);
}

static int test_unchanged(void)
{
    return check(20, 10, 20, 10);
}

/* Growing is the direction in which an in-place copy can overwrite a row it still needs */
static int test_wide_growth_keeps_every_row(void)
{
    return check(2, 40, 50, 40);
}

int main(void)
{
    printf("examples/desktop/src/gfx.c relayout\n");
    RUN(test_wider);
    RUN(test_narrower);
    RUN(test_taller);
    RUN(test_shorter);
    RUN(test_both_dimensions_change);
    RUN(test_unchanged);
    RUN(test_wide_growth_keeps_every_row);

    printf("\n%u run, %u failed\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
