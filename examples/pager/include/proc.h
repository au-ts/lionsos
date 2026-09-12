#include <sel4/sel4.h>
#include <microkit.h>
#include <stdbool.h>
#include <stdint.h>

#include "cspace.h"
#include "page_table.h"

#define PT_LOAD 0x0000000000400000ULL
#define MAX_CHILDREN 10
#define ELF_SIZE 1500
#define PROCESS_CNODE_SIZE_BITS 12
#define PROCESS_VSPACE_SLOT 1

#define PROCESS_FORK_OK 0
#define PROCESS_FORK_INVALID -1
#define PROCESS_FORK_NO_SLOTS -2
#define PROCESS_FORK_CAP -3

typedef pte_t *(*process_page_entry_fn)(uintptr_t vaddr, uint32_t child);
struct process {
    seL4_CPtr vspace;
    seL4_CPtr cspace;
    seL4_CPtr ipc_buffer;
    seL4_CPtr tcb;
    seL4_CPtr sched_context;
    seL4_Word asid_pool;
    seL4_Word asid;
    uint32_t parent_pid;
    uint32_t pid;
    bool allocated;
};

int process_fork(struct process *processes, uint32_t parent, uint32_t child,
                 seL4_CPtr process_cnodes_cptr, cnode_specs_t *untyped,
                 seL4_CPtr frame_cnode_cptr, seL4_CPtr gzp_cnode_cptr,
                 pgd_t *page_tables,
                 uint32_t *vspaces, uint32_t elf_caps[][ELF_SIZE],
                 uint32_t *elf_sizes, process_page_entry_fn make_page_entry,
                 seL4_CapRights_t (*cap_rights)(bool is_write));


// create vspace, then assign asid pool to vspace
// create cspace
// create IPC buffer
// create a TCB and configure it.
// create scheduling context
// set scheduling params.