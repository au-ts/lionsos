#include "types.h"
#include <stdint.h>


void after_page_in(tl_frame_t *frame, uint32_t pd_idx, uintptr_t fault_addr, bool paged_in);
void page_in(tl_frame_t *frame, uint32_t pd_idx, uintptr_t fault_addr);
void after_page_out(tl_frame_t *frame, uint32_t pd_idx, uintptr_t fault_addr);
void page_out(tl_frame_t *frame, uint32_t pd_idx, uintptr_t fault_addr);