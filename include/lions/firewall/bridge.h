/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sddf/util/si_units.h>
#include <sddf/util/util.h>
#include <sddf/network/mac802.h>

#define FDB_AGEING_PERIOD_NS (10ULL * NS_IN_S)
#define FDB_EXPIRY_NS (300ULL * NS_IN_S)
#define FDB_CLEAN_PERIOD_NS (60ULL * NS_IN_S)
#define FDB_MAX_AGE (FDB_EXPIRY_NS / FDB_AGEING_PERIOD_NS)
#define FDB_AGES_PER_CLEAN (FDB_CLEAN_PERIOD_NS / FDB_AGEING_PERIOD_NS)

#define FW_FDB_NUM_ENTRIES 1024U

#define FW_FDB_STATE_EMPTY 0
#define FW_FDB_STATE_OCCUPIED 1
#define FW_FDB_STATE_STALE 2 // so that we can still do linear probing

typedef struct fw_fdb_entry {
    uint8_t mac[MAC802_BYTES];
    uint8_t port;
    uint8_t state;
    /* number of ticks since the entry was last seen */
    uint16_t age;
} fw_fdb_entry_t;

// TODO: Do I bother with this if the capacity is fixed above anyway?
// typedef struct fw_fdb {
//     fw_fdb_entry_t *entries;
//     uint16_t capacity;
// } fw_fdb_t;

static inline uint64_t fw_fdb_mac_hash(const uint8_t mac[MAC802_BYTES])
{
    uint64_t hash = 0;
    for (size_t i = 0; i < MAC802_BYTES; i++) {
        hash |= (uint64_t)mac[i] << (i * 8);
    }

    // SplitMix64 finaliser
    // Need a hash function that has all 48 bits impact the final output (avoid collisions for similar MAC addresses)
    // This isn't a cryptographic hash - good enough for a small internal network...
    hash ^= hash >> 30;
    hash *= 0xbf58476d1ce4e5b9ULL;
    hash ^= hash >> 27;
    hash *= 0x94d049bb133111ebULL;
    hash ^= hash >> 31;

    return hash;
}

static inline void fw_fdb_init(fw_fdb_entry_t *entries)
{
    for (size_t i = 0; i < FW_FDB_NUM_ENTRIES; i++) {
        entries[i].state = FW_FDB_STATE_EMPTY;
    }
}

static inline bool fw_fdb_lookup(fw_fdb_entry_t *entries, const uint8_t mac[MAC802_BYTES], uint8_t *port)
{
    uint64_t hash = fw_fdb_mac_hash(mac);
    size_t index = hash % FW_FDB_NUM_ENTRIES;

    for (size_t i = 0; i < FW_FDB_NUM_ENTRIES; i++) {
        fw_fdb_entry_t *entry = &entries[index];
        if (entry->state == FW_FDB_STATE_EMPTY) {
            return false;
        }

        if (entry->state == FW_FDB_STATE_OCCUPIED && memcmp(entry->mac, mac, MAC802_BYTES) == 0) {
            *port = entry->port;
            return true;
        }

        index = (index + 1) % FW_FDB_NUM_ENTRIES;
    }

    return false;
}

static inline bool fw_fdb_learn(fw_fdb_entry_t *entries, const uint8_t mac[MAC802_BYTES], uint8_t port)
{
    uint64_t hash = fw_fdb_mac_hash(mac);
    size_t index = hash % FW_FDB_NUM_ENTRIES;

    size_t free_slot = FW_FDB_NUM_ENTRIES;

    for (size_t i = 0; i < FW_FDB_NUM_ENTRIES; i++) {
        fw_fdb_entry_t *entry = &entries[index];
        if (entry->state == FW_FDB_STATE_EMPTY) {
            if (free_slot == FW_FDB_NUM_ENTRIES) {
                free_slot = index;
            }
            break; // Linear probing done
        }

        if (entry->state == FW_FDB_STATE_OCCUPIED && memcmp(entry->mac, mac, MAC802_BYTES) == 0) {
            // Update the port and reset the age
            entry->port = port;
            entry->age = 0;
            return true;
        }

        if (entry->state == FW_FDB_STATE_STALE && free_slot == FW_FDB_NUM_ENTRIES) {
            free_slot = index; // Remember just first stale entry to maybe reuse
        }

        index = (index + 1) % FW_FDB_NUM_ENTRIES;
    }

    if (free_slot == FW_FDB_NUM_ENTRIES) {
        return false; // FDB is full
    }

    fw_fdb_entry_t *entry = &entries[free_slot];
    memcpy(entry->mac, mac, MAC802_BYTES);
    entry->port = port;
    entry->state = FW_FDB_STATE_OCCUPIED;
    entry->age = 0;

    return true;
}

static inline void fw_fdb_age(fw_fdb_entry_t *entries)
{
    for (size_t i = 0; i < FW_FDB_NUM_ENTRIES; i++) {
        fw_fdb_entry_t *entry = &entries[i];
        if (entry->state == FW_FDB_STATE_OCCUPIED) {
            entry->age++;
            if (entry->age >= FDB_MAX_AGE) {
                entry->state = FW_FDB_STATE_STALE;
            }
        }
    }
}

static inline void fw_fdb_clean(fw_fdb_entry_t *entries, fw_fdb_entry_t *scratch)
{
    memset(scratch, 0, sizeof(fw_fdb_entry_t) * FW_FDB_NUM_ENTRIES);

    for (size_t i = 0; i < FW_FDB_NUM_ENTRIES; i++) {
        fw_fdb_entry_t *entry = &entries[i];
        if (entry->state != FW_FDB_STATE_OCCUPIED) {
            continue;
        }

        size_t index = fw_fdb_mac_hash(entry->mac) % FW_FDB_NUM_ENTRIES;
        while (scratch[index].state == FW_FDB_STATE_OCCUPIED) {
            index = (index + 1) % FW_FDB_NUM_ENTRIES;
        }

        scratch[index] = *entry;
    }

    memcpy(entries, scratch, sizeof(fw_fdb_entry_t) * FW_FDB_NUM_ENTRIES);
}
