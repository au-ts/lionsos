#include <stdint.h>
#include <stddef.h>

#define MAX_MEMORY_SEGMENTS 10

// might want to make sure we are mapping with the correct permissions later
typedef struct {
    uint64_t target_vaddr;
    uint64_t src;
    uint32_t size;
} segment;

typedef struct {
    uint32_t size;
    uint32_t offset;

    uint32_t num_seg;
    segment segments[MAX_MEMORY_SEGMENTS];
} Arena;

static inline void arena_init(Arena *a, uint32_t size)
{
    a->num_seg = 0;
    a->size = size;
    a->offset = sizeof(Arena);
}

static inline void *arena_alloc(Arena *a, uint32_t size, uint64_t target_vaddr)
{
    uint32_t next = a->offset + size;

    if (next > a->size || a->num_seg >= MAX_MEMORY_SEGMENTS) {
        return NULL;
    }

    uint64_t vaddr = (uint64_t)a + a->offset;

    a->segments[a->num_seg].target_vaddr = target_vaddr;
    a->segments[a->num_seg].src = vaddr;
    a->segments[a->num_seg].size = size;
    a->num_seg++;

    a->offset = next;
    return (void *)vaddr;
}

int get_num_segments(Arena *a) {
    return a->num_seg;
}

void *get_segments(Arena *a) {
    return &(a->segments);
}

void arena_reset(Arena *a)
{
    a->offset = sizeof(Arena);
    a->num_seg = 0;
}