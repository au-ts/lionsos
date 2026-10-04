/*
 * Copyright 2026, LionsOS Contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Host-side tests for the shared window size limits in lions/gui/protocol.h.
 *
 * The compositor resizes a window by handing an application a size, and then
 * validates whatever that application commits. If the two disagree the window
 * is closed rather than resized, so the limits have to live in one place both
 * sides use. These tests pin that: the surface area, the 64 pixel minimum and
 * the 2048 pixel per-dimension cap, which the compositor has always enforced
 * and which a 3000x64 window would otherwise slip past.
 *
 * The helpers are static inline in the header, so there is no component source
 * to compile; the header is freestanding and builds for the host unchanged.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <lions/gui/protocol.h>

/* The desktop's surface region, so the tests use the size that ships */
#define TEST_SURFACE_BYTES 0x200000

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

/* The largest area the surface can hold, and one pixel more */
static int test_area_at_the_limit_fits(void)
{
    uint32_t w = 512;
    uint32_t h = TEST_SURFACE_BYTES / (512 * sizeof(uint32_t));
    CHECK(gui_size_fits(w, h, TEST_SURFACE_BYTES), "512x%u should fit exactly", h);
    CHECK(!gui_size_fits(w, h + 1, TEST_SURFACE_BYTES), "one row more should not fit");
    return 0;
}

static int test_minimum_is_enforced(void)
{
    CHECK(!gui_size_fits(GUI_MIN_WIDTH - 1, 100, TEST_SURFACE_BYTES), "a narrow window should not fit");
    CHECK(!gui_size_fits(100, GUI_MIN_HEIGHT - 1, TEST_SURFACE_BYTES), "a short window should not fit");
    CHECK(gui_size_fits(GUI_MIN_WIDTH, GUI_MIN_HEIGHT, TEST_SURFACE_BYTES), "the minimum should fit");
    return 0;
}

/*
 * The reason 2048 has to be in the shared helper: 3000x64 is 768,000 bytes,
 * comfortably inside 2 MiB, but the compositor rejects any dimension above
 * APP_MAX_DIMENSION and would close the window it had just resized.
 */
static int test_oversize_dimension_fits_no_better_than_area(void)
{
    CHECK(!gui_size_fits(3000, 64, TEST_SURFACE_BYTES), "3000x64 fits the area but exceeds the cap");
    CHECK(!gui_size_fits(64, 3000, TEST_SURFACE_BYTES), "64x3000 fits the area but exceeds the cap");
    CHECK(gui_size_fits(GUI_MAX_DIMENSION, GUI_MIN_HEIGHT, TEST_SURFACE_BYTES),
          "the cap itself should be accepted");
    return 0;
}

static int test_clamp_raises_a_tiny_request(void)
{
    uint32_t w = 1;
    uint32_t h = 1;
    gui_size_clamp(&w, &h, TEST_SURFACE_BYTES);
    CHECK(w == GUI_MIN_WIDTH && h == GUI_MIN_HEIGHT, "1x1 should clamp to %ux%u, got %ux%u",
          GUI_MIN_WIDTH, GUI_MIN_HEIGHT, w, h);
    return 0;
}

static int test_clamp_lowers_height_to_fit(void)
{
    /* 2048 wide at full height is far more than the surface holds */
    uint32_t w = GUI_MAX_DIMENSION;
    uint32_t h = GUI_MAX_DIMENSION;
    gui_size_clamp(&w, &h, TEST_SURFACE_BYTES);
    CHECK(gui_size_fits(w, h, TEST_SURFACE_BYTES), "the clamped %ux%u should fit", w, h);
    CHECK(h >= GUI_MIN_HEIGHT, "clamping must not take the height below %u, got %u",
          GUI_MIN_HEIGHT, h);
    return 0;
}

static int test_clamp_brings_an_oversize_dimension_down(void)
{
    uint32_t w = 3000;
    uint32_t h = 64;
    gui_size_clamp(&w, &h, TEST_SURFACE_BYTES);
    CHECK(w == GUI_MAX_DIMENSION, "3000 should clamp to %u, got %u", GUI_MAX_DIMENSION, w);
    CHECK(gui_size_fits(w, h, TEST_SURFACE_BYTES), "the clamped %ux%u should fit", w, h);
    return 0;
}

static int test_clamp_leaves_a_fitting_request_alone(void)
{
    uint32_t w = 400;
    uint32_t h = 300;
    gui_size_clamp(&w, &h, TEST_SURFACE_BYTES);
    CHECK(w == 400 && h == 300, "a fitting request should be unchanged, got %ux%u", w, h);
    return 0;
}

int main(void)
{
    printf("lions/gui/protocol.h sizes\n");
    RUN(test_area_at_the_limit_fits);
    RUN(test_minimum_is_enforced);
    RUN(test_oversize_dimension_fits_no_better_than_area);
    RUN(test_clamp_raises_a_tiny_request);
    RUN(test_clamp_lowers_height_to_fit);
    RUN(test_clamp_brings_an_oversize_dimension_down);
    RUN(test_clamp_leaves_a_fitting_request_alone);

    printf("\n%u run, %u failed\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
