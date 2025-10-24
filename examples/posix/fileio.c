/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <lions/posix/posix.h>

#include <microkit.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/timer/config.h>
#include <sddf/network/config.h>
#include <sddf/network/queue.h>
#include <lions/fs/helpers.h>
#include <lions/fs/config.h>
#include <lions/fs/protocol.h>
#include <libmicrokitco.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;
__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

serial_queue_handle_t serial_tx_queue_handle;
serial_queue_handle_t serial_rx_queue_handle;

net_queue_handle_t net_tx_handle;
net_queue_handle_t net_rx_handle;

bool fs_enabled;
bool serial_rx_enabled;
bool net_enabled;

#define LIBC_COTHREAD_STACK_SIZE 0x10000

static char libc_cothread_stack[LIBC_COTHREAD_STACK_SIZE];
static co_control_t co_controller_mem;

static void blocking_wait(microkit_channel ch) { microkit_cothread_wait_on_channel(ch); }

void notified(microkit_channel ch) {
    fs_process_completions(NULL);
    microkit_cothread_recv_ntfn(ch);
}

void cont() {
    libc_init(NULL);

    if (fs_enabled) {
        fs_cmpl_t completion;
        int err = fs_command_blocking(&completion, (fs_cmd_t){.type = FS_CMD_INITIALISE});
        if (err || completion.status != FS_STATUS_SUCCESS) {
            printf("FILEIO|ERROR: Failed to mount\n");
            return;
        }

        printf("FILEIO|INFO: fs init\n");

        FILE *file = fopen("hello.txt", "w+");
        assert(file);

        int fd = fileno(file);

        printf("FILEIO|INFO: opened on fd %d\n", fd);

        char *hello = "hello there";
        int size = fwrite(hello, sizeof(char), strlen(hello), file);
        fflush(file);

        printf("FILEIO|INFO: wrote %d bytes\n", size);

        fseek(file, 0, SEEK_END);
        long pos = ftell(file);
        rewind(file);
        printf("FILEIO|INFO: file size is %ld\n", pos);

        char buf[20] = {0};
        size_t bytes_read = fread(buf, sizeof(char), pos, file);
        printf("FILEIO|INFO: bytes_read: %d, buf: %s\n", bytes_read, buf);

        printf("FILEIO|INFO: doing fseek\n");
        err = fseek(file, 100, SEEK_CUR);
        assert(!err);

        pos = ftell(file);

        char *how = "how are you";
        size = fwrite(how, sizeof(char), strlen(how), file);
        fflush(file);

        printf("FILEIO|INFO: wrote %d bytes at pos %ld\n", size, pos);

        int closed = fclose(file);
        assert(closed == 0);

        printf("FILEIO|INFO: closed file\n");

        file = fopen("hello.txt", "r");
        assert(file);

        fd = fileno(file);

        printf("FILEIO|INFO: opened on fd %d\n", fd);
        fseek(file, pos, SEEK_SET);

        memset(buf, 0, sizeof(buf));
        bytes_read = fread(buf, sizeof(char), strlen(how), file);
        printf("FILEIO|INFO: bytes_read: %d, buf: %s\n", bytes_read, buf);

        closed = fclose(file);
        assert(closed == 0);

        const char *dir = "example";
        //rwxr-xr-x
        mode_t mode = 0755;

        if (mkdir(dir, mode) == 0) {
            printf("FILEIO|INFO: Directory '%s' created successfully.\n", dir);
        } else {
            printf("FILEIO|ERROR: Error creating directory\n");
        }

        int dirfd = open(dir, O_DIRECTORY | O_RDONLY);

        printf("FILEIO|INFO: opened directory %s with fd %d\n", dir, dirfd);
        const char *dir2 = "subdir";

        if (mkdirat(dirfd, dir2, mode) == 0) {
            printf("FILEIO|INFO: Subdirectory '%s' created successfully in directory '%s'\n", dir2, dir);
        } else {
            printf("FILEIO|ERROR: Error creating subdirectory\n");
        }

        char *example = "example.txt";
        if ((fd = openat(dirfd, example, O_RDWR | O_CREAT)) > 0) {
            printf("FILEIO|INFO: opened %s at %s with fd %d\n", example, dir, fd);
            int written = write(fd, "hello example", 13);
            printf("FILEIO|INFO: wrote %d\n", written);
            assert(close(fd) == 0);

            char path[40] = "/";
            strcat(path, dir);
            strcat(path, "/");
            strcat(path, example);

            printf("FILEIO|INFO: opening %s\n", path);

            if ((fd = open(path, O_RDONLY)) > 0) {
                printf("FILEIO|INFO: opened %s absolute with fd %d\n", path, fd);
                memset(buf, 0, sizeof(buf));
                bytes_read = read(fd, buf, sizeof(buf));
                printf("FILEIO|INFO: bytes_read: %d, buf: %s\n", bytes_read, buf);
                close(fd);
            }
        }

        close(dirfd);
    }
}

void init() {
    assert(serial_config_check_magic(&serial_config));
    assert(timer_config_check_magic(&timer_config));
    fs_enabled = fs_config_check_magic(&fs_config);
    net_enabled = net_config_check_magic(&net_config);
    serial_rx_enabled = (serial_config.rx.queue.vaddr != NULL);

    if (serial_rx_enabled) {
        serial_queue_init(&serial_rx_queue_handle, serial_config.rx.queue.vaddr, serial_config.rx.data.size,
                          serial_config.rx.data.vaddr);
    }
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size,
                      serial_config.tx.data.vaddr);

    if (fs_enabled) {
        fs_set_blocking_wait(blocking_wait);
        fs_command_queue = fs_config.server.command_queue.vaddr;
        fs_completion_queue = fs_config.server.completion_queue.vaddr;
        fs_share = fs_config.server.share.vaddr;
    }

    stack_ptrs_arg_array_t costacks = {(uintptr_t)libc_cothread_stack};
    microkit_cothread_init(&co_controller_mem, LIBC_COTHREAD_STACK_SIZE, costacks);

    if (microkit_cothread_spawn(cont, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        printf("FILEIO|ERROR: Cannot initialise cothread\n");
        assert(false);
    };

    microkit_cothread_yield();
}
