# Memory Pager for LionsOS
Minimal demand-paging service (`pager` PD) that backs the `client` PD on seL4/Microkit (AArch64) via fault handling, shadow 4-level page tables, CoW global zero page, and bump/slab allocators for frames and paging structures.

This pager is meant for benchmarking minor page faults of anonymous memory like in Figure 8 of the [HongMeng paper](https://www.usenix.org/system/files/osdi24-chen-haibo.pdf). It can be used for real applications but should only really be used for a *static* system as frame objects and paging objects do not get untyped. 
<!-- TODO: write a quick description of the pager.
TODO: big caveats like only for aarch64, supports the maaxboard and qemu. -->

## 1. How to run
### 1.1 Dependencies
- [seL4](https://github.com/SEL4/sel4): specifically tag **15.0.0**
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
    TODO: add command for microkit compilation
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
### 3.1 System description (metaprogram)
### 3.2 [Microkit](https://github.com/au-ts/microkit/tree/joshua/mglru3) changes
### 3.3 [sdfgen](https://github.com/au-ts/microkit_sdf_gen/tree/joshua/mglru3) changes
## 4. Caveats
- 
## 5. Future direction & WIP
- Memory-related syscalls: WIP - necessary for frees.
- Demand paging with page replacement algorithm.
- filesystem page cache.
