/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once

#include <microkit.h>
#include <stdbool.h>
#include <stdint.h>
#include <sddf/resources/common.h>

#define LIONS_FS_MAGIC_LEN 8
#ifdef FS_MULTIPLEXED
static char LIONS_FS_MAGIC[LIONS_FS_MAGIC_LEN] = { 'L', 'i', 'o', 'n', 's', 'M', 'u', 0x1 };
#else
static char LIONS_FS_MAGIC[LIONS_FS_MAGIC_LEN] = { 'L', 'i', 'o', 'n', 's', 'O', 'S', 0x1 };
#endif

typedef struct fs_connection_resource {
    region_resource_t command_queue;
    region_resource_t completion_queue;
    region_resource_t share;
    uint16_t queue_len;
    uint8_t id;
} fs_connection_resource_t;

#ifdef FS_MULTIPLEXED

#define FS_MULTIPLEXER_MAX_CLIENTS 64

typedef struct fs_multiplexer_config {
    char magic[LIONS_FS_MAGIC_LEN];
    fs_connection_resource_t server;
    fs_connection_resource_t clients[FS_MULTIPLEXER_MAX_CLIENTS];
    uint64_t num_clients;
} fs_multiplexer_config_t;

typedef struct fs_server_config {
    char magic[LIONS_FS_MAGIC_LEN];
    fs_connection_resource_t multiplexer;
    uintptr_t client_shares[FS_MULTIPLEXER_MAX_CLIENTS];
    uint64_t num_clients;
} fs_server_config_t;

#else

typedef struct fs_server_config {
    char magic[LIONS_FS_MAGIC_LEN];
    fs_connection_resource_t client;
} fs_server_config_t;

#endif

typedef struct fs_client_config {
    char magic[LIONS_FS_MAGIC_LEN];
    fs_connection_resource_t server;
} fs_client_config_t;

static inline bool fs_config_check_magic(void *config)
{
    char *magic = (char *)config;
    for (int i = 0; i < LIONS_FS_MAGIC_LEN; i++) {
        if (magic[i] != LIONS_FS_MAGIC[i]) {
            return false;
        }
    }

    return true;
}
