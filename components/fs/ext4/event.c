/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <microkit.h>
#include <libmicrokitco.h>

#include <sddf/blk/config.h>
#include <sddf/blk/queue.h>
#include <sddf/blk/storage_info.h>
#include <sddf/serial/config.h>
#include <sddf/serial/queue.h>

#include <lions/fs/config.h>
#include <lions/fs/protocol.h>
#include <lions/posix/posix.h>

#include "decl.h"

__attribute__((__section__(".fs_server_config"))) fs_server_config_t fs_config;
__attribute__((__section__(".blk_client_config"))) blk_client_config_t blk_config;
__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;

serial_queue_handle_t serial_tx_queue_handle;

/* Unused - needed for linking libc for now */
#include <sddf/timer/config.h>
timer_client_config_t timer_config;
/* -- */

co_control_t co_controller_mem;
microkit_cothread_sem_t sem[EXT4_THREAD_NUM];

blk_queue_handle_t blk_queue;
blk_storage_info_t *blk_storage_info;
char *blk_data;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

// four stack declarations kept here for compatibility with LionsOs.FileSystem.Fat for now.
uint64_t worker_thread_stack_one;
uint64_t worker_thread_stack_two;
uint64_t worker_thread_stack_three;
uint64_t worker_thread_stack_four;

uint64_t max_cluster_size;

// Flag for determine if there are blk_requests pushed by the file system
// It is used to determine whether to notify the blk device driver
bool blk_request_pushed = false;

typedef enum { FREE, INUSE } space_status;

typedef struct FS_request {
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
    [FS_CMD_INITIALISE] = handle_initialise,
    [FS_CMD_DEINITIALISE] = handle_deinitialise,
    [FS_CMD_FILE_OPEN] = handle_file_open,
    [FS_CMD_FILE_CLOSE] = handle_file_close,
    [FS_CMD_STAT] = handle_stat,
    [FS_CMD_FILE_READ] = handle_file_read,
    [FS_CMD_FILE_WRITE] = handle_file_write,
    [FS_CMD_FILE_SIZE] = handle_file_size,
    [FS_CMD_RENAME] = handle_rename,
    [FS_CMD_FILE_REMOVE] = handle_file_remove,
    [FS_CMD_FILE_TRUNCATE] = handle_file_truncate,
    [FS_CMD_DIR_CREATE] = handle_dir_create,
    [FS_CMD_DIR_REMOVE] = handle_dir_remove,
    [FS_CMD_DIR_OPEN] = handle_dir_open,
    [FS_CMD_DIR_CLOSE] = handle_dir_close,
    [FS_CMD_FILE_SYNC] = handle_file_sync,
    [FS_CMD_DIR_READ] = handle_dir_read,
    [FS_CMD_DIR_SEEK] = handle_dir_seek,
    [FS_CMD_DIR_TELL] = handle_dir_tell,
    [FS_CMD_DIR_REWIND] = handle_dir_rewind,
};

static fs_request request_pool[EXT4_THREAD_NUM];

void fill_client_response(fs_msg_t *message, const fs_request *finished_request) {
    message->cmpl.id = finished_request->request_id;
    message->cmpl.status = finished_request->shared_data.status;
    message->cmpl.data = finished_request->shared_data.result;
}

// Setting up the request in the request_pool and push the request to the thread pool
bool setup_request(int32_t index, fs_msg_t *message) {
    request_pool[index].request_id = message->cmd.id;
    request_pool[index].cmd = message->cmd.type;
    request_pool[index].shared_data.params = message->cmd.params;
    request_pool[index].stat = FREE;

    void (*func)(void) = operation_functions[request_pool[index].cmd];
    request_pool[index].handle = microkit_cothread_spawn(func, &request_pool[index].shared_data);
    // Check for spawn failure so the caller can send an error response instead of silently dropping the request
    if (request_pool[index].handle == LIBMICROKITCO_NULL_HANDLE) {
        request_pool[index].shared_data.status = FS_STATUS_ALLOCATION_ERROR;
        return false;
    }

    return true;
}

// For debug
void print_sector_data(uint8_t *buffer, unsigned long size) {
    for (unsigned long i = 0; i < size; i++) {
        if (i % 16 == 0) {
            LOG_EXT4FS("\n%04lx  ", i); // Print the offset at the start of each line
        }
        LOG_EXT4FS("%02x ", buffer[i]); // Print each byte in hexadecimal
    }
    LOG_EXT4FS("\n");
}

void init(void) {
    assert(fs_config_check_magic(&fs_config));
    assert(blk_config_check_magic(&blk_config));
    assert(serial_config_check_magic(&serial_config));

    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size,
                      serial_config.tx.data.vaddr);

    // Needed for malloc
    libc_init(NULL);

    assert(blk_config.virt.num_buffers >= EXT4_WORKER_THREAD_NUM);

    max_cluster_size = blk_config.data.size / EXT4_WORKER_THREAD_NUM;
    fs_command_queue = fs_config.client.command_queue.vaddr;
    fs_completion_queue = fs_config.client.completion_queue.vaddr;
    fs_share = fs_config.client.share.vaddr;

    blk_data = blk_config.data.vaddr;

    blk_queue_init(&blk_queue, blk_config.virt.req_queue.vaddr, blk_config.virt.resp_queue.vaddr,
                   blk_config.virt.num_buffers);

    blk_storage_info = blk_config.virt.storage_info.vaddr;

    /* Wait for the the block device before doing anything else */
    while (!blk_storage_is_ready(blk_storage_info))
        ;

    /*
       This part of the code is for setting up the thread pool by
       assign stacks and size of the stack to the pool
    */
    stack_ptrs_arg_array_t costacks = {
        worker_thread_stack_one,
        // worker_thread_stack_two,
        // worker_thread_stack_three,
        // worker_thread_stack_four
    };

    // Init thread pool
    microkit_cothread_init(&co_controller_mem, EXT4_WORKER_THREAD_STACKSIZE, costacks);
    for (uint32_t i = 0; i < EXT4_THREAD_NUM; i++) {
        microkit_cothread_semaphore_init(&sem[i]);
    }
}

// The notified function requires careful management of the state of the file system
/*
  The filesystems should be blockwait for new message if and only if all of working
  threads are either free(no tasks assigned to them, no pending replies) or blocked in diskio.
  If the filesystem is blocked here and any working threads are free, then the fs_command_queue
  must also be empty.
*/
void notified(microkit_channel ch) {
    LOG_EXT4FS("Notification received on channel:: %d\n", ch);
    if (ch != fs_config.client.id && ch != blk_config.virt.id) {
        LOG_EXT4FS("Unknown channel:%d\n", ch);
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
            uint32_t len = blk_queue_length_resp(&blk_queue);

            while (len > 0) {
                // This id is the index to the request pool
                int err = blk_dequeue_resp(&blk_queue, &status, &success_count, &id);
                assert(!err);

                LOG_EXT4FS("blk_dequeue_resp: status: %d success_count: %d ID: %d\n", status, success_count, id);

                microkit_cothread_set_arg(request_pool[id].handle, (void *)(uintptr_t)status);
                microkit_cothread_semaphore_signal(&sem[request_pool[id].handle]);

                len--;
            }
        }

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
        for (uint16_t i = 1; i < EXT4_THREAD_NUM; i++) {
            co_state_t state = microkit_cothread_query_state(request_pool[i].handle);
            if (state == cothread_not_active && request_pool[i].stat == INUSE) {
                fill_client_response(fs_queue_idx_empty(fs_completion_queue, fs_response_enqueued), &(request_pool[i]));
                fs_response_enqueued++;
                LOG_EXT4FS("FS enqueue response:status: %lu\n", request_pool[i].shared_data.status);
                request_pool[i].stat = FREE;
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
                command_queue_size = fs_queue_length_consumer(fs_command_queue);
                completion_queue_size = fs_queue_length_producer(fs_completion_queue);
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
                LOG_EXT4FS("Wrong CMD type: %lu\n", client_req.cmd.type);
                continue;
            }

            // If setup failed, immediately send an error back to the client
            if (!setup_request(index, &client_req)) {
                fill_client_response(fs_queue_idx_empty(fs_completion_queue, fs_response_enqueued),
                                     &request_pool[index]);
                fs_response_enqueued++;
                completion_queue_size++;
                continue;
            }

            // Get request from the head of the queue
            LOG_EXT4FS("FS dequeue request:CMD type: %lu\n", request_pool[index].cmd);

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
        LOG_EXT4FS("FS notify client\n");
        fs_queue_publish_production(fs_completion_queue, fs_response_enqueued);
        microkit_notify(fs_config.client.id);
    }
    if (blk_request_pushed) {
        LOG_EXT4FS("FS notify block virt\n");
        microkit_notify(blk_config.virt.id);
        blk_request_pushed = false;
    }
}
