#include "page_table.h"
#include "pager.h"

uint64_t *get_page_table_entry(uintptr_t vaddr, pt_t pud) {
    pt_t pd = (pt_t) pud[PUD_INDEX(vaddr)];
    if (!pd) {
        // allocate pd; & pt
        pd = (pt_t) allocate_pager_memory(sizeof(pt_t) * 512);
        pud[PUD_INDEX(vaddr)] = (uint64_t) pd;
    }
    pt_t pt = (pt_t) pd[PD_INDEX(vaddr)];
    if (!pt) {
        // allocat pt;
        pt = (pt_t) allocate_pager_memory(sizeof(pt_t) * 512);
        pd[PD_INDEX(vaddr)] = (uint64_t) pt;
    }
    return &pt[PT_INDEX(vaddr)];
}

// 
void insert_frame_to_page(uint32_t const frame, uint64_t* page) {
    *page |= ((uint64_t) frame) << 12;
}

// get bits 12:47
uint32_t get_frame_from_page(uint64_t const page) {
    return (page >> 12) & 0xFFFFFFFFFULL;
}