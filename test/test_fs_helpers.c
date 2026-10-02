/*
 * Copyright 2026, LionsOS Contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Host-side tests for lib/fs/helpers/helpers.c.
 *
 * These exist because CI is build-only (ci/README.md), so there is no on-target
 * test for the filesystem client helpers, and the bugs these cover are silent
 * memory corruption that a successful build cannot reveal.
 *
 * helpers.o is compiled into libc.a and also linked into the MicroPython and WAMR
 * protection domains, so a mistake here is shared by every component that uses the
 * filesystem.
 *
 * Each test runs in a forked child. The allocator state in helpers.c is file-static
 * with no reset entry point, and a forked child gets a pristine copy; that also means
 * assertions can abort freely without taking the suite down with them.
 */

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <lions/fs/config.h>
#include <lions/fs/helpers.h>

/* The client-side globals that helpers.c expects its component to define. */
fs_client_config_t fs_config;
fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

unsigned long test_notify_count;
void microkit_notify(microkit_channel ch)
{
    (void)ch;
    test_notify_count++;
}

/* Both backends hard-code a 64 MiB client share; see components/fs/{fat,nfs}/. */
#define TEST_SHARE_SIZE (64 * 1024 * 1024)
#define SHARE_SLOTS (TEST_SHARE_SIZE / FS_BUFFER_SIZE)

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

#define RUN(fn)                                                                                 \
    do {                                                                                        \
        tests_run++;                                                                            \
        run_test(#fn, fn);                                                                      \
    } while (0)

/*
 * BUG (helpers.c, fs_request_allocate): the loop ran to NUM_BUFFERS (2044) while
 * indexing request_metadata[FS_QUEUE_CAPACITY] (511 entries). Once the 511 real slots
 * were full it kept walking past the end of the table, handing out request ids that
 * fs_process_completions() drops and fs_command_issue() asserts on -- so the caller
 * waits for a completion that was thrown away.
 *
 * The table must yield exactly as many ids as it has entries, then report exhaustion.
 */
static int test_request_table_exhausts_cleanly(void)
{
    uint64_t id;
    uint64_t allocated = 0;

    while (fs_request_allocate(&id) == 0) {
        CHECK(id < FS_QUEUE_CAPACITY, "request id %lu is outside the %d-entry table", id,
              FS_QUEUE_CAPACITY);
        allocated++;
        CHECK(allocated <= 1000000, "fs_request_allocate never reported exhaustion");
    }

    CHECK(allocated == FS_QUEUE_CAPACITY,
          "table yielded %lu ids, expected %d (one per entry)", allocated, FS_QUEUE_CAPACITY);

    CHECK(fs_request_allocate(&id) != 0, "allocator handed out an id after exhaustion");
    return 0;
}

/*
 * Every id the allocator returns has to be usable: fs_process_completions() drops ids
 * above REQUEST_ID_MAXIMUM and fs_command_issue() asserts on them, so an out-of-range id
 * becomes a hang or an abort in the caller rather than an error return.
 */
static int test_request_ids_are_usable(void)
{
    uint64_t ids[FS_QUEUE_CAPACITY];

    for (unsigned i = 0; i < FS_QUEUE_CAPACITY; i++) {
        CHECK(fs_request_allocate(&ids[i]) == 0, "allocation %u failed early", i);
    }
    for (unsigned i = 0; i < FS_QUEUE_CAPACITY; i++) {
        CHECK(ids[i] <= FS_QUEUE_CAPACITY - 1, "request id %lu is above REQUEST_ID_MAXIMUM", ids[i]);
    }

    /* Freeing them all must make the whole table available again. */
    for (unsigned i = 0; i < FS_QUEUE_CAPACITY; i++) {
        fs_request_free(ids[i]);
    }
    uint64_t id;
    CHECK(fs_request_allocate(&id) == 0, "table was not reusable after freeing every id");
    return 0;
}

/*
 * BUG (helpers.c, fs_buffer_allocate): the loop ran to NUM_BUFFERS (2044) while
 * indexing buffer_metadata[FS_QUEUE_CAPACITY] (511 entries), so it walked off the end of
 * that table too and issued offsets the share region does not cover.
 */
static int test_buffer_pool_exhausts_cleanly(void)
{
    ptrdiff_t buffer;
    uint64_t allocated = 0;

    while (fs_buffer_allocate(&buffer) == 0) {
        CHECK(buffer >= 0, "negative buffer offset %td", buffer);
        CHECK((uint64_t)buffer + FS_BUFFER_SIZE <= TEST_SHARE_SIZE,
              "buffer offset %td runs past the %d byte share region", buffer, TEST_SHARE_SIZE);
        allocated++;
        CHECK(allocated <= 1000000, "fs_buffer_allocate never reported exhaustion");
    }

    CHECK(allocated > 0, "no buffers could be allocated at all");
    CHECK(allocated <= SHARE_SLOTS,
          "allocator handed out %lu buffers, more than the %d slots the share region holds",
          allocated, SHARE_SLOTS);

    CHECK(fs_buffer_allocate(&buffer) != 0, "allocator handed out a buffer after exhaustion");
    return 0;
}

/*
 * fs_buffer_free() zeroes the whole 32 KiB on release. That is a security property, not
 * tidiness: a later command may run with fewer privileges than the one that just wrote
 * file contents or a path into that slot.
 */
static int test_buffer_free_zeroes_the_region(void)
{
    ptrdiff_t buffer;
    CHECK(fs_buffer_allocate(&buffer) == 0, "allocation failed");

    volatile uint8_t *region = fs_buffer_ptr(buffer);
    memset(fs_buffer_ptr(buffer), 0xAB, FS_BUFFER_SIZE);
    CHECK(region[0] == 0xAB, "test setup failed to dirty the buffer");

    fs_buffer_free(buffer);
    for (unsigned i = 0; i < FS_BUFFER_SIZE; i++) {
        CHECK(region[i] == 0x00, "byte %u of the released buffer is 0x%02x, expected 0x00", i,
              region[i]);
    }
    return 0;
}

/* A freed buffer must be handed out again rather than leaking. */
static int test_buffers_are_reusable(void)
{
    ptrdiff_t first;
    CHECK(fs_buffer_allocate(&first) == 0, "allocation failed");
    fs_buffer_free(first);

    ptrdiff_t second;
    CHECK(fs_buffer_allocate(&second) == 0, "buffer was not returned to the pool");
    CHECK(second == first, "expected the just-freed buffer %td back, got %td", first, second);
    return 0;
}

/*
 * BUG (components/micropython/vfs_fs_file.c:304): vfs_file_open() released the path buffer
 * twice -- once after FILE_OPEN at :288 and again on the FILE_SIZE failure path at :304.
 * fs_buffer_free() asserts on a buffer that is not in use, so a debug build aborts and a
 * release build walks the metadata table and reissues an offset another call may own.
 *
 * helpers.c owns that invariant, so the detection is asserted here. Reaching this test's
 * own allocation is what proves the double free still aborts after the fix.
 */
static int test_double_buffer_free_is_detected(void)
{
    ptrdiff_t buffer;
    CHECK(fs_buffer_allocate(&buffer) == 0, "allocation failed");
    fs_buffer_free(buffer);

    /* The assert below is expected to fire, which kills this child with SIGABRT. */
    freopen("/dev/null", "w", stderr);
    fs_buffer_free(buffer);
    CHECK(false, "a second fs_buffer_free() of the same offset went undetected");
    return 0;
}

/*
 * fs_process_completions() guards against a server replying with a request id the client
 * never issued. That guard is the only thing between a corrupt queue and an out-of-bounds
 * write into request_metadata[], so it is worth holding in place.
 *
 * Note fs_queue_length_consumer() reports how many entries are *available* to the consumer
 * (tail - head), so it reads zero once the queue has been drained.
 */
static int test_rogue_completion_id_is_dropped(void)
{
    fs_queue_t *queue = malloc(sizeof(fs_queue_t));
    CHECK(queue != NULL, "could not allocate a completion queue");
    memset(queue, 0, sizeof(fs_queue_t));
    fs_completion_queue = queue;

    fs_msg_t rogue;
    memset(&rogue, 0, sizeof(rogue));
    rogue.cmpl.id = 0xFFFFFF;
    rogue.cmpl.status = FS_STATUS_SUCCESS;

    *fs_queue_idx_empty(fs_completion_queue, 0) = rogue;
    fs_queue_publish_production(fs_completion_queue, 1);
    CHECK(fs_queue_length_consumer(fs_completion_queue) == 1, "test setup failed to publish");

    fs_process_completions(NULL);

    CHECK(fs_queue_length_consumer(fs_completion_queue) == 0,
          "the rogue completion was not consumed, so the queue is backing up");
    return 0;
}

int main(void)
{
    fs_share = malloc(TEST_SHARE_SIZE);
    if (fs_share == NULL) {
        printf("could not allocate a %d byte test share region\n", TEST_SHARE_SIZE);
        return 2;
    }
    memset(fs_share, 0, TEST_SHARE_SIZE);

    printf("lib/fs/helpers\n");
    RUN(test_request_table_exhausts_cleanly);
    RUN(test_request_ids_are_usable);
    RUN(test_buffer_pool_exhausts_cleanly);
    RUN(test_buffer_free_zeroes_the_region);
    RUN(test_buffers_are_reusable);
    RUN(test_rogue_completion_id_is_dropped);

    /* This one is expected to abort its own child, so it is reported by hand. */
    tests_run++;
    printf("  %-52s", "test_double_buffer_free_is_detected");
    fflush(stdout);
    {
        pid_t pid = fork();
        if (pid == 0) {
            fflush(stdout);
            _exit(test_double_buffer_free_is_detected());
        }
        int status;
        waitpid(pid, &status, 0);
        if (WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT) {
            printf(" ok\n");
        } else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            printf(" ok\n");
        } else {
            printf(" FAIL (double free not detected)\n");
            tests_failed++;
        }
    }

    free(fs_share);

    printf("\n%u run, %u failed\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}