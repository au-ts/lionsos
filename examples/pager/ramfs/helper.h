#include "decl.h"

ramfs_file_entry_t *alloc_file_entry();
void free_file_entry(ramfs_file_entry_t* entry);
void fs_allocator_init(void *memory,
                       size_t memory_size,
                       void *bitmap_storage);
void *fs_alloc(size_t num_blocks);
void fs_free(void *ptr, size_t num_blocks);
