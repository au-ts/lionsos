/*
 * Copyright 2026, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once

#include <microkit.h>
#include <stdbool.h>
#include <stdint.h>
#include <sddf/resources/common.h>

// copied from other sddf stuff
#define LIONS_PAGER_MAGIC_LEN 8
static char LIONS_PAGER_MAGIC[LIONS_PAGER_MAGIC_LEN] = { 'L', 'i', 'o', 'n', 's', 'O', 'S', 0x3 };

// MAX CLIENTS must match microkit_sdf_gen src/data.zig and MAX_FAULT_CLIENTS in microkit
#define PAGER_MAX_CLIENTS 10
// Must match MAX_CLIENT_ELF_FRAMES in microkit
#define PAGER_MAX_ELF_FRAMES 1500

// represents cnodes inside pager cspace
typedef struct pager_cnode_resource {
    uint8_t slot;
    uint8_t size_bits;
} pager_cnode_resource_t;

typedef struct pager_client_resource {
    // PPC channel for pager endpoint
    uint8_t id;
    // microkit_child is fault_id
    uint8_t fault_id;
    uintptr_t mmap_base;
    uintptr_t brk_base;
} pager_client_resource_t;

typedef struct pager_server_config {
    char magic[LIONS_PAGER_MAGIC_LEN];
    // folio metadata and shadow page tables in memory
    region_resource_t memory;
    region_resource_t bootinfo;
    pager_cnode_resource_t untypeds;
    pager_cnode_resource_t frames;
    pager_cnode_resource_t paging_structures;
    pager_cnode_resource_t zero_page_copies;
    pager_cnode_resource_t process_cspaces;
    pager_cnode_resource_t elf_caps;
    pager_cnode_resource_t frame_copies;
    uint8_t num_clients;
    pager_client_resource_t clients[PAGER_MAX_CLIENTS];
} pager_server_config_t;

typedef struct pager_client_config {
    char magic[LIONS_PAGER_MAGIC_LEN];
    // PPC channel to pager
    uint8_t id;
    uintptr_t mmap_base;
    uintptr_t brk_base;
} pager_client_config_t;

static inline bool pager_config_check_magic(void *config)
{
    char *magic = (char *)config;
    for (int i = 0; i < LIONS_PAGER_MAGIC_LEN; i++) {
        if (magic[i] != LIONS_PAGER_MAGIC[i]) {
            return false;
        }
    }

    return true;
}
