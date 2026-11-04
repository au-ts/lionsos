/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Host test of the fs client helpers. Runs the allocators to their limits
 * so an overrun of either table shows up under the sanitisers.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lions/fs/protocol.h>
#include <lions/fs/config.h>
#include <lions/fs/helpers.h>

#define SHARE_SIZE (64 * 1024 * 1024)
#define SHARE_BUFFERS (SHARE_SIZE / FS_BUFFER_SIZE)

fs_client_config_t fs_config;
fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

static unsigned notifies;

void microkit_notify(microkit_channel ch) {
    assert(ch == fs_config.server.id);
    notifies++;
}

static void wait_none(microkit_channel ch) {
    (void)ch;
    abort();
}

static void test_buffers_fill_the_share(void) {
    static ptrdiff_t taken[SHARE_BUFFERS];
    ptrdiff_t extra;
    size_t n = 0;

    while (n < SHARE_BUFFERS && fs_buffer_allocate(&taken[n]) == 0) {
        assert(taken[n] == (ptrdiff_t)(n * FS_BUFFER_SIZE));
        n++;
    }
    assert(n > FS_QUEUE_CAPACITY);
    assert(fs_buffer_allocate(&extra) != 0);
    memset(fs_buffer_ptr(taken[n - 1]), 0xa5, FS_BUFFER_SIZE);
    printf("buffers: %zu fit in a %u byte share\n", n, SHARE_SIZE);
    while (n > 0) {
        fs_buffer_free(taken[--n]);
    }
    assert(fs_buffer_allocate(&extra) == 0);
    assert(extra == 0);
    fs_buffer_free(extra);
}

static void test_requests_stop_at_the_queue(void) {
    static uint64_t ids[FS_QUEUE_CAPACITY];
    uint64_t extra;
    size_t n = 0;

    while (n < FS_QUEUE_CAPACITY && fs_request_allocate(&ids[n]) == 0) {
        assert(ids[n] == n);
        n++;
    }
    assert(n == FS_QUEUE_CAPACITY);
    assert(fs_request_allocate(&extra) != 0);
    while (n > 0) {
        fs_request_free(ids[--n]);
    }
    printf("requests: %d ids\n", FS_QUEUE_CAPACITY);
}

static void test_small_share_limits_buffers(void) {
    ptrdiff_t a, b, c;

    fs_config.server.share.size = 2 * FS_BUFFER_SIZE;
    assert(fs_buffer_allocate(&a) == 0);
    assert(fs_buffer_allocate(&b) == 0);
    assert(fs_buffer_allocate(&c) != 0);
    fs_buffer_free(b);
    fs_buffer_free(a);
    fs_config.server.share.size = SHARE_SIZE;
    printf("small share: two buffers\n");
}

static void test_command_round_trip(void) {
    fs_cmpl_t completion;
    uint64_t id;
    fs_msg_t *msg;

    assert(fs_request_allocate(&id) == 0);
    fs_command_issue((fs_cmd_t){ .id = id, .type = FS_CMD_FILE_SYNC });
    assert(notifies == 1);
    assert(fs_queue_length_consumer(fs_command_queue) == 1);
    msg = fs_queue_idx_filled(fs_command_queue, 0);
    assert(msg->cmd.id == id && msg->cmd.type == FS_CMD_FILE_SYNC);
    fs_queue_publish_consumption(fs_command_queue, 1);

    msg = fs_queue_idx_empty(fs_completion_queue, 0);
    msg->cmpl = (fs_cmpl_t){ .id = id, .status = FS_STATUS_SUCCESS };
    fs_queue_publish_production(fs_completion_queue, 1);
    fs_process_completions(NULL);
    fs_command_complete(id, NULL, &completion);
    assert(completion.status == FS_STATUS_SUCCESS);
    fs_request_free(id);
    printf("round trip: ok\n");
}

int main(void) {
    fs_share = calloc(1, SHARE_SIZE);
    fs_command_queue = calloc(1, sizeof *fs_command_queue);
    fs_completion_queue = calloc(1, sizeof *fs_completion_queue);
    fs_config.server.share.vaddr = fs_share;
    fs_config.server.share.size = SHARE_SIZE;
    fs_config.server.id = 7;
    fs_set_blocking_wait(wait_none);

    test_buffers_fill_the_share();
    test_requests_stop_at_the_queue();
    test_small_share_limits_buffers();
    test_command_round_trip();
    printf("PASS fs helpers\n");
    return 0;
}
