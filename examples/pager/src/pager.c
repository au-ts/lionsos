/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "cspace.h"
#include "frame_table.h"
#include "mem.h"
#include "page_table.h"
#include "pager.h"
#include "proc.h"
#include "untyped.h"

#include <microkit.h>
#include <sddf/util/printf.h>

/*
 * Patched into pager.elf by the Microkit tool, see include/pager.h. vspaces
 * maps a child id to the VSpace cap the tool placed in our CSpace.
 */
uintptr_t pager_memory;
uintptr_t remaining_untypeds_vaddr;
uint32_t vspaces[MAX_CHILDREN];

void init(void)
{
    allocator_init();
    untyped_init(remaining_untypeds_vaddr,
                 microkit_cspace_root_slot_to_cptr(UNTYPED_CNODE_SLOT));
    /*
     * pager_memory holds the folio metadata at its base and is the arena the
     * shadow page tables are allocated from above that.
     */
    frame_table_init(pager_memory,
                     microkit_cspace_root_slot_to_cptr(FRAME_CNODE_SLOT),
                     microkit_cspace_root_slot_to_cptr(ZERO_PAGE_CNODE_SLOT),
                     microkit_cspace_root_slot_to_cptr(FRAME_COPY_CNODE_SLOT));
    page_table_init(pager_memory + FOLIO_MEMORY_SIZE,
                    microkit_cspace_root_slot_to_cptr(PAGING_CNODE_SLOT));
    process_init(microkit_cspace_root_slot_to_cptr(PROCESS_CNODE_SLOT));
}

void notified(microkit_channel ch)
{
    // does nothing for now.
}

/**
 * Create intermediary paging structures.
 * Map page.
 * Return.
 * 
 * cspace - this is the difficult part... idk how to do that... 
 * vspace
 */
seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    // microkit_pd_stop(child);
    // Only VM faults carry an address and an FSR in the message registers,
    // every other fault type would be decoded as garbage below.
    if (microkit_msginfo_get_label(msginfo) != seL4_Fault_VMFault) {
        sddf_printf("unhandled fault %lu from child %u at ip 0x%lx\n",
            microkit_msginfo_get_label(msginfo), child, microkit_mr_get(0));
        return seL4_False;
    }
    // get fault info
    uintptr_t fault_addr = ROUND_DOWN_TO_4K(microkit_mr_get(1));
    uint64_t fsr = microkit_mr_get(3);
    uintptr_t ip = microkit_mr_get(0);
    uint64_t fsc = fsr & 0x3F;
    bool is_write = (fsr >> 6) & 1;
    struct folio *folio;
    uint32_t frame;
    pte_t *page_entry = make_page_table_entry(fault_addr, child);

    // // TODO: implement access flag faults.
    // if (fsc >= 0x08 && fsc <= 0x0B) {
    //     // Access flag fault (level 0–3)
    //     // unset access flag
    // }
    // Translation fault (level 0–3)
    if (fsc >= 0x04 && fsc <= 0x07) {
        // if it is a read fault, map global zero page.
        if (!is_write) {
            frame = get_gzp();
            seL4_Error err = err = seL4_ARM_Page_Map(gzp_cptr(frame), vspaces[child], fault_addr, create_cap_rights(is_write), 0x03);
            if (err) {
                sddf_printf("error occured on map frame zero %d\n", err);
            }
            // add global zero page to the frame cap.
            insert_frame_to_page(frame, page_entry);
            return seL4_True;
        } else {
            folio = get_frame();
            *page_entry |= DESC_NG;
            frame = folio->frame_page;
        }
        insert_frame_to_page(folio->frame_page, page_entry);
    }
    // Permission fault (level 1–3)
    else if (fsc >= 0x0D && fsc <= 0x0F) {
        // if global zero page get new frame
        if (!(*page_entry & DESC_NG)) {
            folio = get_frame();
            frame = folio->frame_page;
            insert_frame_to_page(frame, page_entry);
            *page_entry |= DESC_NG;
        } else {
            frame = get_frame_from_page(*page_entry);
            folio = get_folio_from_idx(frame);
            if (folio->refcount > 1) {
                // copy on write
                struct folio *new_folio = get_frame();
                insert_frame_to_page(new_folio->frame_page, page_entry);
                --folio->refcount;
                // TODO: copy the frames.
                frame = new_folio->frame_page;
            }
        }
    } else {
        // Nothing else is serviceable, leave the child faulted rather than
        // mapping whatever happens to be in frame.
        sddf_printf("unhandled fault status code 0x%lx at 0x%lx ip 0x%lx\n", fsc, fault_addr, ip);
        return seL4_False;
    }

    // do mapping
    seL4_Error err = seL4_ARM_Page_Map(frame_cptr(frame), vspaces[child], fault_addr, create_cap_rights(is_write), 0x03);
    if (err) {
        sddf_printf("error occured on map frame %d\n", err);
    }
    return seL4_True;
}

seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo)
{
    if (ch == 0) {
        microkit_mr_set(0, pager_mem_call(msginfo, 0));
    }
    return microkit_msginfo_new(0, 1);
}
