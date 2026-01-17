/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <lions/posix/fd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>

#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>

extern serial_queue_handle_t serial_tx_queue_handle;
extern serial_client_config_t serial_config;

static ssize_t console_write(const void *data, size_t count, int fd) {
    char *src = (char *)data;
    uint32_t sent = 0;
    while (sent < count) {
        char *nl = memchr(src, '\n', count - sent);
        uint32_t stop = (nl == NULL) ? count - sent : nl - src;
        /* Enqueue to the first '\n' character, end of string
           or until queue is full */
        uint32_t enq = serial_enqueue_batch(&serial_tx_queue_handle, stop, src);
        sent += enq;
        if (sent == count || serial_queue_free(&serial_tx_queue_handle) < 2) {
            break;
        }

        /* Append a '\r' character before a '\n' character */
        serial_enqueue_batch(&serial_tx_queue_handle, 2, "\r\n");
        sent += 1;
        src += enq + 1;
    }

    if (sent) {
        microkit_notify(serial_config.tx.id);
    }

    return sent;
}

// Note we initialise console FDs here
static bool fd_active[MAX_FDS] = { [STDIN_FD] = true, [STDOUT_FD] = true, [STDERR_FD] = true };
static fd_entry_t fd_table[MAX_FDS] = { [STDIN_FD] =
                                            (fd_entry_t) {
                                                .flags = O_RDONLY,
                                            },
                                        [STDOUT_FD] =
                                            (fd_entry_t) {
                                                .write = console_write,
                                                .flags = O_WRONLY,
                                            },
                                        [STDERR_FD] = (fd_entry_t) {
                                            .write = console_write,
                                            .flags = O_WRONLY,
                                        } };

static inline bool posix_fd_is_valid(int fd) { return fd >= 0 && fd < MAX_FDS && fd_active[fd]; }

fd_entry_t *posix_fd_entry(int fd) {
    if (!posix_fd_is_valid(fd)) {
        return NULL;
    }
    return &fd_table[fd];
}

int posix_fd_allocate() {
    for (int i = 0; i < MAX_FDS; i++) {
        if (!fd_active[i]) {
            fd_active[i] = true;
            return i;
        }
    }
    return -1;
}

int posix_fd_deallocate(int fd) {
    if (!posix_fd_is_valid(fd)) {
        return -1;
    }

    fd_active[fd] = false;
    memset(&fd_table[fd], 0, sizeof(fd_entry_t));

    return 0;
}

fd_entry_t *posix_fd_entry_allocate(int fd) {
    if (fd < 0 || fd >= MAX_FDS) {
        return NULL;
    }

    if (fd_active[fd]) {
        return NULL;
    }

    fd_active[fd] = true;
    memset(&fd_table[fd], 0, sizeof(fd_entry_t));

    return &fd_table[fd];
}
