# RAMFS
<!-- description of the component -->
A simple in-RAM filesystem to be used for testing and benchmarking. **Operations are non-blocking - no need for coroutines**.

## 1. How to run
### 1.1 Dependencies
- [seL4](https://github.com/SEL4/sel4): specifically tag **15.0.0**
- [Microkit](https://github.com/au-ts/microkit/tree/joshua/ramfs): specifically branch **joshua/ramfs**
- [microkit_sdf_gen](https://github.com/au-ts/microkit_sdf_gen/tree/joshua/ramfs): specifically branch **joshua/ramfs**
- [LionsOs](https://github.com/au-ts/lionsos/tree/joshua/ramfs): specifically branch **joshua/ramfs**
### 1.2 How to run examples/pager
The example implementation in examples/pager shows the pager running with a client. The client runs a microbenchmark measuring minor page faults.
1. Install [microkit_sdf_gen](https://github.com/au-ts/microkit_sdf_gen/tree/joshua/mglru3) by running the following:
```sh
python3 -m venv venv
./venv/bin/pip install .
```
2. Compile [Microkit](https://github.com/au-ts/microkit/tree/joshua/ramfs) for maaxboard and qemu_virt_aarch64 by running the following:
```sh
    python build_sdk.py --sel4=/path/to/seL4 --boards=qemu_virt_aarch64,maaxboard --configs=debug,benchmark --skip-docs --skip-tar --ramfs=/path/to/cpio
```
3. Download [LionsOs](https://github.com/au-ts/lionsos/tree/joshua/ramfs) submodule by running the following in the [LionsOs](https://github.com/au-ts/lionsos/tree/joshua/simple-pager) directory:
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
## 2. Implementation details
### 2.1 Initialisation
LionsOS RAMFS initialises itself from a CPIO file. 
### 2.2 Syscalls & libC
RAMFS depends on LionsOS' libC implementation. I have edited the syscall functions to redirect to RAMFS and interfaces through
### 2.X Limitations
- RAMFS is allocated a fixed-size memory region. If too much is written to the RAMFS, then RAMFS will crash.

## Current problems:
- fgetc() only gets one character - bit stupid to have to play around with buffers or do mappings and such.

## TODO (me):
- support more than 1 PD.

## TODO (engineer):
For engineers if this becomes an actual part of LionsOS.
- Convert this PD into an actual component in `lionsos/components/fs/ramfs`.
