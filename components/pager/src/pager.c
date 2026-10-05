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
#include "pager_config.h"

#include <microkit.h>
#include <sddf/util/printf.h>
#include <string.h>

__attribute__((__section__(".pager_server_config"))) pager_server_config_t pager_config;

/*
 * Patched into pager.elf by the Microkit tool, see include/pager.h. vspaces maps a
 * client's fault id to the VSpace cap the tool placed in our CSpace.
 */
uint32_t vspaces[PAGER_MAX_CLIENTS];
uint32_t elf_caps[PAGER_MAX_CLIENTS][PAGER_MAX_ELF_FRAMES];
uint32_t elf_sizes[PAGER_MAX_CLIENTS];

/*
 * Which client a PPC arrived from. sdfgen hands each client a fault id equal to its
 * index, so this is the only mapping we have to keep: the channel ids are allocated
 * alongside every other channel in the system and are not contiguous. Channels we
 * were not given a client for hold PAGER_NO_CLIENT.
 */
#define PAGER_NO_CLIENT 0xFF
static uint8_t client_of_channel[MICROKIT_MAX_CHANNELS];


#ifdef PAGER_INSTRUMENTATION
    uint64_t mapping_latencies[PAGER_MAX_SAMPLES];
    uint64_t received_faults[PAGER_MAX_SAMPLES];
    uint64_t return_faults[PAGER_MAX_SAMPLES];
    uint64_t fault_idx = 0;
#endif

void init(void)
{
    if (!pager_config_check_magic(&pager_config)) {
        sddf_printf("pager: invalid config magic\n");
        return;
    }

    memset(client_of_channel, PAGER_NO_CLIENT, sizeof(client_of_channel));
    for (uint8_t i = 0; i < pager_config.num_clients; ++i) {
        client_of_channel[pager_config.clients[i].id] = pager_config.clients[i].fault_id;
    }

    allocator_init(&pager_config);
    untyped_init((uintptr_t)pager_config.bootinfo.vaddr,
                 microkit_cspace_root_slot_to_cptr(pager_config.untypeds.slot));
    /*
     * The scratch memory holds the folio metadata at its base and is the arena the
     * shadow page tables are allocated from above that.
     */
    frame_table_init((uintptr_t)pager_config.memory.vaddr,
                     microkit_cspace_root_slot_to_cptr(pager_config.frames.slot),
                     microkit_cspace_root_slot_to_cptr(pager_config.zero_page_copies.slot),
                     microkit_cspace_root_slot_to_cptr(pager_config.frame_copies.slot));
    page_table_init((uintptr_t)pager_config.memory.vaddr + FOLIO_MEMORY_SIZE,
                    pager_config.memory.size - FOLIO_MEMORY_SIZE,
                    microkit_cspace_root_slot_to_cptr(pager_config.paging_structures.slot));
    frame_table_zero_gzp(microkit_cspace_root_slot_to_cptr(PAGER_OWN_VSPACE_SLOT));
    process_init(microkit_cspace_root_slot_to_cptr(pager_config.process_cspaces.slot),
                 pager_config.num_clients);
}

void notified(microkit_channel ch)
{
    // does nothing for now.
}

/**
 * Service a client's VM fault: create the intermediary paging structures, map a page
 * and reply so the client retries the access.
 *
 * `child` is the fault id the system description gave this client, which sdfgen
 * allocates equal to the client's index, so it indexes every per-client array here.
 */
seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    #ifdef PAGER_INSTRUMENTATION
    uint64_t ct_before_map, ct_after_map;
    uint64_t ct_received_fault = read_cntpct();
    #endif
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
    uint64_t fsc = fsr & 0x3F;
    bool is_write = (fsr >> 6) & 1;
    #ifdef PAGER_INSTRUMENTATION
    uintptr_t heap_base = pager_config.clients[child].mmap_base;
    bool measure = fault_addr >= heap_base && fault_addr - heap_base < PAGER_HEAP_SIZE
                   && fault_idx < PAGER_MAX_SAMPLES;
    #endif
    struct folio *folio;
    uint32_t frame;

    // // TODO: implement access flag faults.
    // if (fsc >= 0x08 && fsc <= 0x0B) {
    //     // Access flag fault (level 0–3)
    //     // unset access flag
    // }
    // Nothing but a translation or a permission fault is serviceable, and
    // classifying first keeps an unserviceable one from allocating anything.
    if (fsc < 0x04 || (fsc > 0x07 && fsc < 0x0D) || fsc > 0x0F) {
        sddf_printf("unhandled fault status code 0x%lx at 0x%lx ip 0x%lx\n", fsc, fault_addr,
            microkit_mr_get(0));
        return seL4_False;
    }
    *reply_msginfo = microkit_msginfo_new(0, 0);

    pte_t *page_entry = make_page_table_entry(fault_addr, child);
    if (!page_entry) {
        sddf_printf("out of shadow page tables for 0x%lx\n", fault_addr);
        return seL4_False;
    }

    // Translation fault (level 0–3)
    if (fsc <= 0x07) {
        // if it is a read fault, map global zero page.
        if (!is_write) {
            frame = get_gzp();
            #ifdef PAGER_INSTRUMENTATION
            ct_before_map = read_cntpct();
            #endif
            seL4_Error err = seL4_ARM_Page_Map(gzp_cptr(frame), vspaces[child], fault_addr, create_cap_rights(false), seL4_ARM_Default_VMAttributes);
            #ifdef PAGER_INSTRUMENTATION
            ct_after_map = read_cntpct();
            #endif
            if (err) {
                sddf_printf("error occured on map frame zero %d\n", err);
            }
            // add global zero page to the frame cap.
            insert_frame_to_page(frame, page_entry);

            #ifdef PAGER_INSTRUMENTATION
            if (measure) {
                mapping_latencies[fault_idx] = ct_after_map - ct_before_map;
                received_faults[fault_idx] = ct_received_fault;
                
                fault_idx++;
                return_faults[fault_idx - 1] = read_cntpct();
            }
            #endif
            return seL4_True;
        }
        folio = get_frame();
        if (!folio) {
            sddf_printf("out of frames for 0x%lx\n", fault_addr);
            return seL4_False;
        }
        frame = folio->frame_page;
        insert_frame_to_page(frame, page_entry);
        *page_entry |= DESC_NG;
    }
    // Permission fault (level 1–3)
    else {
        // if global zero page get new frame
        if (!(*page_entry & DESC_NG)) {
            uint32_t gzp = get_frame_from_page(*page_entry);
            folio = get_frame();
            if (!folio) {
                sddf_printf("out of frames for 0x%lx\n", fault_addr);
                return seL4_False;
            }
            frame = folio->frame_page;
            insert_frame_to_page(frame, page_entry);
            *page_entry |= DESC_NG;
            // The map below overwrites the zero page's PTE, so the copy only
            // needs unmapping before it is handed out again.
            put_dirty_gzp(gzp);
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
    }

    // do mapping
    #ifdef PAGER_INSTRUMENTATION
    ct_before_map = read_cntpct();
    #endif
    seL4_Error err = seL4_ARM_Page_Map(frame_cptr(frame), vspaces[child], fault_addr, create_cap_rights(true), seL4_ARM_Default_VMAttributes);
    #ifdef PAGER_INSTRUMENTATION
    ct_after_map = read_cntpct();
    #endif
    if (err) {
        sddf_printf("error occured on map frame %d\n", err);
    }
    #ifdef PAGER_INSTRUMENTATION
    if (measure) {
        mapping_latencies[fault_idx] = ct_after_map - ct_before_map;
        received_faults[fault_idx] = ct_received_fault;
        
        fault_idx++;
        return_faults[fault_idx - 1] = read_cntpct();
    }
    #endif
    return seL4_True;
}

seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo)
{
    if (ch >= MICROKIT_MAX_CHANNELS || client_of_channel[ch] == PAGER_NO_CLIENT) {
        sddf_printf("pager: protected procedure call on unconfigured channel %u\n", ch);
        return microkit_msginfo_new(0, 0);
    }
#ifdef PAGER_INSTRUMENTATION
    if (microkit_msginfo_get_label(msginfo) == PAGER_INSTRUMENTATION_TAG) {
        /* MR0 = sample index in; reply MR0 = faults recorded, MR1..3 = that sample. */
        uint64_t i = microkit_mr_get(0);
        microkit_mr_set(0, fault_idx);
        if (i >= fault_idx) {
            return microkit_msginfo_new(0, 1);
        }
        microkit_mr_set(1, mapping_latencies[i]);
        microkit_mr_set(2, received_faults[i]);
        microkit_mr_set(3, return_faults[i]);
        return microkit_msginfo_new(0, 4);
    }
#endif
    microkit_mr_set(0, pager_mem_call(msginfo, client_of_channel[ch]));
    return microkit_msginfo_new(0, 1);
}
