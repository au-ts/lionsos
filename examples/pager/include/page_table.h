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


#define PAGE_SHIFT 12
#define PT_INDEX_SHIFT 21
#define PD_INDEX_SHIFT 30
#define PUD_INDEX_SHIFT 39

#define PT_INDEX_MASK  0x1FFUL
#define PD_INDEX_MASK  0x1FFUL
#define PUD_INDEX_MASK 0x1FFUL

#define PT_INDEX(va)  (((va) >> PT_INDEX_SHIFT)  & PT_INDEX_MASK)
#define PD_INDEX(va)  (((va) >> PD_INDEX_SHIFT)  & PD_INDEX_MASK)
#define PUD_INDEX(va) (((va) >> PUD_INDEX_SHIFT) & PUD_INDEX_MASK)

typedef uint64_t pte_t; // seL4_ARM_VSpaceObject 39-47

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

typedef uint64_t* pt_t;


/**
 * Creates a page table entry including intermediary paging structures if necessary.
 * returns a pointer to the created page.
 */
uint64_t *get_page_table_entry(uintptr_t vaddr, pt_t pud);

// 
void insert_frame_to_page(uint32_t const frame, uint64_t* page);

// get bits 12:47
uint32_t get_frame_from_page(uint64_t const page);

#endif