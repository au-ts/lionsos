
#define MAX_PDS 64
#define NUM_PT_ENTRIES 128
#define BRK_START 0x8000000000
#define MMAP_START 0x9000000000
#define ROUND_DOWN_TO_4K(x) ((x) & ~(4096 - 1))
#define INDEX_INTO_MMAP_ARRAY(x) (ROUND_DOWN_TO_4K(x)) / 4096
#define TAU 10 // not too sure what the optimal number for this would be. maybe this is not useful...
#define PAGEFILE "pagefile"

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

typedef struct Cap {
    uint32_t object;
    Rights rights;
    bool cached;
    bool executable;
} Cap;

typedef struct microkit_data {
    Cap frame_cap;
    uintptr_t pd_idx;
} frame_pd_id;

typedef struct page_entry {
    void *frame_addr;
    bool dirty;
    bool recently_used;
    uint32_t pagefile_offset;
} pe; // might need more stuff here.

typedef struct FrameInfo {
    Cap cap;
    uint64_t last_accessed; // for working set.
    pe *page; // the page this frame is mapped to.
    uint32_t next;
} FrameInfo;


