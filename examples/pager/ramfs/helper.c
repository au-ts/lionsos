#include "helper.h"
#include "ramfs_config.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
/**
 * cpio is where cpio file is mapped to ramfs.
 * cpio_size denotes size of cpio.
 */
uintptr_t cpio;
uint64_t cpio_size;

// a couple allocators to replace the mallocs. (just some slabs)
ramfs_file_entry_t file_entry_memory[SLAB_SIZE];
uint32_t file_entry_memory_idx = 0;

ramfs_file_entry_t *freed_file_entry_memory[SLAB_SIZE];
uint32_t freed_file_entry_memory_idx = 0;

ramfs_file_entry_t *alloc_file_entry() {
    if (freed_file_entry_memory_idx) {
        memset(freed_file_entry_memory[freed_file_entry_memory_idx], 0, sizeof(ramfs_file_entry_t));
        return freed_file_entry_memory[--freed_file_entry_memory_idx];
    }
    return file_entry_memory[file_entry_memory_idx++];
}



/**
 * I need to create an allocator with this memory region.
 */
void *fs_memory;
/**
 * Number of 4096-byte blocks in fs_memory.
 */
static size_t fs_num_blocks;

/**
 * One bit per block:
 *   0 = free
 *   1 = allocated
 */
static uint8_t *fs_bitmap;


/**
 * Initialise the allocator.
 *
 * bitmap_storage must be large enough to hold:
 *
 *     ceil(num_blocks / 8)
 *
 * bytes.
 */
void fs_allocator_init(void *memory,
                       size_t memory_size,
                       void *bitmap_storage)
{
    fs_memory = memory;
    fs_num_blocks = memory_size / RAMFS_BLOCK_SIZE;
    fs_bitmap = bitmap_storage;

    memset(fs_bitmap, 0, (fs_num_blocks + 7) / 8);
}


static inline bool block_is_allocated(size_t block)
{
    return fs_bitmap[block / 8] & (1u << (block % 8));
}


static inline void block_set(size_t block)
{
    fs_bitmap[block / 8] |= (1u << (block % 8));
}


static inline void block_clear(size_t block)
{
    fs_bitmap[block / 8] &= ~(1u << (block % 8));
}


/**
 * Allocate num_blocks contiguous 4096-byte blocks.
 *
 * Returns:
 *
 *     fs_memory + offset
 *
 * or NULL if no sufficiently large contiguous region exists.
 */
void *fs_alloc(size_t num_blocks)
{
    if (num_blocks == 0 || num_blocks > fs_num_blocks)
        return NULL;

    size_t run_start = 0;
    size_t run_length = 0;

    for (size_t i = 0; i < fs_num_blocks; i++) {

        if (!block_is_allocated(i)) {
            if (run_length == 0)
                run_start = i;

            run_length++;

            if (run_length == num_blocks) {
                for (size_t j = run_start;
                     j < run_start + num_blocks;
                     j++) {
                    block_set(j);
                }

                return (uint8_t *)fs_memory +
                       run_start * RAMFS_BLOCK_SIZE;
            }

        } else {
            run_length = 0;
        }
    }

    return NULL;
}


/**
 * Free num_blocks previously allocated by fs_alloc().
 */
void fs_free(void *ptr, size_t num_blocks)
{
    if (ptr == NULL || num_blocks == 0)
        return;

    uintptr_t start = (uintptr_t)fs_memory;
    uintptr_t addr  = (uintptr_t)ptr;

    /*
     * Pointer must be inside fs_memory.
     */
    if (addr < start)
        return;

    uintptr_t offset = addr - start;

    /*
     * Allocation must start on a 4096-byte boundary.
     */
    if (offset % RAMFS_BLOCK_SIZE != 0)
        return;

    size_t block = offset / RAMFS_BLOCK_SIZE;

    if (block >= fs_num_blocks ||
        num_blocks > fs_num_blocks - block)
        return;

    for (size_t i = block;
         i < block + num_blocks;
         i++) {
        block_clear(i);
    }
}

void *file_struct_memory;
uint32_t file_memory_idx;


void *descriptor_memory;
uint32_t descriptor_memory_idx;

typedef struct ramfs_file_entry {
    char path[FS_MAX_PATH_LENGTH];
    uint8_t *data_ptr;
    uint64_t size;

    uint32_t mode;
    bool     is_dir;

    uint64_t dev;
    uint64_t ino;
    uint64_t nlink;
    uint64_t uid;
    uint64_t gid;
    uint64_t rdev;

    uint64_t blksize;
    uint64_t blocks;

    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;

    uint64_t atime_nsec;
    uint64_t mtime_nsec;
    uint64_t ctime_nsec;

    uint64_t used;
    // why does there need to be a next entry?
    struct ramfs_file_entry *next;
} ramfs_file_entry_t;

typedef struct ramfs_fd {
    ramfs_file_entry_t *file_entry;
    uint64_t offset;
} fd_t;

static ramfs_file_entry_t *ramfs_files = NULL;
static ramfs_file_entry_t *ramfs_hash_table[RAMFS_HASH_TABLE_SIZE] = {0};
static uint8_t *ramfs_buffer = NULL;
static uint8_t *ramfs_bitmap[RAMFS_BITMAP_SIZE];
static uint64_t ramfs_buffer_size = 0;
static bool ramfs_initialised = false;


/* FD management */
static uint32_t next_fd = 1;
static fd_t fd_table[RAMFS_MAX_OPENED_FILENUM];


/* Concurrency Control - Spinlock */
static volatile int ramfs_lock = 0;


static inline void ramfs_lock_acquire(void) {
    while (__atomic_test_and_set(&ramfs_lock, __ATOMIC_ACQUIRE));
}


static inline void ramfs_lock_release(void) {
    __atomic_clear(&ramfs_lock, __ATOMIC_RELEASE);
}


/* Hash Function (DJB2) */
static uint32_t hash_path(const char *path) {
    uint32_t hash = 5381;
    int c;
    while ((c = *path++))
        hash = ((hash << 5) + hash) + c;
    return hash % RAMFS_HASH_TABLE_SIZE;
}


static ramfs_file_entry_t* find_file(const char *path) {
    uint32_t h = hash_path(path);
    ramfs_file_entry_t *entry   = ramfs_hash_table[h];
   
   
    while (entry) {
        if (strcmp(entry->path, path) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}


/* Block Allocator with Next Free Hint */
static uint64_t ramfs_next_free_hint = 0;


static int allocate_blocks(uint64_t count, uint8_t **out_ptr) {
    uint64_t start_block = ramfs_next_free_hint;
    
    // Optimization: Try to find contiguous blocks starting from the hint
    for (uint64_t i = 0; i < RAMFS_TOTAL_BLOCKS; i++) {
        uint64_t current = (start_block + i) % RAMFS_TOTAL_BLOCKS;
        
        bool found = true;
        for (uint64_t j = 0; j < count; j++) {
            uint64_t idx = (current + j) % RAMFS_TOTAL_BLOCKS;
            if (idx >= RAMFS_TOTAL_BLOCKS || (ramfs_bitmap[idx / 8] & (1 << (idx % 8)))) {
                found = false;
                break;
            }
        }


        if (found) {
            for (uint64_t j = 0; j < count; j++) {
                uint64_t idx = (current + j) % RAMFS_TOTAL_BLOCKS;
                ramfs_bitmap[idx / 8] |= (1 << (idx % 8));
            }
            ramfs_next_free_hint = (current + count) % RAMFS_TOTAL_BLOCKS;
            *out_ptr = ramfs_buffer + (current * RAMFS_BLOCK_SIZE);
            return 0;
        }
    }
    return -1;
}


static void free_blocks(uint8_t *ptr, uint64_t size) {
    uint64_t start_block = (uint64_t)((uintptr_t)ptr - (uintptr_t)ramfs_buffer) / RAMFS_BLOCK_SIZE;
    uint64_t count = (size + RAMFS_BLOCK_SIZE - 1) / RAMFS_BLOCK_SIZE;
    for (uint64_t i = 0; i < count; i++) {
        ramfs_bitmap[(start_block + i) / 8] &= ~(1 << (start_block + i) % 8);
    }
    ramfs_next_free_hint = start_block;
}


/* CPIO newc format header */
struct cpio_header {
    uint16_t magic;
    uint16_t flags;
    uint16_t version;
    uint32_t pad;
    uint32_t name_len;
    uint16_t mode;
    uint16_t uid;
    uint16_t gid;
    uint32_t size;
    uint32_t mtime;
    uint32_t atime;
    uint32_t dev;
    uint8_t type;
    uint8_t dev_major;
    uint8_t dev_minor;
    uint8_t link_count;
    uint32_t pad2;
};
