# Memory Pager for LionsOS
Minimal demand-paging service (`pager` PD) that backs the `client` PD on seL4/Microkit (AArch64) via fault handling, shadow 4-level page tables, CoW global zero page, and bump/slab allocators for frames and paging structures.

This pager is meant for benchmarking minor page faults of anonymous memory like in Figure 8 of the [HongMeng paper](https://www.usenix.org/system/files/osdi24-chen-haibo.pdf). It is also to be used to benchmark the address space copy semantics of `fork()` but this is WIP.
It can be used for real applications but should only really be used for a *static* system as frame objects and paging objects do not get untyped. 
<!-- TODO: write a quick description of the pager.
TODO: big caveats like only for aarch64, supports the maaxboard and qemu. -->

## 1. How to run
### 1.1 Dependencies
- [seL4](https://github.com/SEL4/sel4): specifically tag **16.0.0**
- [rust](https://github.com/au-ts/rust-sel4): specifically branch *carrells_demo*
- [Microkit](https://github.com/au-ts/microkit/tree/joshua/mglru3): specifically branch **joshua/mglru3**
- [microkit_sdf_gen](https://github.com/au-ts/microkit_sdf_gen/tree/joshua/mglru3): specifically branch **joshua/mglru**
- [LionsOs](https://github.com/au-ts/lionsos/tree/joshua/simple-pager): specifically branch **joshua/simple-pager**

### 1.2 How to run examples/pager
The example implementation in examples/pager shows the pager running with a client. The client runs a microbenchmark measuring minor page faults.
1. Install [microkit_sdf_gen](https://github.com/au-ts/microkit_sdf_gen/tree/joshua/mglru3) by running the following:
```sh
python3 -m venv venv
./venv/bin/pip install .
```
2. Compile [Microkit](https://github.com/au-ts/microkit/tree/joshua/mglru3) for maaxboard and qemu_virt_aarch64 by running the following:
```sh
    # edit the path pointing to rust-seL4 first.
    python build_sdk.py --sel4=/path/to/seL4 --boards=qemu_virt_aarch64,maaxboard --configs=debug,benchmark --skip-docs --skip-tar
```
3. Download [LionsOs](https://github.com/au-ts/lionsos/tree/joshua/simple-pager) submodule by running the following in the [LionsOs](https://github.com/au-ts/lionsos/tree/joshua/simple-pager) directory:
```sh
git submodule update --init --recursive
```
4. Build and run the example:
```sh
cd /dir/to/lionos/examples/pager
```
For qemu:
```sh
cd /dir/to/lionos/examples/pager
#set environment variables
export MICROKIT_BOARD="qemu_virt_aarch64"
make qemu
```
For maaxboard:
```sh
export MICROKIT_BOARD="maaxboard"
make
# use the resulting build/loader.img to boot Maaxboard.
```
## 2. Components
<!-- ![alt text](documentation/pager_system_diagram.svg) -->
<img src="documentation/pager_system_diagram.svg" width="3000">
Note: paging in/out not implemented yet. Pager only deals with anonymous memory, no page cache implementation yet.

- **Client**
A protection domain (process) that runs dependent of the `pager`. Configured as any LionsOS protection domain, except that it must be a child of the `pager`.

- **Pager**
A protection domain that receives all remaining untyped memory after system initialisation. When its child has a fault (only VM faults), the pager creates and maps intermediary paging structures and frames to service the VM fault.

- **LionsOS LibC**
The LionsOS libC is how the client interfaces with the other operating system components like the file system, the serial device and timer (also networking but removed for this example). This is required at this stage as memory allocations (sys_mmap & sys_brk) are implemented as part of the libC, which is a per PD library. It is part of the client protection domain.

## 3. Implementation details
### 3.1 Source layout
Each module owns its own state and exposes it through the matching header in `include/`.

| File | Responsibility |
| --- | --- |
| `include/pager.h` | The contract with `meta.py` and the Microkit tool: the root CSpace slots, the patched-in symbols (`pager_memory`, `remaining_untypeds_vaddr`, `vspaces`) and the slab sizes. |
| `src/pager.c` | The Microkit entry points only: `init()`, `notified()`, `fault()` and `protected()`. |
| `src/untyped.c` | Owns the untypeds left after initialisation; everything that retypes an object goes through `untyped_alloc()`. |
| `src/cspace.c` | Generic CSpace/untyped bookkeeping (`cnode_specs_t`) and `create_cap_rights()`. |
| `src/frame_table.c` | Folio metadata, the frame free list and the global zero page cap copies. |
| `src/page_table.c` | Shadow page tables: the intermediary paging structure free list, `make_page_table_entry()` and `unmap_range()`. |
| `src/proc.c` | The process table and `fork()`. |
| `src/mem.c` | The `brk`/`mmap`/`munmap`/`fork` PPCs from the client's libc. |
| `src/bitmap.c` | The block allocator `mem.c` hands out the client's mmap arena with. |
| `src/client.c` | The client PD, which selects a benchmark from `benchmarks/`. |

### 3.2 System description (metaprogram)
- **Pager:**
```py
pager = SystemDescription.ProtectionDomain("pager", "pager.elf", priority=198)
```
The pager is a special protection domain. The pager **must** be named "pager" as microkit gives it the vspace caps for its children.
- **BootInfo:**
```py
pager_bootinfo = SystemDescription.MemoryRegion(sdf, "pager_bootinfo", 0x2000, prefill_bootinfo="post_capdl_untypeds")
pager_bootinfo_map = SystemDescription.Map(pager_bootinfo, 0x8002000000, "rw", setvar_vaddr="remaining_untypeds_vaddr")
pager.add_map(pager_bootinfo_map)
```
The BootInfo memory region should be specified with `prefill_bootinfo="post_capdl_untypeds"` so that metadata about untyped memory is written to this memory region. This should be mapped into the pager so that the pager has access to information about the remaining untypeds.

- **CNodes:**
We need to create CNodes:
- `remaining_untypeds`: created with `post_capdl_untypeds=True` as this CNode contains all remaining untyped caps.
-  `pagers_empty_cnode`: Created frame caps placed in this CNode.
- `pager_gzp_cnode`: Created copies of the global zero page cap placed in this CNode.
- `pager_ips_cnode`: Created intermediary paging strucure (page table) caps placed in this CNode.
```py
remaining_untypeds = SystemDescription.CNode("remaining_untypeds", True, 9)
pagers_empty_cnode = SystemDescription.CNode("pagerspace", False, 20)
pager_gzp_cnode = SystemDescription.CNode("gzp", False, 20)
pager_ips_cnode = SystemDescription.CNode("ips_cnode", False, 20)
```

<!-- add to -->
Create mappings with `type=Cnode` and `pd=None` as this CNode will not be shared.
```py
pagers_empty_cnode_map = SystemDescription.CapMap(SystemDescription.CapMap.CapType.Cnode, None, pagers_empty_cnode, 2)
pager_gzp_cnode_map = SystemDescription.CapMap(SystemDescription.CapMap.CapType.Cnode, None, pager_gzp_cnode, 4)
pager_ips_cnode_map = SystemDescription.CapMap(SystemDescription.CapMap.CapType.Cnode, None, pager_ips_cnode, 3)
pager_remaining_untypeds = SystemDescription.CapMap(SystemDescription.CapMap.CapType.Cnode, None, remaining_untypeds, 1)
```
Add these mappings to the pager.

<!-- mappings -->
```py
pager.add_cap_map(pager_gzp_cnode_map)
pager.add_cap_map(pager_ips_cnode_map)
pager.add_cap_map(pager_remaining_untypeds)
pager.add_cap_map(pagers_empty_cnode_map)
```
- **Memory regions:**
This memory region is used to create shadow page tables.
```py
pager_memory = SystemDescription.MemoryRegion(sdf, "pager_memory_mr ", 0x2000000)
```
- **Client:**
The `client` protection domain is also a normal protection domain, but a child of the `pager` so that fault from the client are handled by the `pager`.
```py
client = ProtectionDomain("client", "client.elf", priority=1, backed = False)
pager.add_child_pd(client)
```

### 3.3 [Microkit](https://github.com/au-ts/microkit/tree/joshua/mglru3) changes
- Gives vspace caps of pager's children to the pager via the elf.
- Option to have unbacked stack pages for protection domains.
- Also: https://github.com/au-ts/microkit/blob/carrells_demo/CHANGES.md.
### 3.4 [sdfgen](https://github.com/au-ts/microkit_sdf_gen/tree/joshua/mglru3) changes
- Option to have unbacked stack pages for protection domains. `backed=False`
- Also: https://github.com/au-ts/microkit/blob/carrells_demo/CHANGES.md.
## 4. Caveats
- No untyping of typed objects implemented yet: A system using the pager would only be able to dynamically create caps and paging structures.
- See 5. Future direction & WIP.
## 5. Future direction & WIP
- Memory-related syscalls: WIP - necessary for frees.
- Demand paging with page replacement algorithm.
- filesystem page cache: This would require extensive rework of the filesystem.
