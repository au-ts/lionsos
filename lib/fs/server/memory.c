/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <string.h>

#include <lions/fs/protocol.h>
#include <lions/fs/server.h>

void *fs_get_client_buffer(char *client_share, size_t client_share_size, fs_buffer_t buf) {
    if (buf.offset >= client_share_size
        || buf.size > client_share_size - buf.offset
        || buf.size == 0) {
        return NULL;
    }
    return (void *)(client_share + buf.offset);
}

int fs_copy_client_path(char *dest, char *client_share, size_t client_share_size, fs_buffer_t buf) {
    char *client_buf = fs_get_client_buffer(client_share, client_share_size, buf);
    if (client_buf == NULL || buf.size > FS_MAX_PATH_LENGTH) {
        return -1;
    }

    memcpy(dest, client_buf, buf.size);
    dest[buf.size] = '\0';
    return 0;
}
