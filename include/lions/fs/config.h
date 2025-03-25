/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <microkit.h>
#include <stdbool.h>
#include <stdint.h>
#include <sddf/resources/common.h>

#define LIONS_FS_MAGIC_LEN 8
static char LIONS_FS_MAGIC[LIONS_FS_MAGIC_LEN] = { 'L', 'i', 'o', 'n', 's', 'O', 'S', 0x1 };

typedef struct fs_connection_resource {
    region_resource_t command_queue;
    region_resource_t completion_queue;
    region_resource_t share;
    uint16_t queue_len;
    uint8_t id;
} fs_connection_resource_t;

typedef struct fs_server_config {
    char magic[LIONS_FS_MAGIC_LEN];
    fs_connection_resource_t client;
} fs_server_config_t;

typedef struct fs_client_config {
    char magic[LIONS_FS_MAGIC_LEN];
    fs_connection_resource_t server;
} fs_client_config_t;

static bool fs_config_check_magic(void *config)
{
    char *magic = (char *)config;
    for (int i = 0; i < LIONS_FS_MAGIC_LEN; i++) {
        if (magic[i] != LIONS_FS_MAGIC[i]) {
            return false;
        }
    }

    return true;
}
