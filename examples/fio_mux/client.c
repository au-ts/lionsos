/* Copyright 2026, UNSW */
/* SPDX-License-Identifier: BSD-2-Clause */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libmicrokitco.h>
#include <microkit.h>
#include <sddf/serial/config.h>
#include <sddf/serial/queue.h>
#include <sddf/timer/config.h>

#include <lions/fs/config.h>
#include <lions/fs/helpers.h>
#include <lions/fs/protocol.h>
#include <lions/posix/posix.h>

/* Given at fio_mux.mk */
#ifndef CLIENT_ID
#error CLIENT_ID must be defined
#endif

#define ITERATIONS 5
#define STACK_SIZE 0x10000

__attribute__((section(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((section(".fs_client_config"))) fs_client_config_t fs_config;

// @gt ?? is this dummy, as required by LionsOS libc?
timer_client_config_t timer_config;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;
serial_queue_handle_t serial_tx_queue_handle;
serial_queue_handle_t serial_rx_queue_handle;

static char cothread_stack[STACK_SIZE];
static char libc_heap[0x100000];
static co_control_t co_controller;

static void blocking_wait(microkit_channel ch)
{
    microkit_cothread_wait_on_channel(ch);
}

/* Write contents (message) to a file at "path" */
static bool write_message(const char *path, const char *message)
{
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        return false;
    }
    size_t len = strlen(message);
    bool ok = write(fd, message, len) == (ssize_t)len;
    return close(fd) == 0 && ok;
}

/* Read contents (message) from a file at "path" */
static bool read_message(const char *path, char *buffer, size_t size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    ssize_t n = read(fd, buffer, size - 1);
    if (n < 0) {
        // Keep errno for the failed "read"
        int read_errno = errno;
        close(fd);
        errno = read_errno;
        return false;
    }
    if (close(fd) < 0) {
        return false;
    }
    buffer[n] = '\0';
    return true;
}

#if CLIENT_ID == 0
static bool open_server_file(const char *path, uint64_t *fd)
{
    ptrdiff_t path_buffer;
    if (fs_buffer_allocate(&path_buffer)) {
        return false;
    }

    size_t path_len = strlen(path);
    memcpy(fs_buffer_ptr(path_buffer), path, path_len);
    fs_cmpl_t completion;
    int err = fs_command_blocking(&completion, (fs_cmd_t) {
        .type = FS_CMD_FILE_OPEN,
        .params.file_open = {
            .path = { .offset = path_buffer, .size = path_len },
            .flags = FS_OPEN_FLAGS_READ_WRITE | FS_OPEN_FLAGS_CREATE,
        },
    });
    fs_buffer_free(path_buffer);
    if (err || completion.status != FS_STATUS_SUCCESS) {
        return false;
    }

    *fd = completion.data.file_open.fd;
    return true;
}
#endif

static bool file_size_status(uint64_t fd, uint64_t expected_status)
{
    fs_cmpl_t completion;
    int err = fs_command_blocking(&completion, (fs_cmd_t) {
        .type = FS_CMD_FILE_SIZE,
        .params.file_size = { .fd = fd },
    });
    return !err && completion.status == expected_status;
}

#if CLIENT_ID == 0
static bool close_server_file(uint64_t fd)
{
    fs_cmpl_t completion;
    int err = fs_command_blocking(&completion, (fs_cmd_t) {
        .type = FS_CMD_FILE_CLOSE,
        .params.file_close = { .fd = fd },
    });
    return !err && completion.status == FS_STATUS_SUCCESS;
}
#endif

static bool test_fd_ownership(void)
{
    char message[32];
#if CLIENT_ID == 0
    uint64_t fd;
    if (!open_server_file("/fd-owner", &fd)) {
        return false;
    }
    snprintf(message, sizeof(message), "%lu", fd);
    if (!write_message("/fd-value", message)) {
        close_server_file(fd);
        return false;
    }
    do {
        message[0] = '\0';
    } while (!read_message("/fd-tested", message, sizeof(message)) || strcmp(message, "done") != 0);

    bool ok = file_size_status(fd, FS_STATUS_SUCCESS);
    return close_server_file(fd) && ok;
#else
    do {
        message[0] = '\0';
    } while (!read_message("/fd-value", message, sizeof(message)));

    uint64_t fd = strtoull(message, NULL, 10);
    // Server will reject the fabricated fd...
    if (!file_size_status(fd, FS_STATUS_INVALID_FD)) {
        return false;
    }
    return write_message("/fd-tested", "done");
#endif
}

static void run_client(void)
{
    libc_init(NULL, libc_heap, sizeof(libc_heap));
    fs_cmpl_t completion;
    if (fs_command_blocking(&completion, (fs_cmd_t){ .type = FS_CMD_INITIALISE }) ||
        completion.status != FS_STATUS_SUCCESS) {
        printf("FIO_MUX|client%d|FAIL|mount\n", CLIENT_ID);
        return;
    }

    if (!test_fd_ownership()) {
        printf("FIO_MUX|client%d|FAIL|fd-ownership\n", CLIENT_ID);
        return;
    }
    printf("FIO_MUX|client%d|fd-ownership=PASS\n", CLIENT_ID);

    char expected[32];
    char received[32];
    char ping_path[32];
    char pong_path[32];
    for (unsigned round = 0; round < ITERATIONS; round++) {
        snprintf(ping_path, sizeof(ping_path), "/ping-%u", round);
        snprintf(pong_path, sizeof(pong_path), "/pong-%u", round);
#if CLIENT_ID == 0
        // Client 0 writes "ping-x" to /ping-x
        snprintf(expected, sizeof(expected), "ping-%u", round);
        if (!write_message(ping_path, expected)) {
            goto fail;
        }
        // Client 0 tries blocking read "pong-x" from /pong-x
        snprintf(expected, sizeof(expected), "pong-%u", round);
        do {
            received[0] = '\0';
        } while (!read_message(pong_path, received, sizeof(received)) || strcmp(received, expected) != 0);
#else
        // Client 1 tries blocking read "ping-x" from /ping-x
        snprintf(expected, sizeof(expected), "ping-%u", round);
        do {
            received[0] = '\0';
        } while (!read_message(ping_path, received, sizeof(received)) || strcmp(received, expected) != 0);

        // Once read, Client 1 writes "pong-x" to /pong-x
        snprintf(expected, sizeof(expected), "pong-%u", round);
        if (!write_message(pong_path, expected)) {
            goto fail;
        }
#endif
        printf("FIO_MUX|client%d|round=%u\n", CLIENT_ID, round);
    }
    printf("FIO_MUX|client%d|PASS\n", CLIENT_ID);
    return;

fail:
    printf("FIO_MUX|client%d|FAIL|errno=%d\n", CLIENT_ID, errno);
}

void notified(microkit_channel ch)
{
    fs_process_completions(NULL);
    microkit_cothread_recv_ntfn(ch);
}

void init(void)
{
    assert(serial_config_check_magic(&serial_config));
    assert(fs_config_check_magic(&fs_config));
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr,
                      serial_config.tx.data.size, serial_config.tx.data.vaddr);
    fs_set_blocking_wait(blocking_wait);
    fs_command_queue = fs_config.server.command_queue.vaddr;
    fs_completion_queue = fs_config.server.completion_queue.vaddr;
    fs_share = fs_config.server.share.vaddr;

    stack_ptrs_arg_array_t stacks = { (uintptr_t)cothread_stack };
    microkit_cothread_init(&co_controller, STACK_SIZE, stacks);
    assert(microkit_cothread_spawn(run_client, NULL) != LIBMICROKITCO_NULL_HANDLE);
    microkit_cothread_yield();
}
