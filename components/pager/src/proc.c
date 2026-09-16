/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "proc.h"
#include "cspace.h"
#include "frame_table.h"
#include "page_table.h"
#include "pager.h"
#include "untyped.h"

#include <errno.h>
#include <sddf/util/printf.h>

static struct process processes[PAGER_MAX_CLIENTS];
static seL4_CPtr process_cnode_cptr;
static uint32_t next_process_cnode_slot;
static uint8_t num_static_processes;

static int allocate_process(struct process *process, uint32_t pid,
							uint32_t parent)
{
	uint32_t cnode_slot = next_process_cnode_slot++;
	seL4_CPtr cspace;
	seL4_Error error;

	if (cnode_slot >= 512) return PROCESS_FORK_NO_SLOTS;
	error = untyped_alloc(seL4_CapTableObject, PROCESS_CSPACE_SIZE_BITS,
						  cnode_slot, process_cnode_cptr, 1);
	if (error != seL4_NoError) return PROCESS_FORK_CAP;

	cspace = process_cnode_cptr + cnode_slot;
	process->cspace = cspace;
	process->vspace = cspace + PROCESS_VSPACE_SLOT;
	process->parent_pid = parent;
	process->pid = pid;
	process->allocated = true;

	error = untyped_alloc(seL4_ARM_VSpaceObject, 0, PROCESS_VSPACE_SLOT, cspace, 1);
	if (error != seL4_NoError) {
		process->allocated = false;
		return PROCESS_FORK_CAP;
	}
	return PROCESS_FORK_OK;
}

static int clone_leaf(uint32_t parent, uint32_t child, uintptr_t vaddr,
					  pte_t parent_entry)
{
	pte_t *child_entry = make_page_table_entry(vaddr, child);
	uint32_t frame = get_frame_from_page(parent_entry);
	seL4_Error error;

	*child_entry = parent_entry;
	if (parent_entry & DESC_NG) {
		struct folio *folio = get_folio_from_idx(frame);
		if (!folio) return PROCESS_FORK_INVALID;

		/*
		 * Both mappings must be read-only before either process can
		 * write the shared frame. The resulting permission fault is
		 * resolved by the pager's CoW path.
		 */
		error = seL4_ARM_Page_Map(frame_cptr(frame), vspaces[parent],
								  vaddr, create_cap_rights(false), 0x03);
		if (error != seL4_NoError) return PROCESS_FORK_CAP;
	}
	seL4_CPtr child_frame_cptr = (parent_entry & DESC_NG)
		? frame_cptr(frame) : gzp_cptr(frame);
	error = seL4_ARM_Page_Map(child_frame_cptr, vspaces[child],
							  vaddr, create_cap_rights(false), 0x03);
	if (error != seL4_NoError) return PROCESS_FORK_CAP;

	if (parent_entry & DESC_NG) {
		struct folio *folio = get_folio_from_idx(frame);
		++folio->refcount;
	}
	return PROCESS_FORK_OK;
}

static int clone_pages(uint32_t parent, uint32_t child)
{
	pgd_t *pgd = page_table_root(parent);

	for (uint32_t pgdi = 0; pgdi < PAGE_TABLE_ENTRIES; ++pgdi) {
		pud_t *pud = pgd->entries[pgdi];
		if (!pud) continue;
		for (uint32_t pudi = 0; pudi < PAGE_TABLE_ENTRIES; ++pudi) {
			pd_t *pd = pud->entries[pudi];
			if (!pd) continue;
			for (uint32_t pdi = 0; pdi < PAGE_TABLE_ENTRIES; ++pdi) {
				pt_t *pt = pd->entries[pdi];
				if (!pt) continue;
				for (uint32_t ptei = 0; ptei < PAGE_TABLE_ENTRIES; ++ptei) {
					pte_t entry = pt->entries[ptei];
					if (!entry) continue;
					uintptr_t vaddr = ((uintptr_t)pgdi << PUD_INDEX_SHIFT) |
									  ((uintptr_t)pudi << PD_INDEX_SHIFT) |
									  ((uintptr_t)pdi << PT_INDEX_SHIFT) |
									  ((uintptr_t)ptei << PAGE_SHIFT);
					int result = clone_leaf(parent, child, vaddr, entry);
					if (result != PROCESS_FORK_OK) return result;
				}
			}
		}
	}
	return PROCESS_FORK_OK;
}

/**
 * TODO:
 * - assign vspace to asid pool: seL4_ARM_ASIDPool_Assign
 * - TCB object creation & configuration: seL4_TCB_Configure
 * - IPC buffer mapping: seL4_TCB_SetIPCBuffer
 * - set up scheduling context: seL4_SchedContext_Configure,
 */
int process_fork(uint32_t parent, uint32_t child)
{
	if (parent >= PAGER_MAX_CLIENTS || child >= PAGER_MAX_CLIENTS || parent == child ||
		!processes[parent].allocated || processes[child].allocated) {
		return PROCESS_FORK_INVALID;
	}

	int result = allocate_process(&processes[child], child, parent);
	if (result != PROCESS_FORK_OK) return result;
	vspaces[child] = processes[child].vspace;
	return clone_pages(parent, child);
}

long pager_fork(microkit_child parent)
{
    if (parent >= PAGER_MAX_CLIENTS || !processes[parent].allocated) {
        return -EINVAL;
    }

    uint32_t child = 0;
    for (uint32_t i = num_static_processes; i < PAGER_MAX_CLIENTS; i++) {
        if (!processes[i].allocated) {
            child = i;
            break;
        }
    }
    if (child == 0) {
        sddf_printf("pager_fork: no available process slots\n");
        return -EAGAIN;
    }

    int result = process_fork(parent, child);
    if (result != PROCESS_FORK_OK) {
        sddf_printf("fork(%u, %u) failed: %d\n", parent, child, result);
        return -ENOMEM;
    }
    return child;
}

void fork(uint32_t parent, uint32_t child)
{
    int result = process_fork(parent, child);
    if (result != PROCESS_FORK_OK) {
        sddf_printf("fork(%u, %u) failed: %d\n", parent, child, result);
    }
}

void process_init(seL4_CPtr process_cnode, uint8_t num_clients)
{
    process_cnode_cptr = process_cnode;
    /*
     * The clients the system booted with occupy the first slots: their fault ids are
     * their indices, so a fork() must not hand one of them out again.
     */
    for (uint8_t i = 0; i < num_clients; ++i) {
        processes[i].allocated = true;
        processes[i].pid = i;
    }
    num_static_processes = num_clients;
}
