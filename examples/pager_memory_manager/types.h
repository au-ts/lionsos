#ifndef TYPES_H
#define TYPES_H

#include <microkit.h>
#include <stdbool.h>
#include <stdint.h>

#include <sddf/util/printf.h>

#define MAX_PDS 64
#define NUM_PT_ENTRIES 300
#define BRK_START 0x8000000000 
#define MMAP_START 0x9000000000
#define ROUND_DOWN_TO_4K(x) ((x) & ~(4096 - 1))
#define INDEX_INTO_MMAP_ARRAY(x) (ROUND_DOWN_TO_4K(x) - BRK_START) / 4096
#define TAU 10 // not too sure what the optimal number for this would be. maybe this is not useful...
#define PAGEFILE ".pagefile"
#define MM_PPC_NUM 0
#define FRAME_DATA 0x8000000000

extern uintptr_t frame_map_addr;
uintptr_t frame_map_addr = 0;

struct mmap_node
{
    /* data */
    uintptr_t addr;
    struct mmap_node *next;
    struct mmap_node *prev;
};



typedef struct Rights {
    bool read;
    bool write;
    bool grant;
    bool grant_reply;
} Rights;

typedef unsigned long long Cap;

typedef struct microkit_data {
    Cap frame_cap;
    uintptr_t pd_idx;
} frame_pd_id;

struct frame;

typedef struct pe {
    struct frame *frame_addr;
    int pagefile_offset;
} pe;

// typedef struct FrameInfo {
//     Cap cap;
//     uint64_t last_accessed; // for working set.
//     pe *page; // the page this frame is mapped to.
//     uint32_t next;
//     int pd_idx;
// } FrameInfo;

typedef struct FrameInfo {
    seL4_CPtr cap;
    uint64_t last_accessed;
    pe *page;
    uint32_t next;
    uint32_t prev;
    uint32_t pd_idx;
} FrameInfo;

enum paging_state {
    PAGE_OUT,
    PAGE_IN,
};

struct page_request_info {
    uint32_t pd_idx;
    uintptr_t fault_addr;
    struct frame *frame;
    enum paging_state state;
};

/**
 * This actually should wipe the data an the page tables etc...
 */
void myfree(uint16_t mm, uintptr_t addr) {
    microkit_msginfo message = microkit_msginfo_new(0, 2);
    microkit_mr_set(0, 0);
    microkit_mr_set(1, addr);
    microkit_ppcall(mm, message);
}

uintptr_t mymalloc(uint64_t mm) {
    microkit_msginfo message = microkit_msginfo_new(0, 1);
    microkit_mr_set(0, 1);
    microkit_ppcall(mm, message);
    return microkit_mr_get(0);
}

/**
 * if page is not null that means it is mapped
 */
typedef struct frame {
    struct frame *next;
    struct frame *prev;
    seL4_CPtr cap;
    pe *page;
    uint32_t pd_idx;
    bool referenced;
    bool dirty;
    bool active;
    uintptr_t oaddr;
} tl_frame_t;

struct list {
    tl_frame_t *head;
    tl_frame_t *tail;
    int size;
};

struct fault_info {
    uintptr_t addr;
    bool write;
};

char *get_frame_data(tl_frame_t *frame) {
    return (char *)(FRAME_DATA + (((frame->oaddr - frame_map_addr) / sizeof(frame_pd_id)) * 4096)); 
}

#endif