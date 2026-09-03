#include "decl.h"
#include <lions/fs/protocol.h>
#include <lions/fs/server.h>
#include <lions/fs/server_utils.h>
#include <lions/posix/fd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <time.h>


#include <lions/fs/config.h>


#define RAMFS_BLOCK_SIZE 4096
#define RAMFS_TOTAL_BLOCKS (RAMFS_FS_DATA_REGION_SIZE / RAMFS_BLOCK_SIZE)
#define RAMFS_BITMAP_SIZE (RAMFS_TOTAL_BLOCKS / 8)

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
    fs_num_blocks = memory_size / FS_BLOCK_SIZE;
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
                       run_start * FS_BLOCK_SIZE;
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
    if (offset % FS_BLOCK_SIZE != 0)
        return;

    size_t block = offset / FS_BLOCK_SIZE;

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

typedef struct ramfs_file {
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
} ramfs_file_t;

typedef struct ramfs_file_entry {
    ramfs_file_t *file;

    char path[FS_MAX_PATH_LENGTH];

    uint64_t offset;

    struct ramfs_file_entry *next;
} ramfs_file_entry_t;


static ramfs_file_entry_t *ramfs_files = NULL;
static ramfs_file_entry_t *ramfs_hash_table[RAMFS_HASH_TABLE_SIZE] = {0};
static uint8_t *ramfs_buffer = NULL;
static uint8_t *ramfs_bitmap = NULL;
static uint64_t ramfs_buffer_size = 0;
static bool ramfs_initialised = false;


/* FD management */
static fd_t next_fd = 1;
static ramfs_file_entry_t *fd_table[MAX_OPEN_FILES];


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
   ramfs_file_entry_t *entry = ramfs_hash_table[h];
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


void handle_initialise(void) {
   LOG_RAMFS("Initialising ramfs (Optimized)!\n");
   co_data_t *args = microkit_cothread_my_arg();
  
   ramfs_buffer_size = RAMFS_FS_DATA_REGION_SIZE;
   ramfs_buffer = (uint8_t *)malloc(ramfs_buffer_size);
   ramfs_bitmap = (uint8_t *)malloc(RAMFS_BITMAP_SIZE);
  
   if (!ramfs_buffer || !ramfs_bitmap) {
       args->status = FS_STATUS_ALLOCATION_ERROR;
       return;
   }
   memset(ramfs_bitmap, 0, RAMFS_BITMAP_SIZE);


   FILE *cpio_file = fopen(RAMFS_CPIO_PATH, "rb");
   if (cpio_file) {
       uint8_t *buffer_ptr = ramfs_buffer;
       uint64_t current_buffer_usage = 0;


       while (!feof(cpio_file)) {
           struct cpio_header header;
           if (fread(&header, sizeof(struct cpio_header), 1, cpio_file) != 1) break;


           if (header.magic != 0x434F) break;


           char *path = (char *)malloc(header.name_len + 1);
           fread(path, header.name_len, 1, cpio_file);
           path[header.name_len] = '\0';


           ramfs_lock_acquire();
           if (header.type == 1) { // Directory
               ramfs_file_entry_t *entry = (ramfs_file_entry_t *)malloc(sizeof(ramfs_file_entry_t));
               if (entry) {
                   strncpy(entry->path, path, FS_MAX_PATH_LENGTH);
                   entry->is_dir = true;
                   entry->size = 0;
                   entry->mode = header.mode;
                   entry->next = ramfs_hash_table[hash_path(path)];
                   ramfs_hash_table[hash_path(path)] = entry;
               }
           } else { // File
               if (current_buffer_usage + header.size <= ramfs_buffer_size) {
                   ramfs_file_entry_t *entry = (ramfs_file_entry_t *)malloc(sizeof(ramfs_file_entry_t));
                   if (entry) {
                       strncpy(entry->path, path, FS_MAX_PATH_LENGTH);
                       entry->data_ptr = buffer_ptr + current_buffer_usage;
                       entry->size = header.size;
                       entry->is_dir = false;
                       entry->mode = header.mode;
                       entry->next = ramfs_hash_table[hash_path(path)];
                       ramfs_hash_table[hash_path(path)] = entry;


                       fread(buffer_ptr + current_buffer_usage, header.size, 1, cpio_file);
                       current_buffer_usage += header.size;
                   }
               }
           }
           ramfs_lock_release();
           free(path);
       }
       fclose(cpio_file);
   }


   ramfs_initialised = true;
   args->status = FS_STATUS_SUCCESS;
}


void handle_deinitialise(void) {
   LOG_RAMFS("Deinitialising ramfs!\n");
   ramfs_lock_acquire();
   if (ramfs_buffer) {
       free(ramfs_buffer);
       ramfs_buffer = NULL;
   }
   if (ramfs_bitmap) {
       free(ramfs_bitmap);
       ramfs_bitmap = NULL;
   }
   // Clean up hash table
   for (int i = 0; i < RAMFS_HASH_TABLE_SIZE; i++) {
       ramfs_file_entry_t *e = ramfs_hash_table[i];
       while (e) {
           ramfs_file_entry_t *tmp = e;
           e = e->next;
           free(tmp);
       }
       ramfs_hash_table[i] = NULL;
   }
   ramfs_files = NULL;
   ramfs_initialised = false;
   ramfs_lock_release();
}


void handle_file_open(void) {
   co_data_t *args = microkit_cothread_my_arg();
   fs_cmd_params_file_open_t *params = &args->params.file_open;
  
   ramfs_file_entry_t *entry = find_file((char *)params->path.buf);
  
   ramfs_lock_acquire();
   if (!entry) {
       if (params->flags & FS_OPEN_FLAGS_CREATE) {
           uint64_t needed_size = 4096;
           uint8_t *new_ptr = NULL;
           if (allocate_blocks(needed_size / RAMFS_BLOCK_SIZE, &new_ptr) != 0) {
               args->status = FS_STATUS_ALLOCATION_ERROR;
               ramfs_lock_release();
               return;
           }


           entry = (ramfs_file_entry_t *)malloc(sizeof(ramfs_file_entry_t));
           if (!entry) {
               args->status = FS_STATUS_ALLOCATION_ERROR;
               ramfs_lock_release();
               return;
           }
           strncpy(entry->path, (char *)params->path.buf, FS_MAX_PATH_LENGTH);
           entry->data_ptr = new_ptr;
           entry->size = needed_size;
           entry->is_dir = false;
           entry->mode = 0644;
           entry->next = ramfs_hash_table[hash_path(entry->path)];
           ramfs_hash_table[hash_path(entry->path)] = entry;
       } else {
           args->status = FS_STATUS_NO_FILE;
           ramfs_lock_release();
           return;
       }
   }


   if (next_fd >= MAX_OPEN_FILES) {
       args->status = FS_STATUS_TOO_MANY_OPEN_FILES;
       ramfs_lock_release();
       return;
   }


   fd_table[next_fd] = entry;
   args->result.file_open.fd = next_fd++;
   args->status = FS_STATUS_SUCCESS;
   ramfs_lock_release();
}

void handle_file_size(void) {
    co_data_t *args = microkit_cothread_my_arg();
    fs_cmd_params_file_size_t *params = &args->params.file_size;
    
    ramfs_lock_acquire();
    if (params->fd >= MAX_OPEN_FILES || fd_table[params->fd] == NULL) {
        args->status = FS_STATUS_INVALID_FD;
        ramfs_lock_release();
        return;
    }


    ramfs_file_entry_t *entry = fd_table[params->fd];
    params->size = entry->size;
    
    args->status = FS_STATUS_SUCCESS;
    ramfs_lock_release();
}


void handle_rename(void) {
    co_data_t *args = microkit_cothread_my_arg();
    fs_cmd_params_rename_t *params = &args->params.rename;
    
    ramfs_lock_acquire();
    ramfs_file_entry_t *old_entry = find_file((char *)params->old_path.buf);
    if (!old_entry) {
        args->status = FS_STATUS_NO_FILE;
        ramfs_lock_release();
        return;
    }


    strncpy(old_entry->path, (char *)params->new_path.buf, FS_MAX_PATH_LENGTH);
    
    args->status = FS_STATUS_SUCCESS;
    ramfs_lock_release();
}


void handle_file_remove(void) {
    co_data_t *args = microkit_cothread_my_arg();
    fs_cmd_params_file_remove_t *params = &args->params.file_remove;
    
    ramfs_lock_acquire();
    ramfs_file_entry_t *entry = find_file((char *)params->path.buf);
    if (!entry) {
        args->status = FS_STATUS_NO_FILE;
        ramfs_lock_release();
        return;
    }


    free_blocks(entry->data_ptr, entry->size);


    // Remove from hash table
    uint32_t h = hash_path(entry->path);
    ramfs_file_entry_t **head = &ramfs_hash_table[h];
    if (*head == entry) {
        *head = entry->next;
    } else {
        ramfs_file_entry_t *curr = *head;
        while (curr->next && curr->next != entry) {
            curr = curr->next;
        }
        if (curr) curr->next = entry->next;
    }


    args->status = FS_STATUS_SUCCESS;
    ramfs_lock_release();
}


void handle_file_truncate(void) {
    co_data_t *args = microkit_cothread_my_arg();
    fs_cmd_params_file_truncate_t *params = &args->params.file_truncate;
    
    ramfs_lock_acquire();
    if (params->fd < MAX_OPEN_FILES && fd_table[params->fd] != NULL) {
        ramfs_file_entry_t *entry = fd_table[params->fd];
        if (params->length < entry->size) {
            free_blocks(entry->data_ptr + params->length, entry->size - params->length);
        }
        entry->size = params->length;
    }
    
    args->status = FS_STATUS_SUCCESS;
    ramfs_lock_release();
}



ssize_t ramfs_write(const void *buf, size_t len, int fd) {
    ramfs_lock_acquire();
    ramfs_file_entry_t *entry = fd_table[fd];
    if (fd >= MAX_OPEN_FILES || entry == NULL) {
        ramfs_lock_release();
        return FS_STATUS_INVALID_FD;
    }
    if (entry->size < entry->offset + entry->written) {
        // move the file elsewhere.
        // TODO;
        ramfs_lock_release();
        return FS_STATUS_SERVER_WAS_DENIED;
    }

    memcpy(entry->data_ptr + entry->offset, buf, len);
    ramfs_lock_release();
    return FS_STATUS_SUCCESS;
}

ssize_t ramfs_read(void *buf, size_t len, int fd) {
    ramfs_lock_acquire();
    ramfs_file_entry_t *entry = fd_table[fd];
    if (fd >= MAX_OPEN_FILES || entry == NULL) {
        ramfs_lock_release();
        return FS_STATUS_INVALID_FD;
    }
    if (entry->offset + len > entry->size) {
        ramfs_lock_release();
       return FS_STATUS_INVALID_READ;
    }
    memcpy(buf, entry->data_ptr + entry->offset, len);
    ramfs_lock_release();
    return FS_STATUS_SUCCESS;
}

int ramfs_close(int fd) {
    ramfs_lock_acquire();
    ramfs_file_entry_t *entry = fd_table[fd];
    if (fd < MAX_OPEN_FILES && entry != NULL) {
        fd_table[fd] = NULL;
        // TODO: need to free the struct.
    }
    ramfs_lock_release();
    return FS_STATUS_SUCCESS;
}   

int ramfs_dup3(int oldfd, int newfd) {
    // TODO: reference counting.
    fd_table[newfd] = fd_table[oldfd];
    return FS_STATUS_SUCCESS;
}

int ramfs_fstat(int fd, struct stat *statbuf) {
    ramfs_file_entry_t *entry = fd_table[fd];
}

int fstat_int(const char *path, struct stat *statbuf) {
    ramfs_file_entry_t *entry = find_file(path);
  
    ramfs_lock_acquire();
    if (!entry) {
        args->status = FS_STATUS_NO_FILE;
        ramfs_lock_release();
        return;
    }


    statbuf->size = entry->size;
    statbuf->blksize = RAMFS_BLOCK_SIZE;
    statbuf->blocks = entry->size / RAMFS_BLOCK_SIZE;
    statbuf->atime = entry->atime;
    statbuf->mtime = entry->mtime;
    statbuf->ctime = entry->ctime;
    
    args->status = FS_STATUS_SUCCESS;
    ramfs_lock_release();
}