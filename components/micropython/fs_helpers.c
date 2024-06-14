#include <microkit.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <lions/fs/protocol.h>
#include "micropython.h"
#include "fs_helpers.h"

extern char *nfs_share;
struct sddf_fs_queue *nfs_command_queue;
struct sddf_fs_queue *nfs_completion_queue;

#define REQUEST_ID_MAXIMUM (SDDF_FS_QUEUE_CAPACITY - 1)
struct request_metadata {
    struct sddf_fs_command command;
    struct sddf_fs_completion completion;
    bool used;
    bool complete;
} request_metadata[SDDF_FS_QUEUE_CAPACITY];

#define NUM_BUFFERS SDDF_FS_QUEUE_CAPACITY * 4
struct buffer_metadata {
    bool used;
} buffer_metadata[SDDF_FS_QUEUE_CAPACITY];

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

char *fs_buffer_ptr(ptrdiff_t buffer) {
    return nfs_share + buffer;
}

void fs_process_completions(void) {
    union sddf_fs_message message;
    while (sddf_fs_queue_pop(nfs_completion_queue, &message)) {
        struct sddf_fs_completion completion = message.completion;

        if (completion.request_id > REQUEST_ID_MAXIMUM) {
            printf("received bad fs completion: invalid request id: %lu\n", completion.request_id);
            continue;
        }

        request_metadata[completion.request_id].completion = completion;
        request_metadata[completion.request_id].complete = true;
        fs_request_flag_set(completion.request_id);
    }
}

void fs_command_issue(uint64_t request_id, uint32_t cmd_type, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    assert(request_id <= REQUEST_ID_MAXIMUM);
    assert(request_metadata[request_id].used);

    union sddf_fs_message message;
    message.command = (struct sddf_fs_command) {
        .request_id = request_id,
        .cmd_type = cmd_type,
        .args = {
            [0] = arg0,
            [1] = arg1,
            [2] = arg2,
            [3] = arg3,
        }
    };
    bool success = sddf_fs_queue_push(nfs_command_queue, message);
    assert(success);
    microkit_notify(NFS_CH);

    request_metadata[request_id].command = message.command;
}

void fs_command_complete(uint64_t request_id, struct sddf_fs_command *command, struct sddf_fs_completion *completion) {
    assert(request_metadata[request_id].complete);
    if (command != NULL) {
        *command = request_metadata[request_id].command;
    }
    if (completion != NULL) {
        *completion = request_metadata[request_id].completion;
    }
}

int fs_command_blocking(struct sddf_fs_completion *completion, uint32_t cmd_type, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    uint64_t request_id;
    int err = fs_request_allocate(&request_id);
    if (err) {
        return -1;
    }

    fs_command_issue(request_id, cmd_type, arg0, arg1, arg2, arg3);

    while (!request_metadata[request_id].complete) {
        await(NFS_CH);
    }

    fs_command_complete(request_id, NULL, completion);
    fs_request_free(request_id);
    return 0;
}
