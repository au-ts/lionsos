#pragma once

#include <stdint.h>
#include <stddef.h>
#include <firewall_arp.h>
#include <string.h>

#define TABLE_SIZE 100  // Size of the hash table (can be adjusted)

typedef struct entry {
    uint32_t key;
    arp_entry_t value;
} entry_t;

typedef struct hashtable {
    entry_t entries[TABLE_SIZE];  // Array of entries
    uint8_t used[TABLE_SIZE];     // Array to track used slots
} hashtable_t;

// Simple hash function to map a uint32_t key to an index
uint32_t hash(uint32_t key) {
    return key % TABLE_SIZE;
}

// Initialize the hash table
void hashtable_init(hashtable_t *table) {
    for (size_t i = 0; i < TABLE_SIZE; i++) {
        table->used[i] = 0;  // Mark all slots as unused
    }
}

// Insert a key-value pair into the hash table
int hashtable_insert(hashtable_t *table, uint32_t key, arp_entry_t *value) {
    uint32_t index = hash(key);
    uint32_t original_index = index;

    // Linear probing to handle collisions
    while (table->used[index]) {
        if (table->entries[index].key == key) {
            // If key exists, overwrite the value
            memcpy(&table->entries[index].value, value, sizeof(arp_entry_t));
            return 0;
        }
        index = (index + 1) % TABLE_SIZE;
        if (index == original_index) {
            // The table is full
            return -1;
        }
    }

    // Insert the new entry
    table->entries[index].key = key;
    memcpy(&table->entries[index].value, value, sizeof(arp_entry_t));
    table->used[index] = 1;  // Mark this slot as used
    return 0;
}

// Search for a value by key in the hash table
int hashtable_search(hashtable_t *table, uint32_t key, arp_entry_t *value) {
    uint32_t index = hash(key);
    uint32_t original_index = index;

    while (table->used[index]) {
        if (table->entries[index].key == key) {
            memcpy(value, &table->entries[index].value, sizeof(arp_entry_t));
            return 0;
        }
        index = (index + 1) % TABLE_SIZE;
        if (index == original_index) {
            // Full cycle completed, element not found
            break;
        }
    }
    return -1;
}

// Remove a key-value pair from the hash table
int hashtable_remove(hashtable_t *table, uint32_t key) {
    uint32_t index = hash(key);
    uint32_t original_index = index;

    while (table->used[index]) {
        if (table->entries[index].key == key) {
            // Mark the slot as unused and clear the entry
            table->used[index] = 0;
            return 0;
        }
        index = (index + 1) % TABLE_SIZE;
        if (index == original_index) {
            // Full cycle completed, element not found
            break;
        }
    }
    return -1;
}

bool hashtable_empty(hashtable_t *table) {
    for (int i = 0; i < TABLE_SIZE; i++) {
        if (table->used[i] == 1) {
            return false;
        }
    }
    return true;
}
