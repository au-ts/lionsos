<!--
    Copyright 2026, LionsOS Contributors

    SPDX-License-Identifier: BSD-2-Clause
-->

# LionsOS — Codebase Analysis

Deep-dive reference, generated 2026-10-01 against `5945f93`. Companion to `AGENTS.md`,
which holds the operational summary. This file holds the detail: per-component
breakdown, exact line references, concurrency models, and the full known-bugs list.

**Provenance:** derived by reading the tree and dispatching three parallel exploration
agents over `components/` + `include/lions/`, `lib/`, and `examples/`. Line references
were captured at that commit and will drift. `dep/` was deliberately not read.

---

## 1. What this is

LionsOS is UNSW's research operating system, built on the seL4 microkernel using the
seL4 Microkit userspace model. Target domain: embedded, IoT and cyberphysical systems.
Three stated design goals, all held to simultaneously:

- **Formally verifiable** — verification story is under active research.
- **Adaptable** to a wide class of use cases in the target domain.
- **Microkernel-class performance** — explicitly a benchmark-setter.

Modularity, performance, and the "time-honoured KISS principle" are held as joint
constraints. Website: https://lionsos.org

## 2. Size

Excluding `dep/`:

| Language | Lines |
|---|---|
| C (`.c`) | 23,608 |
| C headers (`.h`) | 8,374 |
| Python | 8,276 |
| DeviceTree (`.dts`) | 3,409 |
| Makefiles | 2,151 |
| `.system` SDFs | 1,121 |
| Markdown | 877 |
| Linker scripts | 76 |
| Assembly | 28 |

Files per top-level directory: `examples/` 177, `components/` 56, `lib/` 47,
`include/` 24, `ci/` 10.

`dep/` holds 8 git submodules plus one vendored tree:

| Path | What | Pinned at |
|---|---|---|
| `dep/sddf` | sDDF — the device driver framework, shared-memory RPC per device class | `0.6.0-519-g70d665dd` |
| `dep/micropython` | MicroPython upstream | `v1.26.1` |
| `dep/musllibc` | musl-libc, LionsOS `sel4` fork | `ee77eece` |
| `dep/libnfs` | libnfs client | `libnfs-1.9.6-798-gc45e08d` |
| `dep/libvmm` | hypervisor library | `0.2.0-33-gf91042d5` |
| `dep/libmicrokitco` | coroutine library (`libco/`, `libhostedqueue/`) | `4bf88ee` |
| `dep/wasm-micro-runtime` | WAMR, LionsOS fork | `66c1107` |
| `dep/microdot` | Microdot HTTP server | `v2.0.5-11-gdac6df7` |
| `dep/ff15` | ChaN's FATFs, **not** a submodule | vendored |

## 3. Build system

### Pinned toolchain

Microkit SDK **2.3.1**, WASI SDK **27**, sdfgen **0.35.0**, clang/lld (LLVM 18),
CMake, QEMU, dtc, dosfstools, gptfdisk, mtools, and gcc (solely to build `mpy-cross`).
`flake.nix` exposes this as a single `nix develop` devshell; `mkShellNoCC` is used
deliberately so nixpkgs' `cc` does not leak into a freestanding environment.
`hardeningDisable = [ "all" ]`.

sdfgen is version-asserted at build time: `assert version('sdfgen').split(".")[1] == "35"`
(`examples/webserver/meta.py:12`).

### Two-stage make

There is **no top-level Makefile**. Each example is self-contained:

1. `examples/<name>/Makefile` — a thin wrapper. It requires `MICROKIT_SDK`, normalises
   paths, and **generates** `build/Makefile` by `echo`-ing variables followed by
   `cat <name>.mk`. It also defines the `qemu` target, which re-invokes `make` in
   `build/`. This generation step is why `BUILD_DIR` is overridable and why CI can
   point every example at one scratch tree.
2. `build/Makefile` — the real build, assembled from included `.mk` snippets:
   - `meta.py --dtb … --sdf <name>.system` runs **sdfgen** to emit the system description.
   - ~20 `objcopy --update-section .xxx_config=xxx.data <elf>` calls patch config blobs
     into the ELFs (`examples/webserver/webserver.mk:85-105`).
   - `$(MICROKIT_TOOL) $(SYSTEM_FILE) --board … -o img -r report.txt`.

`examples/vmm` and `examples/dynamic_caps` are the two exceptions: hand-written
`.system` files. `vmm` runs its SDF through **`cpp -I. -P`** with the RAM layout
generated into a C header (`examples/vmm/vmm-dev.mk:131`). `dynamic_caps` needs a
**patched Microkit SDK** (see §9.1).

### Rebuild invalidation

Flag changes are checksummed into stamp filenames so that changing a build flag
forces a rebuild: `.fat_cflags-$(shell … | shasum)` at `components/fs/fat/fat.mk:38-42`,
and the same pattern at `components/fs/nfs/nfs.mk:28-32` and `components/wamr/wamr.mk:27-31`.

### Boards

`qemu_virt_aarch64` (default target), `odroidc4`, `maaxboard`, `imx8mp_iotgate`.
AArch64 only, clang, freestanding (`-nostdlib`).

### CI

GitHub Actions, **build-only** — no runtime tests (`ci/README.md:14`).

- `.github/workflows/examples.yaml` — three jobs: `ubuntu-24.04` via apt, `ubuntu-24.04`
  via Nix, and self-hosted macOS ARM64 via Nix.
- `.github/workflows/push.yaml` — REUSE license check (`seL4/ci-actions/license-check`).
- `.github/workflows/pr.yaml` — trailing-whitespace check (`seL4/ci-actions/git-diff-check`).
- `ci/examples.sh` builds, in order: `kitty`, `webserver`, `fileio`, `firewall`,
  `posix_test`, `wasm_test`, `desktop`. **`vmm` is commented out** (line 28) and
  `dynamic_caps` is absent entirely. Each `ci/<name>.sh` sets `BUILD_DIR` to a scratch
  path, `rm -rf`s it, exports the board/config vars, and runs `make`.

## 4. Architectural invariants

### 4.1 Shared-memory rings, channels as wakeup hints only

**There is no Microkit RPC anywhere in LionsOS.** Every cross-protection-domain message
is a fixed-size struct pushed into a shared-memory ring. Microkit channels carry no
payload; they are edge-triggered wakeup hints.

Three protocol families, all in `include/lions/`:

**Filesystem** — `include/lions/fs/protocol.h` (376 lines)
- `FS_QUEUE_CAPACITY 511`, `FS_MAX_NAME_LENGTH 255`, `FS_MAX_PATH_LENGTH 4095` (`:11-14`)
- 19 status codes `FS_STATUS_SUCCESS`…`FS_STATUS_NOT_EMPTY` (`:26-85`)
- 20 commands `FS_CMD_INITIALISE`…`FS_CMD_DIR_REWIND` (`:88-112`) — **the ordinal order
  is a wire contract**; the dispatch tables in both backends depend on it.
- `fs_stat_t` (`:114-132`) — 17× `uint64_t`, a widened mirror of POSIX `struct stat`
- **`fs_buffer_t { uint64_t offset; uint64_t size; }`** (`:134-137`) — a buffer is an
  offset into the *client-owned share region*, never a pointer. This is the elegant bit:
  it keeps messages fixed-size and address-space-independent.
- `_Static_assert(sizeof(fs_cmd_t) == 64)` (`:249`) and the same for `fs_msg_t` (`:299`)
- `fs_msg_t` is a **union** of `cmd` and `cmpl`, so one 64-byte slot serves both directions
- `fs_queue_t { head, tail, padding[48], buffer[511] }` (`:301-307`) — the 48-byte padding
  exists explicitly so entries are **cache-line aligned**. Monotonic 64-bit indices, so
  `length = tail - head` works mod 2⁶⁴. ACQUIRE loads / RELEASE stores (`:309-333`).
- `fs_config.h` carries `LIONS_FS_MAGIC = "LionsOS\x01"` and `fs_config_check_magic()` (`:31-41`)

**GUI** — `include/lions/gui/protocol.h` (126 lines). Compositor ↔ application. Per app slot:
three shared regions (surface / state / events) plus one channel. The threat model is
stated up front at `:27-28`: the compositor treats everything in the app's regions as
untrusted; a misbehaving app can only affect its own window's pixels.
- `gui_state_t { magic, width, height, title[32], seq, damage }` (`:46-61`) — seqlock
  style: the compositor reads `seq` before and after copying, and redraws fully if it
  changed or if commits were skipped
- `gui_event_enqueue` (`:103-114`) indexes **modulo capacity** on purpose, so a hostile
  app cannot make the compositor read out of its page
- `gui_event_t` is a **superset of `input_event_t`**, so the compositor just widens input
  events with a position

**Input** — `include/lions/input/input.h` (139 lines). Topology
`driver(s) → virtualiser → client(s)`, each hop a SPSC ring plus one channel. Linux
evdev semantics (`INPUT_EV_SYN/KEY/REL/ABS`, `INPUT_KEY_PRESSED/RELEASED/REPEATED`).
`input_event_t { u16 type; u16 code; i32 value; }` (`:70-74`) is **byte-identical to
`struct virtio_input_event`** — that is why virtIO drivers need no marshalling.
`INPUT_ABS_MAX 0xffff` is the contract that the driver normalises absolutes.
`input_enqueue` (`:115-126`) **drops and increments a `dropped` counter on overflow**
rather than blocking, and the counter is never reported back to the producer.

The trust-boundary contrast between `input_queue_t` and `gui_event_queue_t` is
deliberate and documented: for input both indices are trusted (driver and virtualiser
are separate PDs); for GUI the app owns the page, so indices are not trusted.

### 4.2 Build-time config injection

Every PD declares its configuration in an ELF section and validates a magic at `init()`:

```c
__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;
```

Sections in use: `.fs_client_config`, `.fs_server_config`, `.timer_client_config`,
`.serial_client_config`, `.net_driver_config`, `.net_virt_rx_config`, `.net_virt_tx_config`,
`.net_copy_config`, `.net_client_config`, `.timer_driver_config`, `.device_resources`,
`.lib_sddf_lwip_config`, `.input_driver_config`, `.vmm_config`, `.fw_*_config`,
`.gpu_*_config`.

`meta.py` calls `serialise_config(output_dir)` on each sdfgen subsystem, which writes
`<name>.data` files, and the build then `objcopy --update-section`s them in.

**This is what makes a component optional at runtime without recompiling** — if the SDF
did not wire the component up, the magic check fails and the component degrades. Follow
this pattern when adding configuration.

Symbols use the `*_vaddr` suffix convention, from `dep/sddf/resources/common.h`.

### 4.3 The musl shim

`lib/libc/` is a **shim over musl-libc**, not a from-scratch libc. It writes the function
pointer `sel4_vsyscall` into musl's internal `__sysinfo` (`lib/libc/posix/posix.c:203`),
which is musl's own hook for redirecting `syscall()`. Every musl wrapper then funnels
through one static table.

```
musl wrapper (open/read/write/socket)
  → sel4_vsyscall                        posix.c:162-189
    → syscall_table[n]                   posix.c:45
      → file.c / io.c / sock.c handler
        → fd_entry_t vtable              include/lions/posix/fd.h:31
          ├─ file_read/write → fs_command_blocking()   helpers.c:118
          │                     ↳ blocking_wait(server_ch)  ← cothread yield point
          └─ sock_*          → libc_socket_config_t vtable → lib/sock/tcp.c → lwIP
```

- 38 `libc_define_syscall` registrations; 5 of the 12 socket ones are **conditional** on
  the vtable being populated (`sock.c:570-584`), so the unconditional floor is 33.
- `libc_init()` (`posix.c:202-229`) sets `__sysinfo`, optionally initialises the heap,
  always initialises io and file, conditionally sock, then calls musl's `__init_libc` +
  `__libc_start_init` — "everything `__libc_start_main` does apart from `exit(main())`",
  which is correct for a PD with no `main`.

**The load-bearing injection point** is `fs_set_blocking_wait(void(*)(microkit_channel))`
(`include/lions/fs/helpers.h:26`). The client supplies its own yieldable blocking
primitive; the same `helpers.o` then serves blocking POSIX callers *and* async
MicroPython/WAMR callers. `libc_init_file` uses it for every FS call
(`lib/libc/posix/file.c`); `modfs_raw.c:15-26` instead passes `mp_fs_request_flag_set`,
which sets a MicroPython `Flag` object — the "await a future" primitive.

## 5. `lib/` — the shared libraries

| Area | Files | Lines |
|---|---|---|
| `lib/fs/helpers/` | 2 | 148 |
| `lib/fs/server/` | 3 | 283 |
| `lib/libc/posix/` | 6 | 2,130 |
| `lib/libc/compiler_rt/` | 16 `.c` + 20 headers | 3,261 |
| `lib/sock/tcp.c` | 1 | 646 |

### 5.1 `lib/fs/server/` — the generation-tagged open-file table

`lib_fs_server.mk` builds `lib_fs_server.a` from exactly two objects, `fd.o` and
`memory.o`, and is included by both backends (`components/fs/fat/fat.mk:45`,
`components/fs/nfs/nfs.mk:45`). It requires only `CC/AR/RANLIB` plus
`LIB_FS_SERVER_LIBC_INCLUDE`, so it compiles against either libc. This is the single
abstraction point that lets one server library serve both FAT and NFS.

`fd.c` state machine over `MAX_OPEN_FILES` 256 slots (`:12-27`):
`free → allocated → open_file | open_dir → busy_file | busy_dir`.

- `handle` is an **opaque `void *`** — a `FIL*` for FatFs (`fat/op.c:26`) or a
  `struct nfsfh*` / `struct nfsdir*` for NFS (`nfs/op.c:282, 883`).
- **Busy counting** is what makes async NFS safe: `of_begin_op_file` (`:90-105`) moves
  `open_file → busy_file` and sets `busy_count = 1`; a second concurrent op just
  increments. `of_end_op` (`:124-142`) decrements and only reverts at zero. The fd stays
  "busy" for the whole RPC round trip.
- **Generation tags prevent ABA.** `of_to_fd` (`:157-159`) packs
  `index + generation * MAX_OPEN_FILES`; `of_free` bumps `generation++` (`:46`);
  `fd_to_of` (`:144-155`) rejects an fd whose generation is *less* than the slot's.
  Note it tests `<` not `!=`, so a forged future generation would pass — but fds only
  come from the client's own prior replies.
- `fd_unset` **fails while busy** — that is the sole source of
  `FS_STATUS_OUTSTANDING_OPERATIONS`.

**The close protocol** (used identically by both backends, e.g. `fat/op.c:282-312`,
`nfs/op.c:400-440`): `fd_begin_op` → `fd_end_op` → `fd_unset` → real close → `fd_free`
on success or `fd_set_file` to roll back on failure. This two-phase handover is what
makes `close` safe against in-flight NFS RPCs. Do not simplify it.

`memory.c` (29 lines) is the trust boundary. Both accessors are overflow-safe by
construction — `size > limit - offset`, not `offset + size > limit`:
- `fs_get_client_buffer(share, share_size, buf)` (`:10-18`)
- `fs_copy_client_path(dest, share, share_size, buf)` (`:20-29`) — also rejects
  `size > FS_MAX_PATH_LENGTH` and NUL-terminates

**Every untrusted client offset in both backends funnels through these two functions.**
The share size is supplied per backend as 64 MiB: `FAT_FS_DATA_REGION_SIZE`
(`components/fs/fat/config/fat_config.h:11`) and a private `CLIENT_SHARE_SIZE`
(`components/fs/nfs/op.c:32`). Both hard-code it rather than reading the mapped region.

### 5.2 `lib/fs/helpers/` — the client side

Depends on four **extern globals the client component must define**
(`helpers.c:13-16`): `fs_config`, `fs_command_queue`, `fs_completion_queue`, `fs_share`.
It is not self-initialising — that is exactly what lets the same `helpers.o` be linked
into `libc.a` (`libc.mk:30,33`) *and* into MicroPython and WAMR separately.

Three sub-APIs:
- **Request IDs** — `fs_request_allocate`/`_free` over `request_metadata[511]`, each
  entry holding the full `fs_cmd_t` *and* `fs_cmpl_t` so a late completion can be
  correlated. `REQUEST_ID_MAXIMUM = 510` (`:20`).
- **Share-region buffer pool** — `fs_buffer_allocate` (`:51-60`) hands out
  `i * FS_BUFFER_SIZE` where `FS_BUFFER_SIZE = 0x8000` (32 KiB), so a 64 MiB share
  yields 2048 slots; the code reserves 2044. `fs_buffer_free` (`:62-68`) **zeroes the
  whole 32 KiB** on release — a hygiene measure so paths and contents do not leak into
  a later, possibly less-privileged, command.
- **Blocking vs async** — `fs_command_issue` (`:94-104`) asserts the queue is not full,
  copies in, RELEASE-publishes, notifies. `fs_process_completions(cb)` (`:75-92`)
  drains, validates the id, matches, sets `complete = true`, and invokes the optional
  callback. `fs_command_blocking` (`:118-135`) allocates an id, issues, spin-yields on
  `blocking_wait` until complete, then frees. There is an explicit TODO at `:72-74`
  wanting to replace the function-pointer callback with a user-driven completion API.

`libc.mk` cross-compiles musl (`:57-59`, `--with-malloc=oldmalloc`, static only) and
then `ar rcs` **merges in** the POSIX objects, compiler_rt objects and `fs/helpers.o`
(`:38-41`) — which is why `posix/file.c` can call `fs_command_blocking` with no separate
library. A workaround at `:52-54` appends `-MF musl.d` for the musl configure step,
because with `-MD` present musl's compiler probe tries to create `/dev/null.d`.

### 5.3 `lib/libc/posix/` — the syscall surface

| File | Registrations | Syscalls |
|---|---|---|
| `posix.c` | 7 | `clock_gettime`, `nanosleep`, `getpid`, `getuid`, `getgid`, `getrandom`, `set_tid_address` |
| `io.c` | 9 | `write`, `read`, `writev`, `readv`, `close`, `ioctl`, `dup3`, `fstat`, `fcntl` |
| `file.c` | 6 | `newfstatat`, `readlinkat`, `openat`, `lseek`, `mkdirat`, `unlinkat` |
| `sock.c` | 12 (7 unconditional) | `socket`, `bind`, `connect`, `setsockopt`, `getsockopt`, `sendto`, `recvfrom`, + conditional `listen`, `accept`, `getsockname`, `getpeername`, `ppoll` |
| `mem.c` | 4 | `brk`, `mmap`, `munmap`, `mprotect` |

Notable behaviours:

- **`posix.c`** — `CLOCK_REALTIME` is deliberately **aliased to `CLOCK_MONOTONIC`**
  because there is no RTC; the clock is read-only. `getpid` and `set_tid_address` both
  return 1 — the latter because pthreads treats 0 as an invalid TID. `getuid`/`getgid`
  return 501. `sys_getrandom` (`:134-160`) is **explicitly labelled "deliberately
  insecure for now"** — it fills from `rand()`. `nanosleep` (`:74-105`) spin-yields on
  the timer channel and always zeroes `rem` — no signal interruption support.
- **`fd.c`** — `MAX_FDS` 128, overridable with `-DMAX_FDS=` (`posix_test.mk:57` sets 8).
  `console_write` (`:18-43`) batches to the next `\n`, breaks when the queue is nearly
  full, **inserts `\r\n`**, and notifies only if bytes were actually sent. fds 0/1/2 are
  statically pre-populated; **stdin has no `read` function**, so reads on it are not
  wired up. `SERVICES_FD` and `ETC_FD` are synthetic fds placed **above** `MAX_FDS`
  (`fd.h:28-29`) so they cannot collide, modelling a read-only virtual `/etc/services`
  and `/etc`.
- **`io.c`** — careful POSIX conformance in `writev`/`readv` (IOV_MAX check, `SSIZE_MAX`
  accumulation, stop on first short transfer). All ioctls on stdout silently succeed
  (`:242-245`, "muslc does some ioctls to stdout"). `fcntl` implements only
  `F_GETFD`/`F_SETFD`/`F_GETFL`/`F_SETFL`.
- **`file.c`** — a hand-rolled path resolver `resolve_path` (`:35-89`); there is no VFS.
  `fs_status_to_errno[]` (`:91-111`) maps all 19 status codes, lossily:
  `END_OF_DIRECTORY` and `ERROR` both become `FILE_ERR`. **The FS protocol is fully
  positional** — `file_write`/`file_read` track the offset in the client's
  `fd_entry->file_ptr` and issue `FILE_WRITE`/`FILE_READ` at that offset. Several POSIX
  semantics are emulated client-side: `O_CREAT|O_EXCL` (stat first), `EISDIR` (stat
  first), `O_TRUNC` (follow-up `FILE_TRUNCATE` with length 0). `fstat` is implemented as
  **stat-by-cached-path** (`:334-337`), not via `FILE_SIZE`. `SEEK_END` is the only
  `lseek` case needing a round trip. `mode` and `umask` are **entirely ignored**
  (`:583-584`). `readlinkat` is a stub returning `-EINVAL`. `file_dup3` (`:263-267`)
  aliases the server fd with a `TODO: refcount of underlying file?` — a real hazard:
  two POSIX fds share one server fd with no refcounting.
- **`sock.c`** — dependency-injected vtable; it never links lwIP. Only `AF_INET` +
  `SOCK_STREAM` are supported — no UDP, no IPv6, no Unix sockets. `setsockopt` accepts
  only `SO_LINGER` and **silently ignores it** (`:87-94`). `ppoll`'s timeout and sigmask
  are **ignored** (`:497-499`) — it is a non-blocking poll only. `POLLHUP`/`POLLERR` are
  always checked for sockets, matching POSIX, even if not requested.
- **`mem.c`** — `sys_munmap` and `sys_mprotect` are **no-ops returning 0**. `sys_mmap`
  supports only `MAP_ANONYMOUS` and carves from the top of the heap. `sys_brk` returns
  the current break on failure, deliberately Linux-like, because musl expects that.

### 5.4 `lib/libc/compiler_rt/` — 128-bit soft-float only

A vendored **subset** of LLVM compiler-rt (`llvmorg-19.1.7`, Apache-2.0 WITH LLVM-exception),
16 `.c` files totalling 370 lines of thin wrappers over ~2,900 lines of templated
`*.inc` algorithms. It exists for one reason: an AArch64 **hard-float** toolchain emits
calls to `__addtf3`/`__multf3`/etc. for `long double` (IEEE-754 binary128) but provides
no library for them. Arm's *128-bit floating-point support for AArch64* (KA004751)
documents the gap.

- Arithmetic: `addtf3`, `subtf3` (sign flip + add), `multf3`, `divtf3` (**Newton–Raphson**,
  4 half + 1 full iteration), `comparetf2` (NaN semantics documented at `:16-46`:
  `le` returns 1 for NaN, `ge` returns -1, `unord` returns 1).
- Conversions: `floatsitf`, `floatunsitf`, `fixtfsi`, `fixunstfsi`, plus the templated
  `extendsftf2`/`extenddftf2`/`trunctfsf2`/`trunctfdf2`, and two hand-written half-precision
  ones (`extendhfsf2`, `truncsfhf2`) because the EABI needs `__aeabi_h2f`/`__aeabi_f2h` aliases.
- `fp_mode.c` — default fallback: rounding is **always** `TONEAREST`, `__fe_raise_inexact`
  always 0. **No dynamic rounding mode.**

**Not** a general `libgcc` replacement, and not integer builtins. It is entirely float.

### 5.5 `lib/sock/tcp.c` — TCP over lwIP

646 lines, the default provider of `libc_socket_config_t`. It is **not in any archive** —
it is compiled straight into each component that wants TCP via a one-line rule
(`components/fs/nfs/nfs.mk:47-48`, `examples/posix_test/posix_test.mk:96-97`) and then
handed to `libc_init()` as `extern libc_socket_config_t socket_config;`.

Fixed, non-dynamic sizing: `MAX_SOCKETS 10`, `MAX_LISTEN_BACKLOG 10`,
`SOCKET_BUF_SIZE 0x200000` (2 MiB) — **10 × 2 MiB = 20 MiB of statically-allocated
`.bss`**. Nine-state machine (`:47-57`): `unallocated, allocated, bound, connecting,
connected, closing, closed_by_peer, error, listening`. `socket_refcount[]` (`:45`) gives
`dup`/`close` reference semantics; only the last close tears down.

- **Recv path** — pure byte ring (`rx_head`, `rx_len`). Producer
  `socket_recv_callback` (`:133-190`) returns `-ERR_MEM` via `lwip_err_to_errno` if the
  pbuf does not fit, and — notably — **does not call `tcp_recved()`** on that path, so
  the window stalls rather than recycling pbufs. Consumer `tcp_socket_recv` (`:417-446`)
  mirrors it and **does** call `tcp_recved()` (`:444`). A NULL pbuf means FIN
  (`:160-163`).
- **Blocking** — all waits are `microkit_cothread_semaphore_wait`, never spinning.
  Per-socket `connect_sem`, `recv_sem`, `send_sem`, plus `accept_queue.accept_sem`.
  The universal guard is `if (!microkit_cothread_semaphore_is_queue_empty(&sem)) signal(&sem)`
  — signal only if a waiter is parked, so no accumulating credits. `connect`
  (`:274-314`) returns `-EINPROGRESS` under `O_NONBLOCK`, otherwise waits with
  **no timeout**. `write` (`:376-415`) returns `MIN(len, tcp_sndbuf())` — partial writes
  are normal.
- **No timeout support at all.** No `SO_RCVTIMEO`/`SO_SNDTIMEO`, no deadline in any wait
  loop, despite `<sddf/timer/*>` being included. `sys_setsockopt` exists in
  `sock.c:75` but the TCP vtable has no timeout hook.
- **No `tcp_poll` callback is ever installed**, even though `SO_KEEPALIVE` is set on the
  pcb (`:213`), and `sys_check_timeouts` is never called outside `dep/`. Keepalive and
  retransmission timers are effectively driven externally by the sDDF lwIP component.
- `accept` (`:519-565`) retrofits a new pcb with `tcp_err`, `tcp_arg`, `tcp_sent`,
  `tcp_recv` and initialises **only** `recv_sem`/`send_sem` (`:559-560`).

## 6. `components/` — the reusable pieces

### 6.1 `components/micropython/` — the MicroPython port

An upstream MicroPython plus LionsOS glue. Notable files:

- `micropython.c` / `.h` — the PD entry point. Config magic check at `:39-45`;
  framebuffer address at `:89`; cothread setup at `:238-249`; `fs_set_blocking_wait`
  installed at `:225`; fs globals wired at `:226-229`; `fs_config_check_magic` at `:217`.
- `mpconfigport.h` — the port configuration. Cothread/interrupt model at `:105-112`.
- `vfs_fs.c` / `vfs_fs_file.c` / `vfs_fs.h` — the VFS layer bridging MicroPython's
  `mp_vfs_t` onto the FS protocol.
- `modfs_raw.c` — the async MicroPython FS bindings. `mp_fs_request_flag_set` (`:15-26`)
  is the async completion primitive.
- `modfb.c` — the framebuffer module. `extern void *framebuffer_data_region` at `:14`
  is a **type mismatch** against `micropython.c:89`'s `uintptr_t`.
- `modfirewall.c` — the firewall bindings, including the PPC-style register ABI
  (`:348-360` for filters, `:211-257` for routing) that matches the enums in
  `include/lions/firewall/filter.h` and `routing.h`.
- `mpfirewallport.c`, `mpfirewallport.h` — the PD-side firewall state.
- `modmachine.c/.h`, `machine_i2c.c` — `machine` module, I²C. `machine_i2c.c:195` has
  `assert(returned_addr == (i2c_addr_t)addr)`, which will abort the whole PD on a
  protocol violation.
- `modinterrupt.c`, `modtime.c` + `modtime_impl.h`, `mphalport.c/.h` — four
  interrupt-emulation modes, the time module, the HAL port.
- `micropython.mk` — the build. Note `:56` `MICROPY_MPYCROSS_DEPENDENCY=$(abspath
  mpy_cross/mpy-cross)` points at the build output rather than
  `$(MICROPYTHON_SRC)/mpy-cross/mpy-cross` (compare `:55`); almost certainly wrong.

### 6.2 `components/wamr/` — WebAssembly Micro Runtime

`wamr.c` (~1016-line sibling in the desktop's `wasm_host.c`; the component itself is
smaller). Two libco cothreads — the event loop and `wamr_main` — plus a 1 ms timer
(`wamr.c:210-219`, `:168-194`). Config magic at `:42-46`. Built via CMake with
`-DWAMR_BUILD_INTERP=1 -DWAMR_BUILD_LIBC_BUILTIN=1 -DWAMR_BUILD_LIBC_WASI=0
-DWAMR_BUILD_ALLOC_WITH_USAGE=1 -DWAMR_BUILD_DUMP_CALL_STACK=1
-DWAMR_BUILD_CUSTOM_NAME_SECTION=1` (`examples/desktop/desktop.mk:143-153`).

### 6.3 `components/fs/fat/` — FatFs over sDDF block device (~1,238 lines)

`event.c` (284) + `op.c` (743) + `io.c` (211).

**Concurrency: a libmicrokitco coroutine pool.** `init()` (`event.c:122-158`) partitions
the block data region statically, one `0x40000` slice per worker
(`max_cluster_size = blk_config.data.size / FAT_WORKER_THREAD_NUM`, `:128`), spin-waits
on `blk_storage_is_ready()` because FatFs is not reentrant (`:140`), then
`microkit_cothread_init` with **four statically-declared stacks** `worker_thread_stack_one..four`
(`:35-38`) — they must be named symbols because the stack region is declared in the SDF.
`LIBMICROKITCO_MAX_COTHREADS = 5` (four workers + the event loop).

**Dispatch:** `operation_functions[]` at `event.c:65-86` is a designated-initializer
table indexed on the `FS_CMD_*` ordinal, with a comment stating the table must stay
consistent with the protocol enum order. `setup_request()` (`:97-104`) spawns a
coroutine passing a pointer to its own `co_data_t` slot, so every handler begins with
`co_data_t *args = microkit_cothread_my_arg();` and writes `args->status` — a clean
alternative to a global "current request".

**Event loop** (`notified()`, `:167-284`) is a three-stage `while (new_request_popped)`:
drain block responses and signal the right semaphore (`:186-205`) → `microkit_cothread_yield()`
(`:208`) → reap finished coroutines and publish completions (`:218-226`) → admit new
commands (`:233-268`). Two details worth knowing: the client message is **copied to a
local** before use (`:249`, "to avoid modification from client side" — essential because
the client may reuse the slot as soon as it advances `head`), and out-of-range
`cmd.type >= FS_NUM_COMMANDS` is **silently dropped** (`:255-258`).

There is a documented blocking invariant at `:160-166`: block-wait on new messages
**iff** all workers are free or blocked in `diskio`.

**`io.c` — the FatFs ⇄ sDDF bridge**, implementing FatFs' `diskio.h` as coroutines:
- `disk_read` (`:112-150`) does head/tail fixup for unaligned ranges
- `disk_write` (`:152-211`) does **read-modify-write** for unaligned writes
  (`:193-202`), bailing early if the read failed
- `disk_ioctl(CTRL_SYNC)` (`:96-104`) maps `f_sync` to `BLK_REQ_FLUSH`
- `DRESULT` is recovered from the coroutine argument register

`op.c` notes: one volume only (`:95`, `:358`); `handle_stat` hard-codes mode `0777` because
FAT has no user/group/world concept (`:361-364`); `handle_dir_seek` (`:715-743`) is an O(n)
rewind-and-skip loop because FatFs has no `seekdir` and the vendored library was not patched.
`FIL files[256]` and `DIR dirs[256]` are **separate** pools, but the shared `oftable` caps
the *combined* count at 256; a `_Static_assert` at `event.c:119-120` enforces
`FF_FS_LOCK >= FAT_MAX_OPENED_FILENUM + FAT_MAX_OPENED_DIRNUM`.

### 6.4 `components/fs/nfs/` — libnfs over lwIP (~1,132 lines)

`nfs.c` (139) + `op.c` (993). **No coroutines at all.**

`init()` (`nfs.c:113-139`) sets up serial, `libc_init(&socket_config, libc_heap, …)`,
the continuation pool, the net queues, `sddf_lwip_init`, and arms a 1 ms timer
(`TIMEOUT (1 * NS_IN_MS)`, `:33`). `network_ready` comes from the DHCP
`netif_status_callback` (`:59-64`).

`notified()` (`:66-111`) is a **poll-style dispatch** on every 1 ms tick: run
`sddf_lwip_process_rx()` + `sddf_lwip_process_timeout()`, then bridge libnfs's own
`poll()` API onto LionsOS's `libc_socket_config_t` —
`nfs_which_events()` plus `tcp_socket_{hup,err,readable,writable}` → `nfs_service()`.
Command reprocessing is **unconditional on every notification** (`:104-106`), because
leftover commands cannot rely on another client notify arriving.

**Async continuations replace coroutines.** `struct continuation { request_id, data[4],
next_free }` (`:43-47`) with a 511-entry intrusive free list (`:119-141`). Every handler
has the same shape: validate buffers through the memory.c accessors → `continuation_alloc()`
and stash the fd/buffer/handle in `data[]` → `nfs_XXX_async(…, callback, cont)` → on
non-zero return, a `goto`-labelled error ladder guarantees exactly one reply. `reply()`
(`:96-101`) asserts the completion queue is not full.

**Backpressure:** `process_commands()` (`:103-117`) computes
`to_consume = MIN(command_count, FS_QUEUE_CAPACITY - completion_space)` — it **never
dequeues a command it has no completion space for**. It may legitimately drain many
commands into in-flight state, because replies arrive later from callbacks.

The directory-iteration ops (`nfs_closedir`, `nfs_readdir`, `nfs_seekdir`, `nfs_telldir`,
`nfs_rewinddir`) are **synchronous and reply inline** (`:878-993`). `handle_dir_read`
requires `params.buf.size >= FS_MAX_NAME_LENGTH` (255 bytes) at `:909`, stricter than the
protocol needs.

### 6.5 `components/input/` — the input device class

`input.mk` builds two ELFs: `input_driver.elf` (**one image, copied once per device**, each
copy's `.input_driver_config` section patched with that device's register offset and queue
capacity — written by the meta program, e.g. `examples/desktop/desktop.mk:232-233`) and
`input_virt.elf` (which includes the project's `input_config.h`). Sources: `virt.c`
(the virtualiser) and `virtio/input.c` (the driver, config magic at `:57`).

`INPUT_DRIVER_CONFIG_MAGIC 0x4c494e50 /* "LINP" */` (`include/lions/input/config.h:18`).

Note: this is a *newer, third* convention. The FS headers use a byte-loop magic compare
(`fs/config.h:31`); MicroPython's `net_config_check_magic` returns a *bool capability*
rather than asserting. Three conventions for the same idea.

### 6.6 The firewall headers

`include/lions/firewall/` is header-only — `filter.h` (586), `routing.h` (234),
`arp.h` (219), plus `ip.h`, `tcp.h`, `udp.h`, `icmp.h`, `ethernet.h`, `queue.h`,
`checksum.h`, `config.h`, `common.h`, `array_functions.h`. Only `filter.h`, `arp.h`,
`queue.h`, `checksum.h` and `common.h` carry their logic inline; the `fw_routing_*` and
`pkt_waiting_*` functions are declared in `routing.h` and **defined in the router PD**.

- `fw_action_t` (`:42-53`) — `ALLOW 1, DROP 2, REJECT 3, CONNECT 4, ESTABLISHED 5`.
  The last two implement stateful return-traffic permission.
- `rules_reserve_id`/`rules_free_id` (`:173-225`) — a circular 64-bit-block bitmap allocator.
- `fw_filter_add_rule` (`:244-312`) does an O(n) clash scan: two rules clash if port
  anyness, ports, subnets and masked IPs are *all* equal — same action means `DUPLICATE`,
  different means `CLASH`.
- `fw_filter_find_action` (`:431-498`) — the matching algorithm. **External instances are
  checked first** (scanning all neighbour interfaces for the swapped src/dst tuple,
  returning `ESTABLISHED`), then a most-specific-match sweep over rules 1..n prioritised
  src subnet > dst subnet > src port > dst port, falling back to the default action.
- The **PPC control-plane ABI** — `fw_filter_pp_type_t {SET_DEFAULT_ACTION, ADD_RULE,
  DEL_RULE}` plus arg-index enums — is exactly the register set `modfirewall.c:348-360`
  fills. `routing.h:40-58` mirrors this for routes and ping.
- `routing.h`'s distinctive structure is `pkt_waiting_node_t` (`:82-98`): a tree of packets
  queued behind unresolved next hops, root keyed by destination IP, children being further
  packets for the same IP. `fw_routing_find_route` (`:198`) recurses up to
  `FW_ROUTING_MAX_RECURSION` 3 times to chase next hops.
- `icmp.h`'s `icmp_req_t` (`:167-186`) captures the source packet's headers into the
  request so the ICMP module can reply without the original packet —
  `icmp_enqueue_error`, `icmp_enqueue_echo_reply`, `icmp_enqueue_redirect` (`:213-322`).

## 7. `examples/` — the nine systems

CI order is `kitty`, `webserver`, `fileio`, `firewall`, `posix_test`, `wasm_test`,
`desktop`. `vmm` is commented out; `dynamic_caps` is not in CI.

### 7.1 `desktop` — the flagship: a native GUI, each app its own PD

Graphics go straight through sDDF's GPU device class (no Linux driver VM, unlike `kitty`),
input comes from **native virtIO input drivers**, every application is **its own
protection domain**, and one app slot runs **WebAssembly programs loaded from a disk at
run time**.

```
notes/sketch/clock/calculator/widgets/wasm_host <-> compositor <-> gpu_virt -> gpu_driver -> virtio-gpu
compositor <-> input_virt <- input_keyboard, input_tablet <- virtio-keyboard/tablet
wasm_host <-> fatfs <-> blk_virt <-> blk_driver -> virtio-blk: /apps/*.wasm
```

`meta.py` (364 lines) hand-writes the GPU and input parts because sdfgen 0.35 has no GPU
or input class, then **post-processes the rendered XML** to inject what is missing:
`add_region_paddr_setvars()` (`:95-106`) adds `<setvar symbol=… region_paddr=…/>` for
`virtio_metadata_paddr`, `virtio_data_paddr`, `gpu_client_data_paddr` and each driver's
`virtio_dma_paddr`; `add_cspace()` (`:109-121`) injects a `<cspace>` into `wasm_host`
when `SANDBOX=1`.

Protection domains and their priorities: `timer_driver` 254, `gpu_driver` 254,
`input_keyboard`/`input_tablet` 253 (all three devices run the **same**
`input_driver.elf`), `blk_driver` 200, `input_virt` 200, `serial_driver` 100,
`serial_virt_tx` 99, `gpu_virt` 99, **`compositor` 10**, **six app PDs at 5**,
`nfs`/`fatfs` 96, `blk_virt` 199, and — with `--sandboxes N` — `sandbox0..N` at **1**,
below everything.

The virtIO MMIO detail is a good example of the board being awkward: on QEMU virt there
are 32 transports of size 0x200 from `0xa000000`, transport *n* using IRQ `48+n`, so the
GPU (bus 31), keyboard (bus 30) and tablet (bus 29) **all share the 4 KiB page at
`0xa003000`** and it must be mapped into all three drivers (`meta.py:64-75`).

Isolation model, from `examples/desktop/README.md:43-48`: the compositor maps each app's
surface and state **read-only**, validates everything an app publishes, and draws the
window decorations itself. An app cannot draw outside its window, read the screen, or see
another app's input. `src/compositor.c` (1,189 lines) is the whole compositor, shell and
damage tracker; `app_committed()` (`:488-569`) reads `seq` before and after copying the
state, validates magic/size/`w*h*4 <= GUI_SURFACE_REGION_SIZE`, sanitises the title to
printable ASCII, and falls back to a full-window redraw if the sequence jumped.

Per-app regions (from `meta.py:242-258`): surface 1 MiB, state 4 KiB, events 4 KiB;
mapped rw into the app at `0x20/0x21/0x22_000_000 + i`, and into the compositor as
**read-only** for surface and state, read-write for events, at `0x60/0x61/0x62_000_000 + i`.
`GUI_APP_CH_BASE = 10`, so app *i* owns channel `10+i`.

**`include/compat/`** exists because pinned sDDF GPU components were broken by upstream
commits: `sddf_gpu_compat.h` re-implements the removed `ialloc_init_with_offset()`;
`sddf/virtio/virtio.h` restores `virtio_mmio_check_magic/device_id/version` on top of
sDDF's new `transport/`+`feature.h`; `sddf/virtio/virtio_queue.h` includes
`<sddf/virtio/queue.h>`. `desktop.mk:56-69` puts `include/compat` **first** on the include
path so it can shadow sDDF's headers. Rationale at `README.md:466-491`, including the note
that the virtualiser bounds-checks `TRANSFER_TO_2D` as if contiguous, so **the compositor
always transfers full-width row bands** — hence row-based damage tracking
(`dirty_y0`/`dirty_y1`).

**Apps** (`apps/`): `notes.c` (128, text editor), `sketch.c` (182, paint, unions the
damage of everything touched in a batch then commits once at `:38-50`), `clock.c` (74, a
timer client), `calculator.c` (237, microui), `widgets.c` (81, microui gallery).
Shared: `gui_app.[ch]` (the `gui_app_init`/`gui_app_commit` shim), `gfx.[ch]` (a minimal
clipped software renderer for 32-bit BGRA, `0xAARRGGBB` = `GPU_FORMAT_B8G8R8A8_UNORM`),
`font_petme128_8x8.h` (the MicroPython 8×8 font), `keymap.c` (evdev → ASCII, designated-
initializer tables), and `mu_app.[ch]` (a microui 2.02 backend whose
`hash_commands()` FNV-1a over the command list means **a frame is only rendered and
committed if it differs**).

**`wasm_host/`** is the WebAssembly app host, and the most interesting corner of the repo.
Two modes:

*Unsandboxed* — WAMR instantiates the module in-process. `scan_apps()` (`:286-331`) lists
`/apps/*.wasm` and previews each app's requested grants. 15 native symbols are exported
(`:596-612`): `cap_lookup`, `cap_drop`, `win_fill_rect`, `win_draw_text`, `win_blit`,
`win_commit`, `win_width`, `win_height`, `win_set_title`, `timer_start`, `timer_now`,
`console_log`, `file_size`, `file_read`, `exit`. `caps.c` is the policy layer: `CAPS_MAX 8`,
`file_allowed()` (`:47-64`) enforces the `/apps/` prefix and rejects `..`, and
`caps_check()` (`:194-211`) audits refusals with a per-run cap of 16 so a hostile app
cannot flood the console. The pool allocator **zeroes linear memory on every allocation**
(`:623-691`) because the LionsOS libc `mmap` never frees.

*Sandboxed* (`SANDBOX=1`) — `sandbox_host.c` (583) does real seL4 capability management.
Each sandbox gets a **guarded 4096-slot CNode** minted at start-up (`:344-373`);
`make_objects()` retypes in chunks of 256; `map_frame()` makes missing page tables **from
the sandbox's own Untyped** for the sandbox VSpace and from a bookkeeping Untyped for the
host's VSpace (`:144-150` explains why); `load_runner()` (`:210-263`) parses the embedded
ELF64 and validates that segments do not share pages; `revoke_window()` (`:306-316`)
**unmaps then deletes**, and `sandbox_stop()` finishes with `seL4_CNode_Revoke` on the
Untyped. `runner.c` (550) is the program loaded *into* the sandbox — **not a Microkit PD**:
it keeps using Microkit's caps, provides `__sel4_ipc_buffer` at `0xfffffff000`, and has a
custom musl `runner_syscall` supporting only `SYS_writev` on fd 1/2 (`:142-175`).

Design note in `sandbox.h` worth reading before changing anything: a **handle is an index
into the host's caps table, into the grants page, and into the recorded frame ranges, all
at once** — `sandbox_revoke_grant()` takes them apart on `cap_drop`. The three indices
must agree, hence `CAPS_MAX <= SANDBOX_MAX_GRANTS` is a `_Static_assert`. The mailbox
(64 messages + 16 requests, two rings in one page) has three such asserts (`:149-161`).

`wasm_apps/` are freestanding wasm32 modules built with
`--target=wasm32 -O2 -nostdlib -ffreestanding -Wl,--no-entry -Wl,--allow-undefined
-Wl,-z,stack-size=16384`: `hello.c`, `life.c` (Conway), `mandel.c` (Mandelbrot, 8 rows
per tick), `reader.c` (reads its one granted file), `probe.c` (**deliberately asks for too
much** — including `/secret.txt`, which lives outside `/apps` so it can never be granted —
and tallies blocked vs leaked), and `spin.c` (**only in `SANDBOX=1` builds**; computes
forever, and only a sandbox can stop it).

Cost model from the README: ~1,047 kernel objects and a few hundred KiB of copying per
sandbox start; each sandbox PD costs a 16 MiB Untyped and a window slot.

Build specifics: `apps_disk.img` is a 16 MiB GPT with one FAT partition at 1 MiB,
`mcopy`-ing the `.wasm` + `.caps` + `readme.txt` into `/apps/` and `secret.txt` into `/`
(needs gdisk, dosfstools, mtools). QEMU uses `virtio-gpu-device,bus=virtio-mmio-bus.31`
with `DESKTOP_XRES ?= 1024`, `DESKTOP_YRES ?= 768`; the framebuffer must fit the 4 MiB
GPU data region.

### 7.2 `firewall` — a multi-interface stateful router with a web management UI

The most elaborate example. Three interfaces, each with its own `ethernet_driver{i}.elf`,
Rx/Tx virtualiser, ARP requester and ARP responder, and **one filter per protocol**
(`icmp_filter`, `udp_filter`, `tcp_filter`). One `router` PD, one `icmp_module` PD, and
one `micropython` PD running a **Microdot** web server on port 80 that is both the GUI and
the control plane (`ui_server.py`, 1,065 lines, inline-heredoc HTML + `fetch()` against a
JSON API at `:140-428`, bridged to C by the `lions_firewall` MicroPython module).

`pyfw/` is an object-oriented Python framework that builds the SDF *and* computes C struct
sizes **from DWARF**: `memory_layout.py:127-169` runs `llvm-dwarfdump`, scans for
`DW_TAG_structure_type` / `DW_AT_byte_size`, and fills in the entry size — so region sizes
always match the C code. `sdfgen_helper.py` (468 lines) also contains a C-header →
Python-class generator, with its assumptions documented at `:11-46` (headers passed in
dependency order, comments must be whole-line, `num_X` implies the length of `X`).

Constants (`pyfw/constants.py`, 320 lines): interfaces 0 "external" `172.16.2.1/16`,
1 "internal0" `192.168.1.1/24`, 2 "internal1" `10.0.2.1/24`; priorities ethernet_driver
101 / tx_virt 100 / rx_virt 99 / arp_requester 98 / arp_responder 95 / filters icmp 90,
udp 91, tcp 92; `supported_filter_actions` says **TCP does not support REJECT**
(`0x06: [1,1,0,1]`). All three interfaces default to ALLOW per protocol.

C sources: `net_components/firewall_network_virt_rx.c` (208) extends the sDDF Rx
virtualiser with `get_protocol_match()` (`:34-`) that dispatches by ethertype and then by
**ARP opcode** or **IPv4 protocol number** — that is how one virtualiser feeds the ARP
responder, the ARP requester and three filters. `firewall_network_virt_tx.c` (197) maps a
DMA physical address back to a client by walking its data-region base and capacity, because
a buffer can come back from a *different* interface's virtualiser. `arp/arp_requester.c`
(336) owns the ARP cache with `ARP_MAX_RETRIES 5`, `ARP_RETRY_TIMER_S 1`,
`ARP_CACHE_LIFE_M 5`. `routing/routing.c` (455) is the router, with deferred-notify
booleans then batched `microkit_notify` at `:432-453`. `icmp/icmp_module.c` (294)
generates DEST_UNREACHABLE, TTL_EXCEED and ECHO_REPLY.

**`docker/` is the only real runtime test harness in the repo** — which makes the
"CI is build-only" statement a choice, not a limitation.
`scripts/firewall_configuration.sh` is the single source of truth for the topology;
`net_setup.sh` builds a bridge + tap + netns + veth pair per interface with a **default
route via the firewall IP**; `autotest.sh` (739 lines) is **shUnit2**-based with 12
`test_*` functions fanning out over interface pairs via `call_all_interfaces` /
`call_all_interface_pairs`, covering ping reachable, host/net unreachable, TTL exceeded,
ICMP reject, UDP/TCP connect, broadcast, subnet broadcast, and rule add/remove via the API.

Build: ~50 `objcopy --update-section` calls, one per interface per PD. CI builds **only
`imx8mp_iotgate` debug + release** (`ci/firewall.sh:39-40`) — the qemu 3-interface path
and the entire docker harness are **not** exercised in CI.

### 7.3 `vmm` — full device passthrough hypervisor

The only example with **no `meta.py`/sdfgen**: the SDFs are hand-written and run through
`cpp -I. -P`, with the RAM layout generated into `vmm_ram.h` by `sed`-ing the actual initrd
size in (`vmm-dev.mk:119-126`).

`vmm.system` (98 lines) — `odroidc4`, passthrough variant. 1 GiB guest RAM at
`0x80000000` with `page_size="0x200_000"`, ~11 passthrough device MMIO regions, a single
`VMM` PD at **priority 254**, a `<virtual_machine name="linux" priority="1">` with one
vcpu, and **22 IRQs** on the VMM (including GPIO 96-103 → ids 23-30).

`vmm-virtio-console.system` (223 lines) — serial-over-virtio variant. Splits `bus2` so
the UART can go to a LionsOS serial driver, adds 12 sDDF serial regions, drops the VMM to
priority 10, and adds `uart_driver` (100), `serial_tx_virtualiser` (99),
`serial_rx_virtualiser` (98) plus 4 channels.

`vmm/vmm.c` (124) vs `vmm/vmm-virtio-console.c` (229). Both `fault()` handlers reply with
`microkit_msginfo_new(0,0)` so the guest resumes. Requires Armbian `Image`,
`initrd.img` and the meson-sm1 DTB — downloaded content-addressed from lionsos.org.

### 7.4 `kitty` — card-tap kiosk with a *Linux driver VM* for graphics

A payment-card-tap kiosk: a MicroPython PD drives a framebuffer, a PN532 NFC reader on I²C,
and a TCP link to a demo server, while a Linux VM owns the GPU. The framebuffer is set up
by a Linux userspace process using **UIO**, via a fault handler registered at
`FB_UIO_INIT_ADDRESS` (0x300000) that simply `microkit_notify`s the MicroPython PD.
`include/lions/fb/fb.h:10-19` calls this out as "not a principled way of doing this…
in the future we will transition away from this" (tracking issue au-ts/lionsos#141).
The `desktop` example is that transition.

`client/kitty.py` (408) is all the business logic, with an NFC debounce requiring
`TICKS_TO_CONFIRM 2` consecutive matches and `TICKS_TO_RESET 3` consecutive non-matches
to avoid double taps, and a second UID read for long MiFare Ultralight UIDs. `main()`
runs `asyncio.gather(heartbeat, read_from_server, read_card_main)`. `client/pn532.py`
(199) is a hand-rolled PN532 driver written to be type-checkable on CPython too.

`server/server.py` (274) is a **stripped-down demo server** that deliberately cycles
through error responses (checksum error, transaction error, "new", "good", bad exception,
account disabled) so the client can be exercised against a hostile server.

Checked-in reference `.system` files for both boards (328 and ~340 lines) are useful
reading — they show the concrete generated layout. The 64 MiB `shared_nfs_micropython`
region and the 0x8000 command/completion queues are both visible there.

### 7.5 The rest

- **`webserver`** — MicroPython + Microdot serving files off an NFS export. Requires
  `NFS_SERVER`, `NFS_DIRECTORY` and `WEBSITE_DIR` to be set (the `Makefile` `$(error ...)`s
  on each). This is the cleanest small example for understanding the FS + net + MicroPython
  wiring: 10 PDs, 9 ELFs, and a readable 20-call `objcopy` block at `webserver.mk:85-105`.
- **`fileio`** — FS test + benchmark (`fs_test.py`, `bench.py`). Builds for
  `maaxboard` and `qemu_virt_aarch64`.
- **`posix_test`** — exercises the POSIX layer from C. Sets `MAX_FDS=8`
  (`posix_test.mk:57`) to make fd-exhaustion testing cheap.
- **`wasm_test`** — the WAMR host-side equivalent, in C (`test_client.c`, `test_core.c`,
  `test_file.c`, `test_server.c`).
- **`dynamic_caps`** — see below.

### 7.6 `dynamic_caps` — the run-time capability spike

Answers one question: can a Microkit system give a single *manager* PD real seL4 authority
so it can make, grant and revoke kernel objects at run time, and hand a sandbox memory that
another PD maps, with the kernel enforcing the result? Answer: yes, with a 147-line patch
to the Microkit tool. It is step 3 of the desktop's app plan; step 4 is
`examples/desktop` with `SANDBOX=1`.

`manager.c` (356) is the whole spike. The interesting part is what the SDF **cannot**
express, and the patch that fixes it (`microkit-cspace-untyped-cnode-frames.patch`, 226
lines, touching 4 files in the Microkit tool):

- `cap_untyped size="0x100000"` — a fresh Untyped as a **root object** in the manager's cspace
- `cap_cnode pd="sandbox"` / `cap_vspace` — real kernel capabilities to another PD
- `cap_frames mr="surface"` — a CNode with one cap per frame of a memory region, so frame
  *i* is addressable by CPtr `(slot << 58) | i`
- `cap_frames`'s `pd` attribute is actually a **memory region**, validated against `mrs`
  not `pds` (`sdf.rs:100-117`); `cap_untyped` has no PD at all

A system using none of these builds **byte-identically** (`README.md:120-123`), so the
patch is safe to carry.

Documented limits (`README.md:160-204`), all of which are real design constraints to
respect:
- Caps that came *from the SDF* cannot be revoked by revoking the Untyped — only
  *originals* have derivation children, and `cap_frames` caps are CapDL **copies**, so the
  manager must remember and delete its own copies (`manager.c:175-183`)
- A dropped signal is **not** a fault; only a failed lookup raises a cap fault
- The sandbox TCB/SchedContext/CSpace are still static; the sandbox's own Microkit image
  is still mapped; only 63 root slots exist (`PD_ROOT_CAP_BITS` = 6, and slot 0 is
  Microkit's own CNode, though the tool accepts slots up to 127 which do not exist)
- The Untyped size is fixed at build time; `cap_frames` grants a whole region

`watch_without_signals()` (`manager.c:263-284`) raises the sandbox TCB to the manager's
priority and yields — a neat trick for single-stepping a sandbox with no debugger.

## 8. Concurrency models, side by side

Four different models coexist deliberately for the same class of job. **Do not normalise
them** — each fits its backend.

| Component | Model | Where |
|---|---|---|
| MicroPython | 2 libco cothreads (event + VM), VM hooks to yield, 4 interrupt-emulation modes | `micropython.c:238-249`, `mpconfigport.h:105-112`, `modinterrupt.c` |
| WAMR | 2 libco cothreads (event + `wamr_main`) + 1 ms timer | `wamr.c:210-219`, `:168-194` |
| NFS | single-threaded, 1 ms timer, libnfs `nfs_which_events`/`nfs_service` + 511-entry continuation pool | `nfs.c:67-93`, `op.c:43-141` |
| FAT | 5 libco cothreads (4-worker pool + event loop), per-worker block-region slices, semaphore-guarded disk waits | `fat/event.c:97-104`, `fat/io.c:36-49` |
| desktop `wasm_host` | 1 cothread on a 0x100000 stack, waiting on a semaphore signalled by compositor/timer notifications | `wasm_host.c:886-1000` |
| router (firewall) | single-threaded + deferred-notify booleans | `routing.c:415-453` |

## 9. Known bugs and rough edges

Found 2026-10-01, then **re-verified line by line before any fix was written** — which
materially changed the list. The corrections to the original read-only pass are noted
inline, because a plausible-looking bug report that has not been re-read is worth very
little. Three have been fixed and now have host-side tests; the rest are open.

### 9.0 Fixed, with tests

- **`lib/fs/helpers/helpers.c` — out-of-bounds in *both* allocators.** (FIXED)
  The original report said only `fs_request_allocate`. Re-reading showed
  `fs_buffer_allocate` had the same defect: both loops ran to `NUM_BUFFERS` (2044) while
  indexing 511-entry tables. Once the real slots were full, `fs_request_allocate` handed
  out ids above `REQUEST_ID_MAXIMUM` — which `fs_process_completions()` *drops* and
  `fs_command_issue()` *asserts* on, so the caller waits forever for a completion that was
  thrown away. `fs_buffer_allocate` issued offsets the 64 MiB share does not cover.
  Root cause: the number of outstanding *requests* (bounded by the request table, because
  a completion must be matchable) was conflated with the number of *share buffers* (bounded
  by the share region's size). The two are now bounded separately, and
  `buffer_metadata` is sized for `NUM_BUFFERS` — a 64 MiB share really does hold
  2048 × 32 KiB slices, so 2044 was the original intent.
  ASan on the pre-fix code: `global-buffer-overflow ... 0 bytes after global variable
  'buffer_metadata' ... of size 511`. UBSan: `index 511 out of bounds for type
  'request_metadata [511]'`. Covered by `test/test_fs_helpers.c`.
- **`components/micropython/vfs_fs_file.c` — double free.** (FIXED)
  `mp_vfs_fs_file_open` released the path buffer once after `FILE_OPEN` and again on the
  `FILE_SIZE` failure path. `fs_buffer_free()` asserts on a buffer that is not in use, so a
  debug build aborts and a release build walks the metadata table and reissues an offset
  another call may own. `read` and `write` were re-checked and are correctly paired; this
  was the only double free in the file.
- **`components/fs/nfs/op.c` — `handle_rename`.** (FIXED)
  It copied `params.old_path` into *both* path buffers, so every NFS rename was a
  self-rename. **Correction to the original report:** `new_path` was not uninitialised —
  it received a *copy of the old path*. The buffer was initialised, just with the wrong
  contents, so the failure was silent wrong-data rather than a read of uninitialised
  memory. All eight `fs_copy_client_path` call sites in the file were re-checked; this was
  the only copy-paste error.
- **The unbounded-transfer defect, which was the worst of the lot.** (FIXED)
  Neither MicroPython path clamped the transfer to `FS_BUFFER_SIZE`, and neither server
  checked it. **The write path was client-side:** `vfs_fs_file_write` did
  `memcpy(fs_buffer_ptr(write_buffer), buf, size)` into a 32 KiB slot *before the command
  was even sent*, so no server-side check could ever have caught it. **The read path was
  server-side:** the client sent `.buf.size = size` unclamped and the server wrote that
  many bytes at the slot offset — `fs_get_client_buffer` validates only against the
  64 MiB share, never the 32 KiB slot. So an oversized read clobbered whatever the client
  had placed beside the slot in shared memory.

  The root cause was that `FS_BUFFER_SIZE` was a purely conventional client-side number
  with nothing enforcing it anywhere. Of four senders, two honoured it
  (`lib/libc/posix/file.c` and `examples/desktop/wasm_host` both chunk) and two did not;
  neither receiver checked. Fixed on both sides rather than at the four sites that
  happened to be wrong, because fixing sites is exactly what had already let this through
  twice:

  - `FS_BUFFER_SIZE` moved from `include/lions/fs/helpers.h` (a client header) to
    `protocol.h`, beside `FS_MAX_PATH_LENGTH` and `FS_MAX_NAME_LENGTH`, which are the same
    kind of bound both sides must agree on. Zero call-site churn, since `helpers.h`
    already includes `protocol.h`.
  - New `fs_get_client_slot()` in `lib/fs/server/memory.c` — the existing trust boundary,
    now slot-aware. All four read/write handlers use it (FAT and NFS, read and write), and
    all four already replied `FS_STATUS_INVALID_BUFFER` on a NULL accessor, so no handler
    logic changed. Oversized requests are **rejected, not truncated**: a caller with a bug
    gets a loud error instead of silent corruption, and the two correct senders chunk so
    they never trip it. `fs_get_client_buffer` is deliberately left permissive for the
    paths that legitimately use larger or differently-sized buffers (`handle_stat`,
    `handle_dir_read` — each of which validates its own minimum and writes a bounded
    amount).
  - The three unclamped callers now clamp: `vfs_fs_file_read`, `vfs_fs_file_write`, and
    `modfs_raw.c:request_pread`, **a second unclamped site the original report missed**,
    where `nbyte` came straight from a Python argument.

  Clamping rather than looping is sufficient because a short read or write is a legitimate
  answer for `mp_stream_poll_write`/`mp_stream_poll_read`, so MicroPython retries. The read
  path additionally clamps the server-reported `len_read`, since the server is a separate
  protection domain and its count is not trusted to fit the caller's buffer either.
  Covered by `test/test_fs_server_memory.c`.

### 9.1 Open — load-bearing

- **`components/fs/nfs/op.c:203-204`** — `handle_deinitialise` is an **empty function that
  never replies**. A client issuing `FS_CMD_DEINITIALISE` hangs forever. What it should
  do — reply success, or actually unmount — is a design decision.
- **`components/fs/fat/op.c:613-621`** — `handle_dir_read` sets
  `FS_STATUS_END_OF_DIRECTORY` at `:615` and then **unconditionally overwrites it** at
  `:621` with `FS_STATUS_ERROR`, so end-of-directory is unreachable. Root cause: an
  `FRESULT` and an `FS_STATUS_*` share the variable `RET` (`:606` even assigns
  `RET = FS_STATUS_ERROR`). The fix is to compute `args->status` directly.
- **`components/fs/fat/io.c:89-105`** — `disk_ioctl` declares `DRESULT res;` and returns it
  uninitialised for any `cmd` other than `GET_SECTOR_SIZE` and `CTRL_SYNC`.
  **Correction to the original report:** `GET_SECTOR_SIZE` is fine — `:94` does assign
  `res = RES_OK`. The real gap is `GET_BLOCK_SIZE` (which `f_getfree` needs) and anything
  else FatFs may ask for.
- **`components/micropython/modfb.c:77`** — the `cache_clean` length uses the *source*
  `width*height*4` instead of `config->xres*config->yres*4`, **and** is measured from
  `framebuffer_data_region` rather than from the pixels. `fb_base_addr()` puts the pixels
  at `region + sizeof(fb_config_t)` (`fb.h:73`), so the correct end address is
  `framebuffer + config->xres*config->yres*4`. Two independent errors, only one of which
  was originally reported.
- **`components/micropython/modfb.c:14`** declares `extern void *framebuffer_data_region;`
  while `micropython.c:89` defines it as `uintptr_t`. Same width, wrong type, and it is a
  symbol Microkit overwrites via `setvar_vaddr` (`examples/kitty/board/*/kitty.system`),
  so the initialiser is only a fallback.
- **`components/micropython/machine_i2c.c:95`** — `assert(returned_addr == (i2c_addr_t)addr)`
  aborts the whole PD on a bus-level anomaly. **Correction to the original report:** the
  line number was wrong; the two asserts in the file are at `:95` and `:191`, and `:190`
  already carries a `FIXME: not-assert` on the release path. A bus anomaly taking down
  the PD is still the wrong failure mode, but it is a known-and-noted one, not a surprise.

### 9.2 Cleared — investigated, not defects

- **`include/lions/firewall/filter.h:474`** — `if (match == NULL)` is unreachable, because
  `match` is initialised to the default rule at `:453`. **Traced, and this is dead code
  rather than a live bug:** the specificity comparison at `:479-493` already handles every
  case correctly, precisely *because* the default rule is maximally unspecific (subnet 0,
  both ports "any"), so any specificity in a matched rule beats it. Deleting the dead
  branch is safe; changing the logic would be wrong.
- **`include/lions/fb/fb.h:21` `FB_UIO_INIT_ADDRESS 0x300000`** — looks like a
  factor-of-256 typo next to the framebuffer at `0x30000000`, and was flagged as one.
  **It is not.** The UIO region really is at guest phys `0x30000000`
  (`examples/kitty/board/qemu_virt_aarch64/framebuffer_vmm_images/linux.dts:120`), and
  guest RAM starts at `0x40000000` (`kitty.system:215`). `0x300000` is below both, i.e.
  deliberately unmapped, and `examples/kitty/src/vmm/vmm.c:119` registers
  `uio_init_handler` there: it is a **fault-doorbell** the guest writes to in order to ring
  MicroPython (`:50-54`). The value is correct; only the name is a trap.
- `include/lions/fb/fb.h:29` documents byte 4 as "Alpha (transparency)" while
  `modfb.c:66` writes `0` (opaque). The code is right for a display framebuffer — a fully
  opaque alpha is what you want. If anything it is the *comment* that is misleading.

- **Three different magic-check conventions** for the same idea: the FS headers use a
  byte loop (`fs/config.h:31`), the input header uses a scalar compare
  (`input/config.h:28-31`), and MicroPython's `net_config_check_magic` returns a *bool
  capability* rather than asserting.

### 9.3 Build / maintenance debt

- **`components/micropython/micropython.mk:56`** — `MICROPY_MPYCROSS_DEPENDENCY=$(abspath
  mpy_cross/mpy-cross)` points at the build output; it should almost certainly be
  `$(MICROPYTHON_SRC)/mpy-cross/mpy-cross` (compare `:55`).
- **`include/lions/firewall/common.h:19`** — `DEBUG_FIREWALL` is defined **unconditionally**,
  so `LOG_FIREWALL` formatting cost is always paid in production PDs.
- **Hard-coded 64 MiB client share size** in both FS backends rather than reading the
  mapped region size (`components/fs/nfs/op.c:32`, `components/fs/fat/config/fat_config.h:11`).
- **`lib/fs/helpers/helpers.c:72-74`** — explicit TODO to replace the function-pointer
  completion callback with a user-driven completion API.
- **`lib/libc/posix/file.c:263-267`** — `file_dup3` aliases the server fd with a
  `TODO: refcount of underlying file?`. Two POSIX fds share one server fd with **no
  reference counting**, so closing one closes the file under the other.
- **`lib/sock/tcp.c`** — no `tcp_poll` callback installed despite `SO_KEEPALIVE` being set,
  and `sys_check_timeouts` is never called; no socket timeouts at all.

### 9.4 Design limitations (documented, not bugs)

- `lib/libc/posix/sock.c` — no UDP, no IPv6, no Unix sockets; `setsockopt` accepts only
  `SO_LINGER` and ignores it; `ppoll` ignores timeout and sigmask.
- `lib/libc/posix/mem.c` — `munmap` and `mprotect` are no-ops.
- `lib/libc/posix/posix.c:134-160` — `getrandom` is documented as deliberately insecure.
- `lib/libc/posix/file.c:583-584` — `mkdir` mode and `umask` are entirely ignored.
- `lib/libc/posix/file.c:374` — `readlinkat` is a stub returning `-EINVAL`.
- `lib/libc/compiler_rt/fp_mode.c` — rounding is always `TONEAREST`; no dynamic rounding mode.
- FAT and NFS have **no tests** and neither is in a runtime test harness. The firewall's
  docker suite is the only runtime test in the repo.

## 10. Host-side test infrastructure

Because CI is build-only, silent memory corruption in the pure-logic parts of LionsOS
would otherwise never be caught. `test/` closes that gap for the components that can be
compiled for the host:

```sh
make -C test          # runs everything, fails on the first problem
make -C test clean
```

| File | Role |
|---|---|
| `test/Makefile` | Compiles the real component sources for the host with `-fsanitize=address,undefined` and runs each test binary |
| `test/include/microkit.h` | Deliberate stub — provides only `microkit_channel` and `microkit_notify`, so it cannot silently drift from the real header |
| `test/test_fs_helpers.c` | 7 tests for `lib/fs/helpers/helpers.c` — allocator bounds, exhaustion, buffer zeroing, double-free detection |
| `test/test_fs_server_memory.c` | 6 tests for `lib/fs/server/memory.c` — slot bound, share bounds, overflow-safe formulation, path capping |

The rules are written out explicitly per binary rather than as a pattern rule:
target-specific variables are not in scope when make expands a pattern rule's
prerequisites, which silently left the binaries with no dependency on the component
source, so they never rebuilt and a change to the component under test could go
unnoticed. That was a real bug in the first version of this file.

Two constraints that matter:

- **`test/include` is reachable only from `test/Makefile`'s include line.** No target
  build puts it on its include path; the stub must never leak into a real build.
- **Each test runs in a forked child.** The allocator state in `helpers.c` is file-static
  with no reset entry point, so a fresh process is the only way to get a clean allocator;
  it also means an assert can abort freely without taking the suite down. The
  double-free-detection test relies on exactly this.

What is testable this way, and what is not:

| Candidate | Testable? |
|---|---|
| `lib/fs/helpers/helpers.c` | yes — only needs `microkit_notify` and the four extern globals |
| `lib/fs/server/memory.c` | yes — and now tested; see `test/test_fs_server_memory.c` |
| `include/lions/firewall/filter.h` | yes — header-only pure logic; the obvious next addition |
| `lib/fs/server/fd.c` | partly — the generation-tag table is pure logic, but the header pulls in `microkit.h` |
| anything calling `microkit_notify` for real IPC, MicroPython, or lwIP | no |

`lib/fs/server/fd.c`'s generation-tag table is the next candidate: it is the ABA defence
in front of every open file, and its behaviour on a stale fd is currently untested.

## 11. Repo state as of 2026-10-01

- `main` (local) lagged `origin/main`; `fix/sandbox-revocation` and
  `origin/claude/elegant-ramanujan-9bzdkq` both pointed at `5945f93`
  ("examples/desktop: several sandboxed apps at once, each in its own window").
- **A nested submodule is broken:** `dep/micropython/lib/micropython-lib/.git` contains
  `gitdir: /mnt/f/Projet Steven 2026/lionsos/.git/modules/micropython/modules/lib/micropython-lib`,
  a stale path from another checkout. This makes plain `git status` and `git diff` **fail**.
  Work around with `git -c diff.ignoreSubmodules=all <cmd>`, or repair with
  `git submodule update --init dep/micropython`.
- **Uncommitted work in the tree** at the time of analysis: `examples/desktop/wasm_host/sandbox.h`
  (+13) and `sandbox_host.c` (+38/-6), adding `_Static_assert`s on the sandbox grants page
  and mailbox layout plus a `mailbox_mapped` flag and an up-front bounds check in
  `grant_region()`. Appears to be part of the in-progress `fix/sandbox-revocation` work.
- **Four fixes applied across `lib/fs/`, `components/fs/{fat,nfs}/op.c`,
  `components/micropython/{vfs_fs_file,modfs_raw}.c` and `include/lions/fs/`** (see §9.0)
  are uncommitted. None was verified by a full example build, because no Microkit SDK,
  nix or `sdfgen` was available in the environment where they were made.
  `lib/fs/helpers/helpers.c` and `lib/fs/server/memory.c` are covered by host tests. The
  rest are covered only by structural checks (balanced braces, correct accessor
  substitution, verified header chains for the relocated `FS_BUFFER_SIZE`), so **the whole
  set still needs `make -C examples/<name>` on a machine with the SDK** — ideally
  `fileio` and `webserver`, which exercise both backends, and `desktop` for MicroPython.
  The server-side change in particular alters observable behaviour: an oversized transfer
  now gets `FS_STATUS_INVALID_BUFFER` instead of quietly overrunning.

## 12. Where to start reading

| If you want to understand | Read, in this order |
|---|---|
| The IPC model | `include/lions/fs/protocol.h` → `lib/fs/helpers/helpers.c` → `lib/fs/server/memory.c` |
| How a component is configured | `examples/webserver/webserver.mk:85-105` → `examples/webserver/meta.py` → `include/lions/fs/config.h` |
| The POSIX plumbing | `lib/libc/posix/posix.c` → `include/lions/posix/fd.h` → `lib/libc/posix/fd.c` → `lib/libc/posix/file.c` |
| Concurrency patterns | `components/fs/fat/event.c:167-284` (coroutines) vs `components/fs/nfs/op.c:43-141` (continuations) |
| The desktop | `examples/desktop/README.md` → `meta.py` → `src/compositor.c` → `wasm_host/sandbox.h` |
| A worked end-to-end example | `examples/webserver` — smallest complete system, most readable `objcopy` block |
| Capability management | `examples/dynamic_caps/README.md` → `manager.c` → the Microkit patch → `wasm_host/sandbox_host.c` |
| How anything gets tested | `test/Makefile` → `test/test_fs_helpers.c` → §10 above |
