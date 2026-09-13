

#include "proc.h"
#include "pager.h"

#define PAGE_SIZE 4096ULL
static uint32_t next_process_cnode_slot;

static int allocate_process(struct process *process, uint32_t pid,
							uint32_t parent, seL4_CPtr process_cnodes_cptr,
							cnode_specs_t *untyped)
{
	uint32_t cnode_slot = next_process_cnode_slot++;
	seL4_CPtr cspace;
	seL4_Error error;

	if (cnode_slot >= 512) return PROCESS_FORK_NO_SLOTS;
	error = do_untyped_retype(untyped, seL4_CapTableObject,
							  PROCESS_CNODE_SIZE_BITS, cnode_slot,
							  process_cnodes_cptr);
	if (error != seL4_NoError) return PROCESS_FORK_CAP;

	cspace = process_cnodes_cptr + cnode_slot;
	process->cspace = cspace;
	process->vspace = cspace + PROCESS_VSPACE_SLOT;
	process->parent_pid = parent;
	process->pid = pid;
	process->allocated = true;

	error = do_untyped_retype(untyped, seL4_ARM_VSpaceObject, 0,
							  PROCESS_VSPACE_SLOT, cspace);
	if (error != seL4_NoError) {
		process->allocated = false;
		return PROCESS_FORK_CAP;
	}
	return PROCESS_FORK_OK;
}

static int clone_leaf(uint32_t parent, uint32_t child, uintptr_t vaddr,
					  pte_t parent_entry, uint32_t *vspaces,
					  seL4_CPtr frame_cnode_cptr, seL4_CPtr gzp_cnode_cptr,
					  process_page_entry_fn make_page_entry,
					  seL4_CapRights_t (*cap_rights)(bool is_write))
{
	pte_t *child_entry = make_page_entry(vaddr, child);
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
		error = seL4_ARM_Page_Map(frame_cnode_cptr + frame, vspaces[parent],
								  vaddr, cap_rights(false), 0x03);
		if (error != seL4_NoError) return PROCESS_FORK_CAP;
	}
	seL4_CPtr frame_cptr = (parent_entry & DESC_NG)
		? frame_cnode_cptr + frame : gzp_cnode_cptr + frame;
	error = seL4_ARM_Page_Map(frame_cptr, vspaces[child],
							  vaddr, cap_rights(false), 0x03);
	if (error != seL4_NoError) return PROCESS_FORK_CAP;

	if (parent_entry & DESC_NG) {
		struct folio *folio = get_folio_from_idx(frame);
		++folio->refcount;
	}
	return PROCESS_FORK_OK;
}

static int clone_pages(uint32_t parent, uint32_t child, pgd_t *page_tables,
					   uint32_t *vspaces, seL4_CPtr frame_cnode_cptr,
					   seL4_CPtr gzp_cnode_cptr,
					   process_page_entry_fn make_page_entry,
					   seL4_CapRights_t (*cap_rights)(bool is_write))
{
	for (uint32_t pgdi = 0; pgdi < 512; ++pgdi) {
		pud_t *pud = page_tables[parent].entries[pgdi];
		if (!pud) continue;
		for (uint32_t pudi = 0; pudi < 512; ++pudi) {
			pd_t *pd = pud->entries[pudi];
			if (!pd) continue;
			for (uint32_t pdi = 0; pdi < 512; ++pdi) {
				pt_t *pt = pd->entries[pdi];
				if (!pt) continue;
				for (uint32_t ptei = 0; ptei < 512; ++ptei) {
					pte_t entry = pt->entries[ptei];
					if (!entry) continue;
					uintptr_t vaddr = ((uintptr_t)pgdi << 39) |
									  ((uintptr_t)pudi << 30) |
									  ((uintptr_t)pdi << 21) |
									  ((uintptr_t)ptei << 12);
					int result = clone_leaf(parent, child, vaddr, entry,
											vspaces, frame_cnode_cptr,
											gzp_cnode_cptr,
											make_page_entry, cap_rights);
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
int process_fork(struct process *processes, uint32_t parent, uint32_t child,
				 seL4_CPtr process_cnodes_cptr, cnode_specs_t *untyped,
				 seL4_CPtr frame_cnode_cptr, seL4_CPtr gzp_cnode_cptr,
				 pgd_t *page_tables,
				 uint32_t *vspaces, process_page_entry_fn make_page_entry,
				 seL4_CapRights_t (*cap_rights)(bool is_write))
{
	if (parent >= MAX_CHILDREN || child >= MAX_CHILDREN || parent == child ||
		!processes[parent].allocated || processes[child].allocated) {
		return PROCESS_FORK_INVALID;
	}

	int result = allocate_process(&processes[child], child, parent,
								  process_cnodes_cptr, untyped);
	if (result != PROCESS_FORK_OK) return result;
	vspaces[child] = processes[child].vspace;
	result = clone_pages(parent, child, page_tables, vspaces, frame_cnode_cptr,
						 gzp_cnode_cptr, make_page_entry, cap_rights);
	return result;
}