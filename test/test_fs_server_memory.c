/*
 * Copyright 2026, LionsOS Contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Host-side tests for lib/fs/server/memory.c.
 *
 * This is the trust boundary for every untrusted offset a client can put in a
 * filesystem command: no server ever dereferences a client-supplied pointer, and
 * everything arrives here as an (offset, size) pair that has to be checked before
 * it is turned into an address. These tests pin that checking, including the
 * overflow-safe formulation.
 *
 * memory.c has no Microkit dependency, so it compiles for the host unchanged.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <lions/fs/protocol.h>
#include <lions/fs/server.h>

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

/* Both backends hard-code a 64 MiB client share. */
#define TEST_SHARE_SIZE (64 * 1024 * 1024)

static char *share;

static fs_buffer_t at(uint64_t offset, uint64_t size)
{
    fs_buffer_t b = { .offset = offset, .size = size };
    return b;
}

/*
 * The bug this exists for: a single transfer may not exceed one 32 KiB slot.
 *
 * A transfer that fits inside the 64 MiB share is still able to run off the end of
 * the slot it names and overwrite whatever the client has placed beside it -- another
 * file's data, or a path the server is about to trust. fs_get_client_buffer() only
 * knows about the share, so it accepted these.
 */
static int test_slot_rejects_transfer_wider_than_a_slot(void)
{
    /* Comfortably inside the share, but twice a slot. */
    CHECK(fs_get_client_slot(share, TEST_SHARE_SIZE, at(0, 2 * FS_BUFFER_SIZE)) == NULL,
          "a 2-slot transfer at offset 0 was accepted; it overruns the slot it names");

    /* The same overrun, at an offset in the middle of the share. */
    CHECK(fs_get_client_slot(share, TEST_SHARE_SIZE, at(8 * FS_BUFFER_SIZE,
                                                        2 * FS_BUFFER_SIZE))
              == NULL,
          "a 2-slot transfer at offset %d was accepted", 8 * FS_BUFFER_SIZE);

    /* One byte over is still an overrun. */
    CHECK(fs_get_client_slot(share, TEST_SHARE_SIZE, at(0, FS_BUFFER_SIZE + 1)) == NULL,
          "a transfer of FS_BUFFER_SIZE+1 bytes was accepted");
    return 0;
}

/* A full slot is the largest legitimate transfer and must still work. */
static int test_slot_accepts_a_full_slot(void)
{
    void *p = fs_get_client_slot(share, TEST_SHARE_SIZE, at(0, FS_BUFFER_SIZE));
    CHECK(p == share, "a full-size slot was refused");

    for (unsigned slot = 1; slot < 8; slot++) {
        fs_buffer_t b = at((uint64_t)slot * FS_BUFFER_SIZE, FS_BUFFER_SIZE);
        p = fs_get_client_slot(share, TEST_SHARE_SIZE, b);
        CHECK(p == share + (uint64_t)slot * FS_BUFFER_SIZE, "slot %u resolved to the wrong address",
              slot);
    }
    return 0;
}

/* A short transfer is fine; a stream read is allowed to ask for less than a slot. */
static int test_slot_accepts_a_partial_transfer(void)
{
    fs_buffer_t b = at(4 * FS_BUFFER_SIZE + 17, 100);
    void *p = fs_get_client_slot(share, TEST_SHARE_SIZE, b);
    CHECK(p == share + 4 * FS_BUFFER_SIZE + 17, "a partial transfer resolved to the wrong address");
    return 0;
}

/* The share bounds still apply, and the check must not itself overflow. */
static int test_slot_still_checks_the_share_bounds(void)
{
    CHECK(fs_get_client_slot(share, TEST_SHARE_SIZE, at(TEST_SHARE_SIZE, 1)) == NULL,
          "an offset at the end of the share was accepted");

    /* offset + size would wrap if it were computed naively; it must not be. */
    CHECK(fs_get_client_slot(share, TEST_SHARE_SIZE, at(TEST_SHARE_SIZE - 16, 32)) == NULL,
          "a transfer whose offset+size overflows was accepted");

    CHECK(fs_get_client_slot(share, TEST_SHARE_SIZE, at(0, 0)) == NULL,
          "a zero-length transfer was accepted");

    CHECK(fs_get_client_slot(share, TEST_SHARE_SIZE, at(UINT64_MAX, 1)) == NULL,
          "a wildly out-of-range offset was accepted");
    return 0;
}

/*
 * fs_get_client_buffer() is the looser accessor, kept for the paths that legitimately
 * deal in buffers larger than a slot. It must keep behaving exactly as it did: these
 * tests exist to catch a change to the shared, more permissive contract.
 */
static int test_client_buffer_is_unchanged(void)
{
    void *p = fs_get_client_buffer(share, TEST_SHARE_SIZE, at(0, FS_BUFFER_SIZE));
    CHECK(p == share, "a full-size buffer was refused");

    p = fs_get_client_buffer(share, TEST_SHARE_SIZE, at(0, 2 * FS_BUFFER_SIZE));
    CHECK(p == share, "fs_get_client_buffer should still allow a transfer wider than a slot");

    CHECK(fs_get_client_buffer(share, TEST_SHARE_SIZE, at(0, 0)) == NULL,
          "a zero-length buffer was accepted");
    CHECK(fs_get_client_buffer(share, TEST_SHARE_SIZE, at(TEST_SHARE_SIZE - 16, 32)) == NULL,
          "a transfer running past the end of the share was accepted");
    CHECK(fs_get_client_buffer(share, TEST_SHARE_SIZE, at(UINT64_MAX, 1)) == NULL,
          "a wildly out-of-range offset was accepted");
    return 0;
}

/* Paths are still capped at FS_MAX_PATH_LENGTH and always NUL-terminated. */
static int test_client_path_is_capped_and_terminated(void)
{
    char dest[FS_MAX_PATH_LENGTH + 2];
    memset(dest, 0x5A, sizeof(dest));

    const char *path = "/some/nfs/path.txt";
    uint64_t len = strlen(path) + 1;
    memcpy(share, path, len);

    CHECK(fs_copy_client_path(dest, share, TEST_SHARE_SIZE, at(0, len)) == 0,
          "a valid path was refused");
    CHECK(strcmp(dest, path) == 0, "the path was not copied verbatim: \"%s\"", dest);

    CHECK(fs_copy_client_path(dest, share, TEST_SHARE_SIZE,
                              at(0, FS_MAX_PATH_LENGTH + 1))
              != 0,
          "a path longer than FS_MAX_PATH_LENGTH was accepted");

    CHECK(fs_copy_client_path(dest, share, TEST_SHARE_SIZE, at(TEST_SHARE_SIZE - 1, 8)) != 0,
          "a path running past the end of the share was accepted");
    return 0;
}

int main(void)
{
    share = malloc(TEST_SHARE_SIZE);
    if (share == NULL) {
        printf("could not allocate a %d byte test share region\n", TEST_SHARE_SIZE);
        return 2;
    }
    memset(share, 0, TEST_SHARE_SIZE);

    printf("lib/fs/server/memory\n");
    RUN(test_slot_rejects_transfer_wider_than_a_slot);
    RUN(test_slot_accepts_a_full_slot);
    RUN(test_slot_accepts_a_partial_transfer);
    RUN(test_slot_still_checks_the_share_bounds);
    RUN(test_client_buffer_is_unchanged);
    RUN(test_client_path_is_capped_and_terminated);

    free(share);

    printf("\n%u run, %u failed\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}