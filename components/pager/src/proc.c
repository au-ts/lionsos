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
static uint32_t process_cnode_slots;
/* Slot 0 of the CNode holds its own self-reference, so the processes start above it. */
static uint32_t next_process_cnode_slot = 1;
static uint8_t num_static_processes;

#define PROCESS_CNODE_SLOTS 2

static int allocate_process(struct process *process, uint32_t pid,
							uint32_t parent)
{
	uint32_t cnode_slot = next_process_cnode_slot;
	seL4_Error error;

	if (cnode_slot + PROCESS_CNODE_SLOTS > process_cnode_slots) return PROCESS_FORK_NO_SLOTS;
	if (vspaces[pid] == 0) return PROCESS_FORK_CAP;

	error = untyped_alloc(seL4_CapTableObject, PROCESS_CSPACE_SIZE_BITS,
						  cnode_slot, process_cnode_cptr, 1);
	if (error != seL4_NoError) return PROCESS_FORK_CAP;

	error = untyped_alloc(seL4_TCBObject, 0, cnode_slot + 1, process_cnode_cptr, 1);
	if (error != seL4_NoError) return PROCESS_FORK_CAP;

	/* TODO: neither these slots nor the channel minted below are reclaimed if a
	 * process fails to start. */
	next_process_cnode_slot = cnode_slot + PROCESS_CNODE_SLOTS;
	process->cspace = process_cnode_cptr + cnode_slot;
	process->tcb = process_cnode_cptr + cnode_slot + 1;
	process->vspace = vspaces[pid];
	process->ppc_ep_slot = processes[parent].ppc_ep_slot;
	process->parent_pid = parent;
	process->pid = pid;
	process->allocated = true;
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
	insert_frame_to_page(child_frame_cptr, child_entry);
	if (parent_entry & DESC_NG) {
		struct folio *folio = get_folio_from_idx(frame);
		++folio->refcount;
	}
	return PROCESS_FORK_OK;
}

// TODO: clone leaf fn may be suboptimal... small difference though..
// TODO: stack pages must also be copied.
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


static int map_ipc_buffer(uint32_t child)
{
	struct folio *folio = get_frame();
	if (!folio) return PROCESS_FORK_CAP;
	uint32_t frame = folio->frame_page;

	frame_table_zero_frame(frame);

	pte_t *entry = make_page_table_entry(IPC_LOCATION, child);
	if (!entry) {
		put_frame(folio);
		return PROCESS_FORK_CAP;
	}
	insert_frame_to_page(frame, entry);
	*entry |= DESC_NG;

	seL4_Error error = seL4_ARM_Page_Map(frame_cptr(frame), vspaces[child], IPC_LOCATION,
										 create_cap_rights(true),
										 seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever);
	if (error != seL4_NoError) {
		sddf_printf("fork: could not map the IPC buffer of %u: %d\n", child, error);
		return PROCESS_FORK_CAP;
	}
	processes[child].ipc_buffer = frame_cptr(frame);
	return PROCESS_FORK_OK;
}


static int mint_endpoints(uint32_t child)
{
	seL4_CPtr self_ep_cnode = microkit_cspace_root_slot_to_cptr(pager_config.pager_caps.slot);
	seL4_CPtr cspace = processes[child].cspace;
	seL4_Error error;

	uint8_t channel = pager_claim_channel(child);
	if (channel == PAGER_NO_CHANNEL) return PROCESS_FORK_NO_SLOTS;

	error = seL4_CNode_Mint(cspace, PROCESS_FAULT_EP_SLOT, PROCESS_CSPACE_SIZE_BITS,
						self_ep_cnode, PAGER_SELF_EP_SLOT, PAGER_CNODE_DEPTH ,create_ep_cap_rights(),
						MICROKIT_FAULT_BADGE | child);
	if (error != seL4_NoError) return PROCESS_FORK_CAP;

	error = seL4_CNode_Mint(cspace, processes[child].ppc_ep_slot, PROCESS_CSPACE_SIZE_BITS,
						self_ep_cnode, PAGER_SELF_EP_SLOT, PAGER_CNODE_DEPTH, create_ep_cap_rights(),
						MICROKIT_PPC_BADGE | channel);
	if (error != seL4_NoError) return PROCESS_FORK_CAP;
	return PROCESS_FORK_OK;
}

/**
 * TODO: the process is configured but cannot run yet. It still needs
 * - a scheduling context: seL4_SchedControl_Configure, seL4_TCB_SetSchedParams, which
 *   also installs the fault endpoint minted above. Both need a SchedControl cap the
 *   pager is not given; pre-created scheduling contexts in pager_caps are the way in,
 *   the same way the VSpaces above are pre-created.
 * - its registers: the parent's, with the fork() return value zeroed, through the cap to
 *   the parent's TCB the tool leaves at BASE_TCB_CAP + its fault id.
 * - its own endpoint, reply object and notification before it reaches libmicrokit's
 *   event loop, and the stack clone_pages() does not copy yet.
 */
int process_fork(uint32_t parent, uint32_t child)
{
	if (parent >= PAGER_MAX_CLIENTS || child >= PAGER_MAX_CLIENTS || parent == child ||
		!processes[parent].allocated || processes[child].allocated) {
		return PROCESS_FORK_INVALID;
	}
	// cspace, vspace and tcb
	int result = allocate_process(&processes[child], child, parent);
	if (result != PROCESS_FORK_OK) return result;
	clone_pages(parent, child);

	result = map_ipc_buffer(child);
	if (result != PROCESS_FORK_OK) goto failed;

	result = mint_endpoints(child);
	if (result != PROCESS_FORK_OK) goto failed;

	seL4_Error error = seL4_TCB_Configure(processes[child].tcb,
										  processes[child].cspace,
										  seL4_CNode_CapData_new(0, PROCESS_CSPACE_GUARD_SIZE).words[0],
										  vspaces[child], 0,
										  IPC_LOCATION, processes[child].ipc_buffer);
	if (error != seL4_NoError) {
		sddf_printf("fork: could not configure the TCB of %u: %d\n", child, error);
		result = PROCESS_FORK_CAP;
		goto failed;
	}
	return PROCESS_FORK_OK;

failed:
	processes[child].allocated = false;
	return result;
}

long pager_fork(microkit_child parent)
{
    if (parent >= PAGER_MAX_CLIENTS || !processes[parent].allocated) {
        return -EINVAL;
    }
	// allocate pid
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

void process_init(seL4_CPtr process_cnode, uint8_t process_cnode_size_bits,
                  uint8_t num_clients)
{
    process_cnode_cptr = process_cnode;
    process_cnode_slots = 1u << process_cnode_size_bits;
    for (uint8_t i = 0; i < num_clients; ++i) {
        processes[i].allocated = true;
        processes[i].pid = i;
        processes[i].vspace = vspaces[i];
        processes[i].ppc_ep_slot = PROCESS_PPC_EP_SLOT(pager_config.clients[i].client_id);
    }
    num_static_processes = num_clients;
}
