/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "decl.h"
#include "ff.h"
#include "diskio.h"
#include <sddf/blk/queue.h>
#include <libmicrokitco.h>
#include <lions/fs/protocol.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <blk_config.h>
#include <microkit.h>
#include <sddf/util/util.h>

#define VIRT_CH 0
#define CLIENT_CH 1

co_control_t co_controller_mem;
microkit_cothread_sem_t sem[FAT_WORKER_THREAD_NUM + 1];

blk_queue_handle_t blk_queue_handle_memory;
blk_queue_handle_t *blk_queue_handle = &blk_queue_handle_memory;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;

blk_req_queue_t *blk_req_queue;
blk_resp_queue_t *blk_resp_queue;

// Config pointed to the SDDF_blk config
blk_storage_info_t *blk_storage_info;

uint64_t worker_thread_stack_one;
uint64_t worker_thread_stack_two;
uint64_t worker_thread_stack_three;
uint64_t worker_thread_stack_four;

char *fs_share;

// File system metadata region
uintptr_t fs_metadata;

char *blk_data;

// Flag for determine if there are blk_requests pushed by the file system
// It is used to determine whether to notify the blk device driver
bool blk_request_pushed = false;

typedef enum {
    FREE,
    INUSE
} space_status;

typedef struct FS_request{
    /* Client side cmd info */
    uint64_t cmd;
    /* Used for passing data to worker threads and receiving responses */
    co_data_t shared_data;
    /* Used to track request_id */
    uint64_t request_id;
    /* Thread handle */
    microkit_cothread_ref_t handle;
    /* Self metadata */
    space_status stat;
} fs_request;

// This operations list must be consistent with the file system protocol enum
void (*operation_functions[])(void) = {
    [FS_CMD_INITIALISE] = fat_mount,
    [FS_CMD_DEINITIALISE] = fat_unmount,
    [FS_CMD_FILE_OPEN] = fat_open,
    [FS_CMD_FILE_CLOSE] = fat_close,
    [FS_CMD_STAT] = fat_stat,
    [FS_CMD_FILE_READ] = fat_pread,
    [FS_CMD_FILE_WRITE] = fat_pwrite,
    [FS_CMD_FILE_SIZE] = fat_fsize,
    [FS_CMD_RENAME] = fat_rename,
    [FS_CMD_FILE_REMOVE] = fat_unlink,
    [FS_CMD_FILE_TRUNCATE] = fat_truncate,
    [FS_CMD_DIR_CREATE] = fat_mkdir,
    [FS_CMD_DIR_REMOVE] = fat_rmdir,
    [FS_CMD_DIR_OPEN] = fat_opendir,
    [FS_CMD_DIR_CLOSE] = fat_closedir,
    [FS_CMD_FILE_SYNC] = fat_sync,
    [FS_CMD_DIR_READ] = fat_readdir,
    [FS_CMD_DIR_SEEK] = fat_seekdir,
    [FS_CMD_DIR_TELL] = fat_telldir,
    [FS_CMD_DIR_REWIND] = fat_rewinddir,
};

static fs_request request_pool[FAT_THREAD_NUM];

void fill_client_response(fs_msg_t* message, const fs_request* finished_request) {
    message->cmpl.id = finished_request->request_id;
    message->cmpl.status = finished_request->shared_data.status;
    message->cmpl.data = finished_request->shared_data.result;
}

// Setting up the request in the request_pool and push the request to the thread pool
void setup_request(int32_t index, fs_msg_t* message) {
    request_pool[index].request_id = message->cmd.id;
    request_pool[index].cmd = message->cmd.type;
    request_pool[index].shared_data.params = message->cmd.params;
    void (*func)(void) = operation_functions[request_pool[index].cmd];
    void *shared_data = &request_pool[index].shared_data;
    request_pool[index].handle = microkit_cothread_spawn(func, shared_data);
}

// For debug
void print_sector_data(uint8_t *buffer, unsigned long size) {
    for (unsigned long i = 0; i < size; i++) {
        if (i % 16 == 0) {
            LOG_FATFS("\n%04lx  ", i); // Print the offset at the start of each line
        }
        LOG_FATFS("%02x ", buffer[i]); // Print each byte in hexadecimal
    }
    LOG_FATFS("\n");
}

_Static_assert(BLK_QUEUE_CAPACITY_CLI_FATFS >= FAT_WORKER_THREAD_NUM,
    "The capacity of queue between fs and blk should be at least the size of FAT_WORKER_THREAD_NUM");

void init(void) {
    LOG_FATFS("starting\n");
    LOG_FATFS("fs_command_queue %p\n", fs_command_queue);
    // LOG_FATFS("fs_completion_queue %p\n", fs_completion_queue);
    // Init the block device queue
    // Have to make sure who initialize this SDDF queue
    blk_queue_init(blk_queue_handle, blk_req_queue, blk_resp_queue, BLK_QUEUE_CAPACITY_CLI_FATFS);
    /*
       This part of the code is for setting up the thread pool by
       assign stacks and size of the stack to the pool
    */
    uint64_t stack[FAT_WORKER_THREAD_NUM];
    stack[0] = worker_thread_stack_one;
    stack[1] = worker_thread_stack_two;
    stack[2] = worker_thread_stack_three;
    stack[3] = worker_thread_stack_four;

    for (int i = 0; i < FAT_WORKER_THREAD_NUM; i++) {
        LOG_FATFS("stack %p\n", stack[i]);
    }

    // LOG_FATFS("before cothread fs_completion_queue %p\n", fs_completion_queue);
    // Init thread pool
    microkit_cothread_init(&co_controller_mem,
                            FAT_WORKER_THREAD_STACKSIZE,
                            stack[0],
                            stack[1],
                            stack[2],
                            stack[3]);
    for (uint32_t i = 0; i < (FAT_WORKER_THREAD_NUM + 1); i++) {
        microkit_cothread_semaphore_init(&sem[i]);
    }
    // LOG_FATFS("after cothread fs_completion_queue %p\n", fs_completion_queue);

    // Init file system metadata
    init_metadata(fs_metadata);

    LOG_FATFS("finished init, fs_completion_queue %p, queue->head 0x%lx, queue->tail 0x%lx\n", fs_completion_queue, &fs_completion_queue->head, &fs_completion_queue->tail);
}

// The notified function requires careful management of the state of the file system
/*
  The filesystems should be blockwait for new message if and only if all of working
  threads are either free(no tasks assigned to them, no pending replies) or blocked in diskio.
  If the filesystem is blocked here and any working threads are free, then the fs_command_queue
  must also be empty.
*/
void notified(microkit_channel ch) {
    LOG_FATFS("start, fs_command_queue %p, queue->head 0x%lx, queue->tail 0x%lx\n", fs_command_queue, &fs_command_queue->head, &fs_command_queue->tail);
    LOG_FATFS("start, fs_completion_queue %p, queue->head 0x%lx, queue->tail 0x%lx\n", fs_completion_queue, &fs_completion_queue->head, &fs_completion_queue->tail);
    switch (ch) {
    case CLIENT_CH:
        LOG_FATFS("notified by client\n");
        break;
    case VIRT_CH:
        LOG_FATFS("notified by block virt\n");
        break;
    default:
        LOG_FATFS("notified on unknown channel %d\n", ch);
        assert(false);
        return;
    }

    // This variable track if there are new requests being popped from client request queue and pushed into the couroutine pool or not
    bool new_request_popped = true;
    // Get the number of elements in the queue is costly so we have a flag defined here to only get the number when needed
    bool queue_size_init = false;

    uint64_t command_queue_size;
    uint64_t completion_queue_size;

    uint32_t fs_request_dequeued = 0;
    uint32_t fs_response_enqueued = 0;

    while (new_request_popped) {
        {
            blk_resp_status_t status;
            uint16_t success_count;
            uint32_t id;
            // Get current element numbers in blk response queue
            uint32_t len = blk_queue_length_resp(blk_queue_handle);

            while (len > 0) {
                // This id is the index to the request pool
                int err = blk_dequeue_resp(blk_queue_handle, &status, &success_count, &id);
                assert(!err);

                LOG_FATFS("blk_dequeue_resp: status: %d success_count: %d ID: %d\n", status, success_count, id);

                microkit_cothread_set_arg(request_pool[id].handle, (void *)status);
                microkit_cothread_semaphore_signal(&sem[request_pool[id].handle]);

                len--;
            }
        }

        LOG_FATFS("before, fs_command_queue %p, queue->head 0x%lx, queue->tail 0x%lx\n", fs_command_queue, &fs_command_queue->head, &fs_command_queue->tail);
        LOG_FATFS("before, fs_completion_queue %p, queue->head 0x%lx, queue->tail 0x%lx\n", fs_completion_queue, &fs_completion_queue->head, &fs_completion_queue->tail);

        // Give worker threads a chance to run
        microkit_cothread_yield();

        /**
        If the code below get executed, then all the working threads are either blocked or finished.
        So the code below would send the result back to client through SDDF and do the cleanup for finished
        threads. After that, the main thread would block wait on new requests or server sending responses.
        **/
        /*
          This for loop check if there are threads finished and send the result back
        */
        for (uint16_t i = 1; i < FAT_THREAD_NUM; i++) {
            co_state_t state = microkit_cothread_query_state(request_pool[i].handle);
            if (state == cothread_not_active && request_pool[i].stat == INUSE) {
                fill_client_response(fs_queue_idx_empty(fs_completion_queue, fs_response_enqueued), &(request_pool[i]));
                fs_response_enqueued++;
                LOG_FATFS("FS enqueue response:status: %lu\n", request_pool[i].shared_data.status);
                request_pool[i].stat= FREE;
            }
        }

        /*
          This should pop the request from the command_queue to the thread pool to execute, if no new request is
          popped, we should exit the whole while loop.
        */
        new_request_popped = false;
        while (true) {
            microkit_cothread_ref_t index;
            // If there is space and we do not know the size of the queue, get it now
            if (queue_size_init == false && microkit_cothread_free_handle_available(&index)) {
                LOG_FATFS("here1, fs_command_queue %p, queue->head 0x%lx, queue->tail 0x%lx\n", fs_command_queue, &fs_command_queue->head, &fs_command_queue->tail);
                command_queue_size = fs_queue_length_consumer(fs_command_queue);
                LOG_FATFS("here2asjdasjdi\n");
                LOG_FATFS("fs_completion_queue %p, queue->head 0x%lx, queue->tail 0x%lx\n", fs_completion_queue, &fs_completion_queue->head, &fs_completion_queue->tail);
                completion_queue_size = fs_queue_length_producer(fs_completion_queue);
                LOG_FATFS("here3\n");
                queue_size_init = true;
            }

            // We only dequeue the request if there is a free slot in the thread pool
            if (!microkit_cothread_free_handle_available(&index) || command_queue_size == 0
                  || completion_queue_size == FS_QUEUE_CAPACITY) {
               break;
            }

            // Copy the request to local buffer first to avoid modification from client side
            fs_msg_t client_req = *fs_queue_idx_filled(fs_command_queue, fs_request_dequeued);

            fs_request_dequeued++;
            command_queue_size--;

            // For invalid request, dequeue but do not process
            if (client_req.cmd.type >= FS_NUM_COMMANDS) {
                LOG_FATFS("Wrong CMD type: %lu\n", client_req.cmd.type);
                continue;
            }

            // Get request from the head of the queue
            setup_request(index, &client_req);
            LOG_FATFS("FS dequeue request:CMD type: %lu\n", request_pool[index].cmd);

            request_pool[index].stat = INUSE;
            new_request_popped = true;
            // Dequeue one request from command queue and reserve a space in completion queue
            completion_queue_size++;
        }
    }
    // Publish the changes to the fs_queue, If there are replies to client or server, reply back here
    if (fs_request_dequeued) {
        fs_queue_publish_consumption(fs_command_queue, fs_request_dequeued);
    }
    if (fs_response_enqueued) {
        LOG_FATFS("FS notify client\n");
        fs_queue_publish_production(fs_completion_queue, fs_response_enqueued);
        microkit_notify(CLIENT_CH);
    }
    if (blk_request_pushed) {
        LOG_FATFS("FS notify driver\n");
        microkit_notify(VIRT_CH);
        blk_request_pushed = false;
    }
}
