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
#define PAGER_SELF_EP_SLOT 1
/* Slots of the pager_caps CNode the pager and sdfgen's LionsOs.Pager agree on. A peer is
 * a PD a forked process is connected to, such as a serial or network virtualiser. */
#define PAGER_SINK_NTFN_SLOT 2
#define PAGER_PEER_CNODE_SLOT(peer) (4 + 2 * (peer))
#define PAGER_PEER_NTFN_SLOT(peer) (5 + 2 * (peer))

// The connections a forked process gets its own copy of. Must match Pager.ConnKind in
// microkit_sdf_gen src/data.zig, as must the PAGER_MAX_* below.
#define PAGER_CONN_SERIAL_TX 0
#define PAGER_CONN_SERIAL_RX 1
#define PAGER_CONN_NET_TX 2
#define PAGER_CONN_NET_RX 3
#define PAGER_MAX_CONN_REGIONS 3
#define PAGER_MAX_CLIENT_CONNS 4
#define PAGER_MAX_PEERS 16
#define PAGER_MAX_SPARES 32

// A connection a client has, which a process forked from it gets a spare copy of.
typedef struct pager_conn_template {
    uint8_t kind;
    // The channel the client, and every process forked from it, signals the peer on.
    uint8_t client_id;
    uint8_t num_regions;
    // Where the client maps the connection's regions, in the order its config lists them.
    region_resource_t regions[PAGER_MAX_CONN_REGIONS];
} pager_conn_template_t;

// A connection the peer was configured with at build time that no PD is on the other end
// of, for the pager to give to a forked process.
typedef struct pager_spare_conn {
    uint8_t kind;
    // Index of the peer PD, see PAGER_PEER_*_SLOT.
    uint8_t peer;
    // The channel the peer signals this connection on.
    uint8_t peer_id;
    uint8_t num_regions;
    // For PAGER_CONN_NET_RX, the MAC address the virtualiser routes to this connection.
    uint8_t mac_addr[6];
    // Where each region's frames start in connection_frames, and how many there are.
    uint32_t frames[PAGER_MAX_CONN_REGIONS];
    uint32_t num_frames[PAGER_MAX_CONN_REGIONS];
} pager_spare_conn_t;

// represents cnodes inside pager cspace
typedef struct pager_cnode_resource {
    uint8_t slot;
    uint8_t size_bits;
} pager_cnode_resource_t;

typedef struct pager_client_resource {
    // PPC channel for pager endpoint
    uint8_t id;
    uint8_t fault_id;
    uint8_t client_id;
    // The client's scheduling parameters, which the processes forked from it inherit.
    uint8_t priority;
    uintptr_t mmap_base;
    uintptr_t brk_base;
    // In microseconds, as the SDF gives them.
    uint64_t budget;
    uint64_t period;
    uint8_t num_conns;
    pager_conn_template_t conns[PAGER_MAX_CLIENT_CONNS];
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
    // Caps the Microkit tool hands the pager for the processes it creates at runtime.
    pager_cnode_resource_t pager_caps;
    // Frame caps of every spare connection's regions.
    pager_cnode_resource_t connection_frames;
    uint8_t sched_control;
    uint8_t num_clients;
    pager_client_resource_t clients[PAGER_MAX_CLIENTS];
    uint8_t num_peers;
    uint8_t num_spares;
    pager_spare_conn_t spares[PAGER_MAX_SPARES];
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
