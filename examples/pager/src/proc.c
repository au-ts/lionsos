

#include "proc.h"
#include "pager.h"

#define PAGE_SIZE 4096ULL
#define PAGE_MASK 0xfffffffffffff000ULL
#define ROUND_DOWN_TO_PAGE(value) ((uintptr_t)(value) & PAGE_MASK)

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
		++folio->refcount;
		// unmap not required for aarch64.
		// error = seL4_ARM_Page_Unmap(frame_cnode_cptr + frame);
		// if (error != seL4_NoError) return PROCESS_FORK_CAP;
		error = seL4_ARM_Page_Map(frame_cnode_cptr + frame, vspaces[parent],
								  vaddr, cap_rights(false), 0x03);
		if (error != seL4_NoError) return PROCESS_FORK_CAP;
	}
	seL4_CPtr frame_cptr = (parent_entry & DESC_NG)
		? frame_cnode_cptr + frame : gzp_cnode_cptr + frame;
	error = seL4_ARM_Page_Map(frame_cptr, vspaces[child],
							  vaddr, cap_rights(false), 0x03);
	return error == seL4_NoError ? PROCESS_FORK_OK : PROCESS_FORK_CAP;
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

static int map_elf(uint32_t child, seL4_CPtr frame_cnode_cptr,
				   uint32_t elf_caps[][ELF_SIZE], uint32_t *elf_sizes,
				   uint32_t *vspaces, process_page_entry_fn make_page_entry,
				   seL4_CapRights_t (*cap_rights)(bool is_write))
{
	for (uint32_t i = 0; i < elf_sizes[child] && i < ELF_SIZE; ++i) {
		uintptr_t vaddr = ROUND_DOWN_TO_PAGE(PT_LOAD + i * PAGE_SIZE);
		pte_t *entry = make_page_entry(vaddr, child);
		*entry = 0;
		insert_frame_to_page(elf_caps[child][i], entry);
		seL4_Error error = seL4_ARM_Page_Map(
			frame_cnode_cptr + elf_caps[child][i], vspaces[child], vaddr,
			cap_rights(false), 0x03);
		if (error != seL4_NoError) return PROCESS_FORK_CAP;
	}
	return PROCESS_FORK_OK;
}

int process_fork(struct process *processes, uint32_t parent, uint32_t child,
				 seL4_CPtr process_cnodes_cptr, cnode_specs_t *untyped,
				 seL4_CPtr frame_cnode_cptr, seL4_CPtr gzp_cnode_cptr,
				 pgd_t *page_tables,
				 uint32_t *vspaces, uint32_t elf_caps[][ELF_SIZE],
				 uint32_t *elf_sizes, process_page_entry_fn make_page_entry,
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
	if (result != PROCESS_FORK_OK) return result;
	return map_elf(child, frame_cnode_cptr, elf_caps, elf_sizes, vspaces,
				   make_page_entry, cap_rights);
}