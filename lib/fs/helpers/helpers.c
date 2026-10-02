/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <lions/fs/protocol.h>
#include <lions/fs/config.h>
#include <lions/fs/helpers.h>

extern fs_client_config_t fs_config;
extern fs_queue_t *fs_command_queue;
extern fs_queue_t *fs_completion_queue;
extern char *fs_share;

void (*blocking_wait)(microkit_channel ch) = NULL;

/*
 * A request id is only useful if the server can complete it: fs_command_issue()
 * refuses ids above REQUEST_ID_MAXIMUM, and fs_process_completions() drops such a
 * completion, which would strand the caller. So the number of outstanding requests
 * is bounded by the request table, not by the number of share buffers.
 */
#define REQUEST_ID_MAXIMUM (FS_QUEUE_CAPACITY - 1)
struct request_metadata {
    fs_cmd_t command;
    fs_cmpl_t completion;
    bool used;
    bool complete;
} request_metadata[FS_QUEUE_CAPACITY];

/*
 * Share buffers are a different thing: each is a 32 KiB slice of the client's share
 * region, so how many exist depends on that region's size rather than on the queue.
 * Both filesystem backends give the client 64 MiB, which holds 2048 slices; we take
 * NUM_BUFFERS of them and leave the rest. buffer_metadata must therefore be sized
 * for NUM_BUFFERS -- sizing it for the queue capacity walked the table off its end
 * and issued offsets the share region does not cover.
 */
#define NUM_BUFFERS FS_QUEUE_CAPACITY * 4
struct buffer_metadata {
    bool used;
} buffer_metadata[NUM_BUFFERS];

int fs_request_allocate(uint64_t *request_id) {
    for (uint64_t i = 0; i < FS_QUEUE_CAPACITY; i++) {
        if (!request_metadata[i].used) {
            request_metadata[i].used = true;
            *request_id = i;
            return 0;
        }
    }
    return 1;
}

void fs_request_free(uint64_t request_id) {
    assert(request_id <= REQUEST_ID_MAXIMUM);
    assert(request_metadata[request_id].used);
    request_metadata[request_id].used = false;
    request_metadata[request_id].complete = false;
}

int fs_buffer_allocate(ptrdiff_t *buffer) {
    for (int i = 0; i < NUM_BUFFERS; i++) {
        if (!buffer_metadata[i].used) {
            buffer_metadata[i].used = true;
            *buffer = i * FS_BUFFER_SIZE;
            return 0;
        }
    }
    return 1;
}

void fs_buffer_free(ptrdiff_t buffer) {
    uint64_t i = buffer / FS_BUFFER_SIZE;
    assert(i < NUM_BUFFERS);
    assert(buffer_metadata[i].used);
    buffer_metadata[i].used = false;
    memset(fs_share + buffer, 0, FS_BUFFER_SIZE);
}

void *fs_buffer_ptr(ptrdiff_t buffer) { return fs_share + buffer; }

// TODO: probably turn this API into multiple calls from the user so they
// can decide how to process the completion themselves rather than passing
// function pointers.
void fs_process_completions(void (*fs_request_flag_set)(uint64_t)) {
    uint64_t to_consume = fs_queue_length_consumer(fs_completion_queue);
    for (uint64_t i = 0; i < to_consume; i++) {
        fs_cmpl_t completion = fs_queue_idx_filled(fs_completion_queue, i)->cmpl;

        if (completion.id > REQUEST_ID_MAXIMUM) {
            printf("received bad fs completion: invalid request id: %lu\n", completion.id);
            continue;
        }

        request_metadata[completion.id].completion = completion;
        request_metadata[completion.id].complete = true;
        if (fs_request_flag_set != NULL) {
            fs_request_flag_set(completion.id);
        }
    }
    fs_queue_publish_consumption(fs_completion_queue, to_consume);
}

void fs_command_issue(fs_cmd_t cmd) {
    assert(cmd.id <= REQUEST_ID_MAXIMUM);
    assert(request_metadata[cmd.id].used);

    fs_msg_t message = { .cmd = cmd };
    assert(fs_queue_length_producer(fs_command_queue) != FS_QUEUE_CAPACITY);
    *fs_queue_idx_empty(fs_command_queue, 0) = message;
    fs_queue_publish_production(fs_command_queue, 1);
    microkit_notify(fs_config.server.id);
    request_metadata[cmd.id].command = cmd;
}

void fs_command_complete(uint64_t request_id, fs_cmd_t *command, fs_cmpl_t *completion) {
    assert(request_metadata[request_id].complete);
    if (command != NULL) {
        *command = request_metadata[request_id].command;
    }
    if (completion != NULL) {
        *completion = request_metadata[request_id].completion;
    }
}

void fs_set_blocking_wait(void (*f)(microkit_channel)) { blocking_wait = f; }

int fs_command_blocking(fs_cmpl_t *completion, fs_cmd_t cmd) {
    assert(blocking_wait);
    uint64_t request_id;
    int err = fs_request_allocate(&request_id);
    if (err) {
        return -1;
    }
    cmd.id = request_id;

    fs_command_issue(cmd);
    while (!request_metadata[request_id].complete) {
        blocking_wait(fs_config.server.id);
    }

    fs_command_complete(request_id, NULL, completion);
    fs_request_free(request_id);
    return 0;
}
