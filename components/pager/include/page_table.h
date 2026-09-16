#ifndef _PAGE_TABLES_H
#define _PAGE_TABLES_H
// shadow page table definitions

// ARM64 Page/Block Descriptor (64-bit):

//  63   59 58   55 54 53 52 51      12 11 10  9  8  7  6  5  4  2  1  0
//  ┌──────┬───────┬──┬──┬──┬──────────┬──┬──┬──┬──┬──┬──┬─────┬──┬──┐
//  │ PBHA │ SW   │UXN│PXN│Cont│  OA   │nG│AF│SH│AP│NS│  │AtIdx│type│V│
//  └──────┴───────┴──┴──┴──┴──────────┴──┴──┴──┴──┴──┴──┴─────┴──┴──┘

//  bit 0:     Valid (V)        — 1 = entry is valid
//  bit 1:     Type             — at L0-L2: 1 = table descriptor, 0 = block descriptor
//                                at L3: 1 = page descriptor (must be 1 for valid pages)
//  bits [4:2]: AttrIndx[2:0]  — index into MAIR_EL1 (memory type selection)
//  bit 5:     NS               — Non-Secure (applies in Secure state)
//  bits [7:6]: AP[2:1]         — Access Permissions (see table below)
//  bits [9:8]: SH[1:0]         — Shareability: 00=non-shareable, 10=outer, 11=inner
//  bit 10:    AF               — Access Flag: fault on first access if 0 (SW manages)
//  bit 11:    nG               — not-Global: 1 = ASID-tagged (user), 0 = global (kernel)
//  bits [47:12]: Output Address (OA) — physical page frame number (bits [47:12] of PA)
//  bit 52:    Contiguous hint  — TLB can merge contiguous entries
//  bit 53:    PXN              — Privileged Execute Never (EL1 cannot execute)
//  bit 54:    UXN              — Unprivileged Execute Never (EL0 cannot execute)
//  bits [58:55]: SW            — Software-defined (kernel uses for _PAGE_* flags)
//  bits [63:59]: PBHA          — Page-Based Hardware Attributes (ARMv8.2+)

#include <stdint.h>
#include <sel4/sel4.h>


#define PAGE_SHIFT 12
#define PT_INDEX_SHIFT 21
#define PD_INDEX_SHIFT 30
#define PUD_INDEX_SHIFT 39

#define PAGE_INDEX_MASK 0x1FFUL
#define PT_INDEX_MASK  0x1FFUL
#define PD_INDEX_MASK  0x1FFUL
#define PUD_INDEX_MASK 0x1FFUL

#define PAGE_INDEX(va) (((va) >> PAGE_SHIFT) & PAGE_INDEX_MASK)
#define PT_INDEX(va)  (((va) >> PT_INDEX_SHIFT)  & PT_INDEX_MASK)
#define PD_INDEX(va)  (((va) >> PD_INDEX_SHIFT)  & PD_INDEX_MASK)
#define PUD_INDEX(va) (((va) >> PUD_INDEX_SHIFT) & PUD_INDEX_MASK)

/* Entries in every one of the four levels below. */
#define PAGE_TABLE_ENTRIES 512

#define PAGE_SIZE (1ULL << PAGE_SHIFT)
#define PAGE_MASK (~(PAGE_SIZE - 1))
#define ROUND_DOWN_TO_4K(x) ((uintptr_t)(x) & PAGE_MASK)
#define ROUND_UP_TO_4K(x) (((uintptr_t)(x) + PAGE_SIZE - 1) & PAGE_MASK)

typedef uint64_t pte_t; // seL4_ARM_VSpaceObject 39-47

// struct page_table {
//     struct page_table **entries;
//     uint64_t cap;
// };
struct pt {
    pte_t entries[PAGE_TABLE_ENTRIES];
    uint32_t cap;
};
struct pd {
    struct pt *entries[PAGE_TABLE_ENTRIES];
    uint32_t cap;
};
struct pud {
    struct pd *entries[PAGE_TABLE_ENTRIES];
    uint32_t cap;
};
// a vspace cap would be good to have for a real GPOS.
struct pgd {
    struct pud *entries[PAGE_TABLE_ENTRIES];
};

typedef struct pud pud_t;
typedef struct pd pd_t;
typedef struct pt pt_t;
typedef struct pgd pgd_t;


// typedef struct page_table *pt_t;

// // each of the below have 512 entries.
// typedef struct {
//     pte_t *pte;
// } pt_t; // PageTable 1, 30-38

// typedef struct {
//     pt_t *pt;
// } pd_t; // PageTable 2, 21-29

// typedef struct {
//     pd_t *pt;
// } pud_t; // PageTable 3, 12-20


/**
 * Takes over size bytes at memory as the arena the shadow page tables are
 * bump-allocated from and fills the intermediary paging structure free list.
 * paging_cnode is where the paging structure caps are placed.
 */
void page_table_init(uintptr_t memory, uint64_t size, seL4_CPtr paging_cnode);

/**
 * The root of a child's shadow page table.
 */
pgd_t *page_table_root(uint32_t child);

/**
 * Creates a page table entry including intermediary paging structures if
 * necessary. Returns a pointer to the created page.
 */
pte_t *make_page_table_entry(uintptr_t vaddr, uint32_t child);

/**
 * Unmaps everything a child has mapped in [start, end) and returns the frames
 * and the paging structures that fall empty to their free lists.
 */
void unmap_range(uintptr_t start, uintptr_t end, uint32_t child);

seL4_Error map_frame(uint64_t frame_cap, seL4_CPtr vspace, seL4_Word vaddr,
                     seL4_CapRights_t rights, seL4_ARM_VMAttributes attr, int num);

/**
 * Takes an intermediary paging structure off the unused list.
 */
uint32_t get_ips();
void put_ips(uint32_t ips);

extern seL4_CPtr paging_cnode_cptr;

/* CSpace address of an intermediary paging structure. */
static inline seL4_CPtr ips_cptr(uint32_t ips) {
    return paging_cnode_cptr + ips;
}

// bits 12:47, the frame's index within its CNode
#define DESC_OA (0xFFFFFFFFFULL << 12)

// 
static inline void insert_frame_to_page(uint32_t const frame, uint64_t* page) {
    *page = (*page & ~DESC_OA) | (((uint64_t) frame) << 12);
}

// get bits 12:47
static inline uint32_t get_frame_from_page(uint64_t const page) {
    return (page >> 12) & 0xFFFFFFFFFULL;
}

// void set_nG(uint64_t *page) {
//     *page |= (1ULL << 11);
// }

// void clear_nG(uint64_t *page) {
//     *page &= ~(1ULL << 11);
// }

#define DESC_NG (1ULL << 11)

#endif
