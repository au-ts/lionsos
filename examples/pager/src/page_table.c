#include "page_table.h"
#include "pager.h"

uint64_t *get_page_table_entry(uintptr_t vaddr, pt_t vspace, int *num) {
    pt_t pud = (pt_t) vspace[PUD_INDEX(vaddr)];
    int count = 0;
    if (!pud) {
        // allocate pud; & pt
        pud = (pt_t) allocate_pager_memory(sizeof(pt_t) * 512);
        vspace[PUD_INDEX(vaddr)] = (uint64_t) pud;
        ++count;
    }
    pt_t pd = (pt_t) pud[PD_INDEX(vaddr)];
    if (!pd) {
        // allocate pd; & pt
        pd = (pt_t) allocate_pager_memory(sizeof(pt_t) * 512);
        pud[PD_INDEX(vaddr)] = (uint64_t) pd;
        ++count;
    }
    pt_t pt = (pt_t) pd[PT_INDEX(vaddr)];
    if (!pt) {
        // allocat pt;
        pt = (pt_t) allocate_pager_memory(sizeof(pt_t) * 512);
        pd[PT_INDEX(vaddr)] = (uint64_t) pt;
        ++count;
    }
    *num = count;
    return &pt[PAGE_INDEX(vaddr)];
}

// 
void insert_frame_to_page(uint32_t const frame, uint64_t* page) {
    *page |= ((uint64_t) frame) << 12;
}

// get bits 12:47
uint32_t get_frame_from_page(uint64_t const page) {
    return (page >> 12) & 0xFFFFFFFFFULL;
}