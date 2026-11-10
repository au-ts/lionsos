/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "proc.h"
#include "conn.h"
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
static uint8_t num_static_processes;

static inline seL4_Word block_slot(uint32_t pid, uint32_t offset)
{
	return PROCESS_BLOCK(pid) + offset;
}

static seL4_Error retype_into_block(uint32_t pid, uint32_t offset, seL4_Word type,
									seL4_Word size_bits)
{
	return untyped_alloc(type, size_bits, block_slot(pid, offset), process_cnode_cptr, 1);
}

static void free_process_block(uint32_t pid)
{
	for (uint32_t i = 0; i < PROCESS_BLOCK_SLOTS; ++i) {
		seL4_CNode_Delete(process_cnode_cptr, block_slot(pid, i), PAGER_CNODE_DEPTH);
	}
}

static int allocate_process(struct process *process, uint32_t pid,
							uint32_t parent)
{
	if (retype_into_block(pid, PROCESS_BLOCK_CSPACE, seL4_CapTableObject,
						  PROCESS_CSPACE_SIZE_BITS) != seL4_NoError ||
		retype_into_block(pid, PROCESS_BLOCK_TCB, seL4_TCBObject, 0) != seL4_NoError ||
		retype_into_block(pid, PROCESS_BLOCK_SC, seL4_SchedContextObject,
						  PROCESS_SC_SIZE_BITS) != seL4_NoError ||
		retype_into_block(pid, PROCESS_BLOCK_NTFN, seL4_NotificationObject, 0) != seL4_NoError) {
		return PROCESS_FORK_CAP;
	}
	process->cspace = process_cnode_cptr + block_slot(pid, PROCESS_BLOCK_CSPACE);
	process->tcb = process_cnode_cptr + block_slot(pid, PROCESS_BLOCK_TCB);
	process->sched_context = process_cnode_cptr + block_slot(pid, PROCESS_BLOCK_SC);
	process->ntfn = process_cnode_cptr + block_slot(pid, PROCESS_BLOCK_NTFN);
	process->fault_ep = process_cnode_cptr + block_slot(pid, PROCESS_BLOCK_FAULT_EP);

	if (untyped_alloc(seL4_ReplyObject, 0, PROCESS_REPLY_SLOT, process->cspace, 1) != seL4_NoError) {
		return PROCESS_FORK_CAP;
	}

	process->vspace = vspaces[pid];
	process->ppc_ep_slot = processes[parent].ppc_ep_slot;
	process->priority = processes[parent].priority;
	process->template_client = processes[parent].template_client;
	process->budget = processes[parent].budget;
	process->period = processes[parent].period;
	process->parent_pid = parent;
	process->pid = pid;
	process->allocated = true;
	return PROCESS_FORK_OK;
}

static int clone_leaf(uint32_t parent, uint32_t child, uintptr_t vaddr,
					  pte_t parent_entry)
{
	pte_t *child_entry = make_page_table_entry(vaddr, child);
	if (!child_entry) return PROCESS_FORK_CAP;
	uint32_t frame = get_frame_from_page(parent_entry);
	seL4_Error error;

	if (!(parent_entry & DESC_NG)) {
		uint32_t gzp = get_gzp();
		if (!gzp) return PROCESS_FORK_CAP;
		error = seL4_ARM_Page_Map(gzp_cptr(gzp), vspaces[child], vaddr,
								  create_cap_rights(false), seL4_ARM_Default_VMAttributes);
		if (error != seL4_NoError) {
			put_gzp(gzp);
			return PROCESS_FORK_CAP;
		}
		*child_entry = 0;
		insert_frame_to_page(gzp, child_entry);
		return PROCESS_FORK_OK;
	}

	struct folio *folio = get_folio_from_idx(frame);
	if (!folio) return PROCESS_FORK_INVALID;

	error = seL4_ARM_Page_Map(mapping_cptr(parent_entry), vspaces[parent], vaddr,
							  create_cap_rights(false), seL4_ARM_Default_VMAttributes);
	if (error != seL4_NoError) return PROCESS_FORK_CAP;

	uint32_t copy = copy_frame_cap(frame);
	if (!copy) return PROCESS_FORK_CAP;
	error = seL4_ARM_Page_Map(frame_copy_cptr(copy), vspaces[child], vaddr,
							  create_cap_rights(false), seL4_ARM_Default_VMAttributes);
	if (error != seL4_NoError) {
		delete_copied_frame_cap(copy);
		return PROCESS_FORK_CAP;
	}
	*child_entry = DESC_NG;
	insert_frame_to_page(frame, child_entry);
	insert_copy_to_page(copy, child_entry);
	++folio->refcount;
	return PROCESS_FORK_OK;
}

// TODO: clone leaf fn may be suboptimal... small difference though..
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
					/* The child gets an IPC buffer of its own from map_ipc_buffer(). */
					if (vaddr == IPC_LOCATION) continue;
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

	error = seL4_CNode_Mint(process_cnode_cptr, block_slot(child, PROCESS_BLOCK_FAULT_EP),
						PAGER_CNODE_DEPTH, self_ep_cnode, PAGER_SELF_EP_SLOT, PAGER_CNODE_DEPTH,
						create_ep_cap_rights(), MICROKIT_FAULT_BADGE | child);
	if (error != seL4_NoError) return PROCESS_FORK_CAP;

	/* Where a Microkit PD has it, although on MCS the kernel only uses the copy above. */
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

static int start_process(uint32_t parent, uint32_t child)
{
	struct process *process = &processes[child];
	seL4_Error error;

	error = seL4_CNode_Copy(process->cspace, PROCESS_INPUT_SLOT, PROCESS_CSPACE_SIZE_BITS,
							process_cnode_cptr, block_slot(child, PROCESS_BLOCK_NTFN),
							PAGER_CNODE_DEPTH, seL4_AllRights);
	if (error != seL4_NoError) return PROCESS_FORK_CAP;
	error = seL4_TCB_BindNotification(process->tcb, process->ntfn);
	if (error != seL4_NoError) {
		sddf_printf("fork: could not bind the notification of %u: %d\n", child, error);
		return PROCESS_FORK_START;
	}

	error = seL4_SchedControl_ConfigureFlags(microkit_cspace_root_slot_to_cptr(pager_config.sched_control),
											 process->sched_context, process->budget,
											 process->period, 0, 0, 0);
	if (error != seL4_NoError) {
		sddf_printf("fork: could not configure the scheduling context of %u: %d\n", child, error);
		return PROCESS_FORK_START;
	}
	error = seL4_TCB_SetSchedParams(process->tcb, processes[parent].tcb, process->priority,
									process->priority, process->sched_context, process->fault_ep);
	if (error != seL4_NoError) {
		sddf_printf("fork: could not set the scheduling parameters of %u: %d\n", child, error);
		return PROCESS_FORK_START;
	}

	seL4_UserContext regs;
	const seL4_Word num_regs = sizeof(regs) / sizeof(seL4_Word);
	error = seL4_TCB_ReadRegisters(processes[parent].tcb, false, 0, num_regs, &regs);
	if (error != seL4_NoError) {
		sddf_printf("fork: could not read the registers of %u: %d\n", parent, error);
		return PROCESS_FORK_START;
	}
	regs.pc += 4;
	regs.x1 = seL4_MessageInfo_new(0, 0, 0, 1).words[0];
	regs.x2 = 0;
	sddf_dprintf("fork: starting %u at 0x%lx\n", child, regs.pc);
	error = seL4_TCB_WriteRegisters(process->tcb, true, 0, num_regs, &regs);
	if (error != seL4_NoError) {
		sddf_printf("fork: could not start %u: %d\n", child, error);
		return PROCESS_FORK_START;
	}
	return PROCESS_FORK_OK;
}

int process_fork(uint32_t parent, uint32_t child)
{
	if (parent >= PAGER_MAX_CLIENTS || child >= PAGER_MAX_CLIENTS || parent == child ||
		!processes[parent].allocated || processes[child].allocated) {
		return PROCESS_FORK_INVALID;
	}
	if (pager_config.sched_control == 0) {
		return PROCESS_FORK_NO_SCHED_CONTROL;
	}
	if (PROCESS_BLOCK(child) + PROCESS_BLOCK_SLOTS > process_cnode_slots) {
		return PROCESS_FORK_NO_SLOTS;
	}
	if (vspaces[child] == 0) return PROCESS_FORK_CAP;

	int result = allocate_process(&processes[child], child, parent);
	if (result != PROCESS_FORK_OK) goto failed;

	result = clone_pages(parent, child);
	if (result != PROCESS_FORK_OK) {
		sddf_printf("fork: could not clone the address space of %u: %d\n", parent, result);
		goto failed;
	}

	result = map_ipc_buffer(child);
	if (result != PROCESS_FORK_OK) goto failed;

	result = mint_endpoints(child);
	if (result != PROCESS_FORK_OK) goto failed;

	result = conn_connect(child, processes[child].template_client, processes[child].cspace,
						  process_cnode_cptr, block_slot(child, PROCESS_BLOCK_NTFN));
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

	result = start_process(parent, child);
	if (result != PROCESS_FORK_OK) goto failed;

	return PROCESS_FORK_OK;

failed:
	conn_release(child);
	free_process_block(child);
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
        return result == PROCESS_FORK_NO_SCHED_CONTROL ? -ENOSYS : -ENOMEM;
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

static void adopt_elf_frames(uint8_t client)
{
	seL4_CPtr elf_cnode = microkit_cspace_root_slot_to_cptr(pager_config.elf_caps.slot);

	for (uint32_t i = 0; i < elf_sizes[client]; ++i) {
		uintptr_t vaddr = elf_vaddrs[client][i];
		pte_t *entry = make_page_table_entry(vaddr, client);
		if (!entry) {
			sddf_printf("pager: out of shadow page tables for the ELF of %u\n", client);
			return;
		}
		struct folio *folio = frame_table_adopt(elf_cnode, elf_caps[client][i]);
		if (!folio) return;
		seL4_Error error = seL4_ARM_Page_Map(frame_cptr(folio->frame_page), vspaces[client],
											 vaddr, create_cap_rights(true),
											 seL4_ARM_Default_VMAttributes);
		if (error != seL4_NoError) {
			sddf_printf("pager: could not map the ELF frame at 0x%lx of %u: %d\n", vaddr,
						client, error);
			return;
		}
		*entry = DESC_NG;
		insert_frame_to_page(folio->frame_page, entry);
	}
}

void process_init(seL4_CPtr process_cnode, uint8_t process_cnode_size_bits,
                  uint8_t num_clients)
{
    process_cnode_cptr = process_cnode;
    process_cnode_slots = 1u << process_cnode_size_bits;
    if (PROCESS_BLOCK(PAGER_MAX_CLIENTS - 1) + PROCESS_BLOCK_SLOTS > process_cnode_slots) {
        sddf_printf("pager: process_cspaces has %u slots, too few to fork %u processes\n",
                    process_cnode_slots, PAGER_MAX_CLIENTS);
    }
    for (uint8_t i = 0; i < num_clients; ++i) {
        pager_client_resource_t *client = &pager_config.clients[i];
        processes[i].allocated = true;
        processes[i].pid = i;
        processes[i].vspace = vspaces[i];
        processes[i].tcb = BASE_TCB_CAP + client->fault_id;
        processes[i].priority = client->priority;
        processes[i].template_client = i;
        processes[i].budget = client->budget;
        processes[i].period = client->period;
        processes[i].ppc_ep_slot = PROCESS_PPC_EP_SLOT(client->client_id);
        adopt_elf_frames(i);
    }
    num_static_processes = num_clients;
}
