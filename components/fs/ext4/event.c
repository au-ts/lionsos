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

#include <lions/fs/config.h>
#include <lions/fs/protocol.h>
#include <lions/posix/posix.h>

#include "decl.h"

__attribute__((__section__(".fs_server_config"))) fs_server_config_t fs_config;
__attribute__((__section__(".blk_client_config"))) blk_client_config_t blk_config;

/* Unused - needed for linking libc for now */
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/timer/config.h>

serial_queue_handle_t serial_tx_queue_handle;
serial_client_config_t serial_config;
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

/* 4 stack declarations kept here for compatibility with LionsOs.FileSystem.Fat for now. */
uint64_t worker_thread_stack_one;
uint64_t worker_thread_stack_two;
uint64_t worker_thread_stack_three;
uint64_t worker_thread_stack_four;

uint64_t max_cluster_size;

/* Whether at least one block request was enqueued in this cycle. */
bool blk_request_pushed = false;

typedef enum { FREE, INUSE } space_status_t;

typedef struct ext4_request {
    uint64_t cmd;
    co_data_t shared_data;
    uint64_t request_id;
    microkit_cothread_ref_t handle;
    space_status_t stat;
} ext4_request_t;

/* This operation list must remain consistent with the FS protocol enum. */
static void (*operation_functions[])(void) = {
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

static ext4_request_t request_pool[EXT4_THREAD_NUM];

static void fill_client_response(fs_msg_t *message, const ext4_request_t *finished_request) {
    message->cmpl.id = finished_request->request_id;
    message->cmpl.status = finished_request->shared_data.status;
    message->cmpl.data = finished_request->shared_data.result;
}

static bool setup_request(int32_t index, fs_msg_t *message) {
    request_pool[index].request_id = message->cmd.id;
    request_pool[index].cmd = message->cmd.type;
    request_pool[index].shared_data.params = message->cmd.params;
    request_pool[index].stat = FREE;

    void (*func)(void) = operation_functions[request_pool[index].cmd];
    request_pool[index].handle = microkit_cothread_spawn(func, &request_pool[index].shared_data);
    if (request_pool[index].handle == LIBMICROKITCO_NULL_HANDLE) {
        request_pool[index].shared_data.status = FS_STATUS_ALLOCATION_ERROR;
        return false;
    }

    return true;
}

void init(void) {
    assert(fs_config_check_magic(&fs_config));
    assert(blk_config_check_magic(&blk_config));

    /* Needed for malloc */
    libc_init(NULL);

    assert(blk_config.virt.num_buffers >= EXT4_WORKER_THREAD_NUM);

    max_cluster_size = blk_config.data.size / EXT4_WORKER_THREAD_NUM;

    fs_command_queue = fs_config.client.command_queue.vaddr;
    fs_completion_queue = fs_config.client.completion_queue.vaddr;
    fs_share = fs_config.client.share.vaddr;

    blk_data = blk_config.data.vaddr;
    blk_storage_info = blk_config.virt.storage_info.vaddr;
    blk_queue_init(&blk_queue, blk_config.virt.req_queue.vaddr, blk_config.virt.resp_queue.vaddr,
                   blk_config.virt.num_buffers);

    while (!blk_storage_is_ready(blk_storage_info)) {
    }

    stack_ptrs_arg_array_t costacks = {
        worker_thread_stack_one,
    };

    microkit_cothread_init(&co_controller_mem, EXT4_WORKER_THREAD_STACKSIZE, costacks);
    for (uint32_t i = 0; i < EXT4_THREAD_NUM; i++) {
        microkit_cothread_semaphore_init(&sem[i]);
    }
}

void notified(microkit_channel ch) {
    LOG_EXT4FS("Notification received on channel: %d\n", ch);
    if (ch != fs_config.client.id && ch != blk_config.virt.id) {
        LOG_EXT4FS("Unknown channel: %d\n", ch);
        return;
    }

    bool new_request_popped = true;
    bool queue_size_init = false;

    uint64_t command_queue_size = 0;
    uint64_t completion_queue_size = 0;

    uint32_t fs_request_dequeued = 0;
    uint32_t fs_response_enqueued = 0;

    while (new_request_popped) {
        blk_resp_status_t status;
        uint16_t success_count;
        uint32_t id;
        uint32_t len = blk_queue_length_resp(&blk_queue);

        while (len > 0) {
            int err = blk_dequeue_resp(&blk_queue, &status, &success_count, &id);
            assert(!err);

            LOG_EXT4FS("blk_dequeue_resp: status: %d success_count: %d ID: %d\n", status, success_count, id);

            microkit_cothread_set_arg(request_pool[id].handle, (void *)(uintptr_t)status);
            microkit_cothread_semaphore_signal(&sem[request_pool[id].handle]);
            len--;
        }

        microkit_cothread_yield();

        for (uint16_t i = 1; i < EXT4_THREAD_NUM; i++) {
            co_state_t state = microkit_cothread_query_state(request_pool[i].handle);
            if (state == cothread_not_active && request_pool[i].stat == INUSE) {
                fill_client_response(fs_queue_idx_empty(fs_completion_queue, fs_response_enqueued), &request_pool[i]);
                fs_response_enqueued++;
                LOG_EXT4FS("FS enqueue response: status: %lu\n", request_pool[i].shared_data.status);
                request_pool[i].stat = FREE;
            }
        }

        new_request_popped = false;
        while (true) {
            microkit_cothread_ref_t index;

            if (!queue_size_init && microkit_cothread_free_handle_available(&index)) {
                command_queue_size = fs_queue_length_consumer(fs_command_queue);
                completion_queue_size = fs_queue_length_producer(fs_completion_queue);
                queue_size_init = true;
            }

            if (!microkit_cothread_free_handle_available(&index) || command_queue_size == 0
                || completion_queue_size == FS_QUEUE_CAPACITY) {
                break;
            }

            fs_msg_t client_req = *fs_queue_idx_filled(fs_command_queue, fs_request_dequeued);
            fs_request_dequeued++;
            command_queue_size--;

            if (client_req.cmd.type >= FS_NUM_COMMANDS) {
                LOG_EXT4FS("Invalid CMD type: %lu\n", client_req.cmd.type);
                continue;
            }

            if (!setup_request(index, &client_req)) {
                fill_client_response(fs_queue_idx_empty(fs_completion_queue, fs_response_enqueued),
                                     &request_pool[index]);
                fs_response_enqueued++;
                completion_queue_size++;
                continue;
            }

            request_pool[index].stat = INUSE;
            LOG_EXT4FS("FS dequeue request: CMD type: %lu\n", request_pool[index].cmd);
            completion_queue_size++;
            new_request_popped = true;
        }
    }

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
