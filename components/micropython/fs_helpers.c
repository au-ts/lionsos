#include <microkit.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <lions/fs/protocol.h>
#include "micropython.h"
#include "fs_helpers.h"

extern char *fs_data;
struct fs_queue *fs_command_queue;
struct fs_queue *fs_completion_queue;

#define REQUEST_ID_MAXIMUM (FS_QUEUE_CAPACITY - 1)
struct request_metadata {
    fs_cmd_t command;
    fs_cmpl_t completion;
    bool used;
    bool complete;
} request_metadata[FS_QUEUE_CAPACITY];

#define NUM_BUFFERS FS_QUEUE_CAPACITY * 4
struct buffer_metadata {
    bool used;
} buffer_metadata[FS_QUEUE_CAPACITY];

int fs_request_allocate(uint64_t *request_id) {
    for (uint64_t i = 0; i < NUM_BUFFERS; i++) {
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
}

void *fs_buffer_ptr(ptrdiff_t buffer) {
    return fs_data + buffer;
}

void fs_process_completions(void) {
    fs_msg_t message;
    uint64_t to_consume = fs_queue_size_consumer(fs_completion_queue);
    for (uint64_t i = 0; i < to_consume; i++) {
        fs_cmpl_t completion = fs_queue_idx_filled(fs_completion_queue, i)->cmpl;

        if (completion.id > REQUEST_ID_MAXIMUM) {
            printf("received bad fs completion: invalid request id: %lu\n", completion.id);
            continue;
        }

        request_metadata[completion.id].completion = completion;
        request_metadata[completion.id].complete = true;
        fs_request_flag_set(completion.id);
    }
    fs_queue_publish_consumption(fs_completion_queue, to_consume);
}

void fs_command_issue(fs_cmd_t cmd) {
    assert(cmd.id <= REQUEST_ID_MAXIMUM);
    assert(request_metadata[cmd.id].used);

    fs_msg_t message = { .cmd = cmd };
    assert(fs_queue_size_producer(fs_command_queue) != FS_QUEUE_CAPACITY);
    *fs_queue_idx_empty(fs_command_queue, 0) = message;
    fs_queue_publish_production(fs_command_queue, 1);
    microkit_notify(NFS_CH);
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

int fs_command_blocking(fs_cmpl_t *completion, fs_cmd_t cmd) {
    uint64_t request_id;
    int err = fs_request_allocate(&request_id);
    if (err) {
        return -1;
    }
    cmd.id = request_id;

    fs_command_issue(cmd);
    while (!request_metadata[request_id].complete) {
        microkit_cothread_wait_on_channel(NFS_CH);
    }

    fs_command_complete(request_id, NULL, completion);
    fs_request_free(request_id);
    return 0;
}
