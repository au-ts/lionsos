/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef _PAGER_H
#define _PAGER_H

#include <stdint.h>

#include <lions/pager/config.h>


// Slab tuning for frame gzp ips and free list.
// how many list holds and retyped at a time.
#define BUFFERS_SIZE 200000
#define REFILL_SIZE 64

// batched untyped retype.
#define RETYPE_BATCH 256

// vspace for pager in root cspace
#define PAGER_OWN_VSPACE_SLOT 3

#ifndef INIT_FRAMES
#define INIT_FRAMES BUFFERS_SIZE
#endif
#define INIT_IPS 1024
#define INIT_GZP 20000



 // below written to pager.elf by microkit
extern uint32_t vspaces[PAGER_MAX_CLIENTS];
extern uint32_t elf_caps[PAGER_MAX_CLIENTS][PAGER_MAX_ELF_FRAMES];
extern uint32_t elf_sizes[PAGER_MAX_CLIENTS];

/* Serialised by sdfgen's LionsOs.Pager into the .pager_server_config section. */
extern pager_server_config_t pager_config;

#endif
