#include "mem.h"
#include "pager.h"

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <string.h>

#define BLOCK_SIZE 4096
#define MAX_CHILDREN 10
#define NUM_BLOCKS 50000
#define PAGE_SIZE 0x1000
#define PAGE_MASK 0xFFFFFFFFFFFFF000ULL
#define ROUND_DOWN_TO_4K(x)      ((uintptr_t)(x) & PAGE_MASK)
/**
 * I need to create an allocator with these memory regions.
 */
// set to 0x8000000000
void *heaps[MAX_CHILDREN]; 
// set to 0x7000000000
void *brks[MAX_CHILDREN];
void *morecore_bases[MAX_CHILDREN];


/**
 * Should be size (NUM_BLOCKS + 7) / 8 sized.
 * One bit per block:
 *   0 = free
 *   1 = allocated
 */
static uint8_t bitmaps[MAX_CHILDREN][(NUM_BLOCKS + 7) / 8];

/**
 * Initialise the allocator.
 *
 * bitmap_storage must be large enough to hold:
 *
 *     ceil(num_blocks / 8)
 *
 * bytes.
 */
void allocator_init()
{
    for (int i = 0; i < MAX_CHILDREN; ++i) {
        memset(bitmaps[i], 0, (NUM_BLOCKS + 7) / 8);
        heaps[i] = (void *)0x8000000000ULL;
        brks[i] = (void *)0x7000000000ULL;
        morecore_bases[i] = (void *)0x7000000000ULL;
    }
}


static inline bool block_is_allocated(size_t block, uint8_t *bitmap)
{
    return bitmap[block / 8] & (1u << (block % 8));
}


static inline void block_set(size_t block, uint8_t *bitmap)
{
    bitmap[block / 8] |= (1u << (block % 8));
}


static inline void block_clear(size_t block, uint8_t *bitmap)
{
    bitmap[block / 8] &= ~(1u << (block % 8));
}


/**
 * Allocate num_blocks contiguous 4096-byte blocks.
 *
 * Returns:
 *
 *     heap + offset
 *
 * or NULL if no sufficiently large contiguous region exists.
 */
void *alloc(size_t num_blocks, void *heap, uint8_t *bitmap)
{
    if (num_blocks == 0 || num_blocks > NUM_BLOCKS)
        return NULL;

    size_t run_start = 0;
    size_t run_length = 0;

    for (size_t i = 0; i < NUM_BLOCKS; i++) {

        if (!block_is_allocated(i, bitmap)) {
            if (run_length == 0)
                run_start = i;

            run_length++;

            if (run_length == num_blocks) {
                for (size_t j = run_start;
                     j < run_start + num_blocks;
                     j++) {
                    block_set(j, bitmap);
                }

                return (uint8_t *)heap +
                       run_start * BLOCK_SIZE;
            }

        } else {
            run_length = 0;
        }
    }

    return NULL;
}


/**
 * Free num_blocks previously allocated by alloc().
 */
void free_blocks(void *ptr, size_t num_blocks, void *heap, uint8_t *bitmap)
{
    if (ptr == NULL || num_blocks == 0)
        return;

    uintptr_t start = (uintptr_t)heap;
    uintptr_t addr  = (uintptr_t)ptr;

    /*
     * Pointer must be inside heap.
     */
    if (addr < start)
        return;

    uintptr_t offset = addr - start;

    /*
     * Allocation must start on a 4096-byte boundary.
     */
    if (offset % BLOCK_SIZE != 0)
        return;

    size_t block = offset / BLOCK_SIZE;

    if (block >= NUM_BLOCKS ||
        num_blocks > NUM_BLOCKS - block)
        return;

    for (size_t i = block;
         i < block + num_blocks;
         i++) {
        block_clear(i, bitmap);
    }
}

/**
 * TODO: implement bookkeeping of allocated memory regions so that pager can validate.
 */

static long sys_brk(va_list ap, microkit_child child) {
    uintptr_t newbrk = va_arg(ap, uintptr_t);

    if (newbrk <= 0x800000000 && newbrk >= (uintptr_t)morecore_bases[child]) {
        if (newbrk < (uintptr_t)brks[child]) {
            // TODO: free the memory region (if it is taken up).
            // this should free the brks[child] rounded down to newbrk rounded down.
            myfree(ROUND_DOWN_TO_4K(brks[child]), ROUND_DOWN_TO_4K(newbrk), child);
        }
        brks[child] = (void *)newbrk;
        return (long)newbrk;
    }
    return (long)brks[child];
}

static long sys_mmap(va_list ap, microkit_child child) {
    void *addr = va_arg(ap, void *);
    size_t length = va_arg(ap, size_t);
    int prot = va_arg(ap, int);
    int flags = va_arg(ap, int);
    int fd = va_arg(ap, int);
    off_t offset = va_arg(ap, off_t);
    (void)fd, (void)offset, (void)prot, (void)addr;

    if (length == 0) {
        return -EINVAL;
    }

    if (flags & MAP_ANONYMOUS) {
        // return an address that is n blocks long.
        length = (length + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);
        return (long)alloc(length / PAGE_SIZE, heaps[child], bitmaps[child]);
    }
    return -ENOMEM;
}

static long sys_munmap(va_list ap, microkit_child child) {
    void *addr = va_arg(ap, void *);
    size_t length = va_arg(ap, size_t);
    length = (length + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);
    free_blocks((void *)ROUND_DOWN_TO_4K(addr), length / PAGE_SIZE,
                heaps[child], bitmaps[child]);
    myfree(ROUND_DOWN_TO_4K(addr), ROUND_DOWN_TO_4K(addr) + length, child);
    return 0;
}

static long sys_mprotect(va_list ap, microkit_child microkit_child) {
    void *addr = va_arg(ap, void *);
    size_t size = va_arg(ap, size_t);
    int prot = va_arg(ap, int);
    (void)addr, (void)size, (void)prot;
    // do nothing.
    return 0;
}

long pager_mem_call(microkit_msginfo msginfo, microkit_child child)
{
    uintptr_t addr;
    size_t length;

    switch (microkit_msginfo_get_label(msginfo)) {
    case PAGER_MEM_BRK:
        addr = microkit_mr_get(0);
        if (addr <= 0x800000000 && addr >= (uintptr_t)morecore_bases[child]) {
            if (addr < (uintptr_t)brks[child]) {
                myfree(ROUND_DOWN_TO_4K(brks[child]), ROUND_DOWN_TO_4K(addr), child);
            }
            brks[child] = (void *)addr;
            return (long)addr;
        }
        return (long)brks[child];
    case PAGER_MEM_MMAP:
        length = microkit_mr_get(1);
        if (length == 0 || !(microkit_mr_get(3) & MAP_ANONYMOUS)) {
            return -ENOMEM;
        }
        length = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        return (long)alloc(length / PAGE_SIZE, heaps[child], bitmaps[child]);
    case PAGER_MEM_MUNMAP:
        addr = microkit_mr_get(0);
        length = (microkit_mr_get(1) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        if (length == 0) {
            return -EINVAL;
        }
        free_blocks((void *)ROUND_DOWN_TO_4K(addr), length / PAGE_SIZE,
                    heaps[child], bitmaps[child]);
        myfree(ROUND_DOWN_TO_4K(addr), ROUND_DOWN_TO_4K(addr) + length, child);
        return 0;
    case PAGER_MEM_FORK: {
        long new_child = pager_fork(child);
        if (new_child > 0 && new_child < MAX_CHILDREN) {
            brks[new_child] = brks[child];
            morecore_bases[new_child] = morecore_bases[child];
            memcpy(bitmaps[new_child], bitmaps[child], sizeof(bitmaps[child]));
        }
        return new_child;
    }
    default:
        return -ENOSYS;
    }
}