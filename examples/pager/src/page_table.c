#include "page_table.h"
#include "pager.h"




// 
void insert_frame_to_page(uint32_t const frame, uint64_t* page) {
    *page |= ((uint64_t) frame) << 12;
}

// get bits 12:47
uint32_t get_frame_from_page(uint64_t const page) {
    return (page >> 12) & 0xFFFFFFFFFULL;
}