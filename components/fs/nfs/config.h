/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#define LIONS_NFS_SERVER_URL_LEN_MAX 4096
#define LIONS_NFS_EXPORT_PATH_LEN_MAX 4096

#define LIONS_NFS_MAGIC_LEN 8
static char LIONS_NFS_MAGIC[LIONS_NFS_MAGIC_LEN] = { 'L', 'i', 'o', 'n', 's', 'O', 'S', 0x2 };

typedef struct nfs_config {
    char magic[LIONS_NFS_MAGIC_LEN];
    char server[LIONS_NFS_SERVER_URL_LEN_MAX];
    char export[LIONS_NFS_EXPORT_PATH_LEN_MAX];
} nfs_config_t;

static bool nfs_config_check_magic(nfs_config_t *config)
{
    for (int i = 0; i < LIONS_NFS_MAGIC_LEN; i++) {
        if (config->magic[i] != LIONS_NFS_MAGIC[i]) {
            return false;
        }
    }

    return true;
}
