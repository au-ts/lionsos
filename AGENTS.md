<!--
    Copyright 2026, LionsOS Contributors

    SPDX-License-Identifier: BSD-2-Clause
-->

# AGENTS.md — working on LionsOS

LionsOS is UNSW's research OS: seL4 microkernel + Microkit 2.3.1 userspace model.
Aimed at embedded / IoT / cyberphysical systems, designed for formal verifiability,
adaptability, and microkernel-class performance. KISS is an explicit design goal.

Full architecture deep-dive: **[`docs/codebase-analysis.md`](docs/codebase-analysis.md)** — read it
when you need detail beyond this file (per-component breakdown, exact line references,
concurrency models, the full known-bugs list).

Long-term resource and security direction: **[`docs/typed-resource-model.md`](docs/typed-resource-model.md)**.
Read it before introducing a new service, resource handle, application permission,
namespace, broker, or cross-PD protocol. It records the project's NT-inspired goal of
making authority a coherent graph of explicitly typed resources while preserving
seL4's small kernel and capability model. It also records the intended end-user
namespace and the project's preference for small, visible, end-to-end desktop progress.

## Ground rules

- **Language:** C11 + Python 3 (build-time codegen) + Python (on-target for MicroPython).
  AArch64 only, clang/lld, freestanding (`-nostdlib`).
- **Target is `qemu_virt_aarch64`** unless the example says otherwise. Boards in use:
  `qemu_virt_aarch64`, `odroidc4`, `maaxboard`, `imx8mp_iotgate`.
- **Style:** follow the surrounding file. 4-space indent, `//` comments in newer code,
  `snake_case`, `_Static_assert` is the assertion idiom. No comments unless asked.
- **REUSE/SPDX is enforced by CI.** Every new file needs an SPDX header, or a
  `.reuse/dep5` entry. Use `Copyright <year>, LionsOS Contributors` /
  `SPDX-License-Identifier: BSD-2-Clause` for new work.
- **Trailing whitespace is enforced by CI** (`.github/workflows/pr.yaml`).
- **CI is build-only — there are no runtime tests.** Don't assume a target test suite
  exists. There *is* a small host-side suite in `test/`; see below.
- **Delivery:** prefer the smallest demonstrable end-to-end slice that moves the usable
  desktop forward. A rough but honest vertical slice is useful; do not disguise missing
  security enforcement, corrupt data, or make an unstable protocol permanent merely to
  produce a demo. See `docs/typed-resource-model.md` under “Incremental delivery”.

## Build

No top-level Makefile. Each example is built from its own directory:

```sh
cd examples/<name>
MICROKIT_BOARD=qemu_virt_aarch64 make
```

`nix develop` gives you the pinned toolchain (Microkit SDK 2.3.1, WASI SDK 27,
sdfgen 0.35.0, clang, qemu, dtc, dosfstools, gptfdisk, mtools, cmake, gcc for mpy-cross).

CI equivalent: `./ci/examples.sh $(pwd) $MICROKIT_SDK`.

#### Without nix

Only the Microkit SDK and sdfgen are actually needed to build; both can be fetched
directly, which is enough to reproduce CI on a machine that has no nix.

```sh
# Microkit SDK 2.3.1 (the release tarball behind the flake's microkit-url)
curl -sSLO https://github.com/seL4/microkit/releases/download/2.3.1/microkit-sdk-2.3.1-linux-x86-64.tar.gz
tar xzf microkit-sdk-2.3.1-linux-x86-64.tar.gz

# sdfgen 0.35 -- meta.py asserts version('sdfgen').split(".")[1] == "35"
python3 -m venv sdkvenv && ./sdkvenv/bin/pip install 'sdfgen==0.35.0'

export MICROKIT_SDK=/path/to/microkit-sdk-2.3.1
export PYTHONPATH=/path/to/sdkvenv/lib/python3.*/site-packages
./ci/examples.sh $(pwd) $MICROKIT_SDK
```

Notes from doing this:

- The venv's Python must match the `python3` that runs `meta.py`; check
  `sdkvenv/lib/pythonX.Y` rather than assuming a version.
- Each example rebuilds musl and lwIP from scratch into `$LIONSOS/ci_build/`, so a
  full `ci/examples.sh` run takes about an hour. Running the `ci/*.sh` scripts in
  parallel (16 cores) brings it to roughly 20 minutes.
- `ci_build/` is **not** gitignored (only `build/` is). Remove it after a run or it
  shows up as untracked.
- `examples/wasm_test` does not build this way: its Makefile invokes `clang` for the
  wasm apps with no `--target=wasm32-wasi`, relying on WASI SDK 27's clang defaulting
  to wasm and using `wasm-ld`. A host clang instead falls back to `/usr/bin/ld`, which
  rejects `-Wl,--initial-memory=`. Everything else builds.

### The build pipeline (memorise this — it explains most "why" questions)

Two stages, per example:

1. `examples/<name>/Makefile` — thin wrapper. Requires `MICROKIT_SDK`. Writes a
   generated `build/Makefile` from the `examples/<name>/<name>.mk` snippet, passing
   through `MICROKIT_BOARD`, `MICROKIT_CONFIG` (default `debug`), `BUILD_DIR`.
2. `build/Makefile` (from the `.mk`) — the real build:
   - `meta.py` runs **sdfgen** to emit the microkit system description `*.system`.
   - ~20 `objcopy --update-section` calls patch **config blobs** into the ELFs.
   - `$(MICROKIT_TOOL) $(SYSTEM_FILE) ...` produces `*.img` + a memory report.

`examples/vmm` and `examples/dynamic_caps` are the exceptions: hand-written `.system`
files (vmm runs its SDF through `cpp`; dynamic_caps needs a patched Microkit SDK).

Build-flag changes are checksummed into stamp files to force rebuilds —
e.g. `.fat_cflags-$(shell … | shasum)` in `components/fs/fat/fat.mk:38-42`.

## The two architectural invariants

**1. Every cross-PD message is a fixed-size struct in a shared-memory ring.
Microkit channels are only edge-triggered wakeup hints — never the transport.**
There is no Microkit RPC anywhere. Three protocol families:

| Protocol | Header | Shape |
|---|---|---|
| Filesystem | `include/lions/fs/protocol.h` | 64-byte `fs_cmd_t`/`fs_cmpl_t` (both `_Static_assert`ed at `:249`/`:299`), 511-entry SPSC rings, 19 status codes, 20 commands |
| GUI | `include/lions/gui/protocol.h` | surface + state + events per app slot, seqlock-style `seq`/`damage` |
| Input | `include/lions/input/input.h` | byte-identical to `struct virtio_input_event` — virtIO drivers need no marshalling |

The elegant bit in the FS protocol: a buffer is an **offset into the client's shared
region**, not a pointer (`protocol.h:134`). That keeps messages fixed-size and
address-space-independent. Servers never dereference client-supplied pointers — all
untrusted offsets funnel through `fs_get_client_buffer` / `fs_copy_client_path`
(`lib/fs/server/memory.c`).

The share region is divided into **32 KiB slots** (`FS_BUFFER_SIZE`, `protocol.h:26`), one
per `fs_buffer_allocate()`. A single `FILE_READ`/`FILE_WRITE` may move at most one slot's
worth of data. Callers must **chunk** (see the loops in `lib/libc/posix/file.c`); the
servers enforce the limit with `fs_get_client_slot()`, which rejects a wider transfer
with `FS_STATUS_INVALID_BUFFER` rather than truncating it.

**2. Per-PD configuration is injected at build time via `objcopy --update-section`.**
Every PD declares `__attribute__((section(".fs_client_config")))` (or `.timer_client_config`,
`.serial_client_config`, `.net_driver_config`, `.input_driver_config`, …) and validates a
magic at `init()`. **This is what makes a component optional at runtime without
recompiling** — if the SDF didn't wire it up, the magic check fails and the component
degrades. Follow this pattern when adding configuration.

## Long-term direction: typed resources and explicit authority

For new architecture, follow [`docs/typed-resource-model.md`](docs/typed-resource-model.md).
The short version is:

- Model externally visible resources as explicit types with narrow operations and
  unforgeable handles or seL4 capabilities; do not add ambient authority.
- Keep policy and object management in isolated user-space services. Do not grow seL4
  into an NT-style monolithic object manager.
- Make delegation, attenuation, revocation, ownership and lifetime visible in the
  protocol rather than implied by process-global state.
- Design protocols so the system can eventually answer "what exactly may this PD or
  agent do?" as one inspectable authority graph, with auditable grant, use, denial and
  revocation events.
- Preserve compatibility through versioned, typed protocol contracts, not by exposing
  service internals.

Apply this direction incrementally when adding or substantially changing a subsystem.
Do not rewrite working components merely to rename them "objects", and do not introduce
inheritance hierarchies or C++-style object orientation: "object" here means a typed,
referenceable resource with controlled operations and lifetime.

## How the POSIX layer works

`lib/libc/` is a **musl-libc shim**, not a from-scratch libc. It writes the function
pointer `sel4_vsyscall` into musl's internal `__sysinfo` (`lib/libc/posix/posix.c:203`),
so all 38 registered syscalls funnel through one static table.

```
musl wrapper (open/read/write/socket)
  → sel4_vsyscall            (__sysinfo hook, posix.c:203)
    → syscall_table[n]       (posix.c:45)
      → fd_entry_t vtable    (include/lions/posix/fd.h:31)
        ├─ file_read/write  → fs_command_blocking()  → ring + microkit_notify
        │                     ↳ spin: blocking_wait(server_ch)   ← cothread yield
        └─ sock_*           → libc_socket_config_t vtable → lib/sock/tcp.c → lwIP
```

Key injection point: `fs_set_blocking_wait()` (`include/lions/fs/helpers.h:26`) lets the
client supply its own yieldable blocking primitive. That is how the *same* object file
serves blocking POSIX callers and async MicroPython/WAMR callers.

Notes:
- The fd table is per-component global state, so a component is effectively single-threaded.
  Hence `getpid` and `set_tid_address` both return 1, and `nanosleep` has no signal
  interruption. Don't "fix" this — it's a consequence of the design.
- `lib/libc/compiler_rt/` is a vendored LLVM subset providing **only** 128-bit soft-float
  (`long double`) for AArch64 hard-float. Not a general libgcc replacement.
- `lib/sock/tcp.c` is not in any archive — it's compiled straight into each component that
  wants TCP, then handed to `libc_init()` as `extern libc_socket_config_t socket_config;`.

## Conventions you'll see everywhere

- **Dispatch by designated-initializer table indexed on the protocol enum.** e.g.
  `operation_functions[]` at `components/fs/fat/event.c:65-86`. The comment there states
  the table must stay consistent with the protocol enum order — it is a wire contract.
- **Named config symbols with a `*_vaddr` suffix convention** (from `dep/sddf/resources/common.h`).
- **Logging via `sddf_printf`**, not `printf`, so it works before libc is up:
  `dlog()` / `dlogp()` in `include/lions/util.h:12-20`.
- **Three different concurrency models coexist deliberately** for the same job:
  MicroPython = 2 cothreads; WAMR = 2 cothreads; NFS = single-threaded + continuation pool;
  FAT = 4-worker cothread pool. Pick the model that fits the backend, don't normalise them.
- **Coroutine argument passing is the idiom for async results**: handlers start with
  `co_data_t *args = microkit_cothread_my_arg();` and write `args->status`.

## Gotchas

- **The FS server's open-file table uses generation tags** (`lib/fs/server/fd.c:46,157-159`)
  to prevent ABA / stale-fd reuse. Don't simplify that away.
- **`fd_unset` fails while the fd is busy** — that's the source of
  `FS_STATUS_OUTSTANDING_OPERATIONS`, and it's what makes `close` safe against in-flight
  NFS RPCs. The close protocol is: `fd_begin_op` → `fd_end_op` → `fd_unset` → real close.
- **FAT silently drops out-of-range commands** (`event.c:255-258`); **NFS replies
  `FS_STATUS_INVALID_COMMAND`** (`nfs/op.c:110-113`). Intentional divergence, don't unify.
- **`include/lions/firewall/common.h:19` defines `DEBUG_FIREWALL` unconditionally**, so
  `LOG_FIREWALL` formatting cost is always paid in production PDs.
- `components/fs/*/op.c` hard-code a 64 MiB client share size rather than reading the
  mapped region size (`nfs/op.c:32`, `components/fs/fat/config/fat_config.h:11`).

## Host-side tests

CI is build-only, so there is a small host test suite for the pure-logic parts:

```sh
make -C test          # runs everything, fails on the first problem
```

`test/Makefile` compiles the real sources (`lib/fs/helpers/helpers.c`, …) for the host
against `test/include/microkit.h`, a deliberate stub, and runs them under
`-fsanitize=address,undefined`. Add a test by stubbing only the external symbols the
component actually needs; **never** put `test/include` on a target build's include path.

Each test runs in a forked child, because the allocator state in `helpers.c` is
file-static with no reset entry point and asserts should be free to abort.

Only components with no Microkit dependency in their logic can be tested this way.
`lib/fs/server/memory.c` and `include/lions/firewall/filter.h` are the obvious next
candidates; anything calling `microkit_notify`, MicroPython or lwIP cannot.

## Known bugs

Verified against source on 2026-10-01. **Nothing below has been re-verified since** —
treat line numbers as a starting point to grep from. Full detail and the corrections
to the original analysis are in `docs/codebase-analysis.md` § 9.

### Fixed

- **FIXED** `lib/fs/helpers/helpers.c` — both `fs_request_allocate` *and*
  `fs_buffer_allocate` looped to `NUM_BUFFERS` (2044) over 511-entry tables, walking off
  the end of both. `request_metadata` ids are now bounded by `FS_QUEUE_CAPACITY`;
  `buffer_metadata` is sized for `NUM_BUFFERS`, since a 64 MiB share really does hold
  2048 × 32 KiB slices. Covered by `test/test_fs_helpers.c`.
- **FIXED** `components/micropython/vfs_fs_file.c` — `mp_vfs_fs_file_open` released the
  path buffer twice on the `FILE_SIZE` failure path, aborting a debug build.
- **FIXED** `components/fs/nfs/op.c` — `handle_rename` copied `params.old_path` into both
  path buffers, so every NFS rename was a self-rename.
- **FIXED** the unbounded-transfer defect, which was the worst of the lot. Neither
  MicroPython read/write path clamped the transfer to `FS_BUFFER_SIZE`, and neither
  server checked it — `memcpy(fs_buffer_ptr(write_buffer), buf, size)` overflowed the
  slot before the command was even sent, and a large read had the server write past it.
  Fixed on both sides: `fs_get_client_slot()` now rejects any transfer wider than one
  slot, used by all four server handlers; the three unclamped callers (two in
  `vfs_fs_file.c`, one in `modfs_raw.c:request_pread`) now clamp. `FS_BUFFER_SIZE` moved
   from `helpers.h` to `protocol.h`, since the server needs it and it is a wire contract.
   Covered by `test/test_fs_server_memory.c`.
 - **FIXED** `components/fs/fat/op.c` `handle_dir_read` — one variable carried both a
   `FRESULT` and an `FS_STATUS`. End of directory is `FR_OK` with an empty name, which was
   turned into `FS_STATUS_END_OF_DIRECTORY` and then overwritten by the trailing
   `(RET == FR_OK)` ternary, so a client walking a directory saw `FS_STATUS_ERROR` and
   never finished. `RET` is now only a `FRESULT` and the status goes to `args->status`.
 - **FIXED** `components/fs/fat/io.c` `disk_ioctl` — `res` was uninitialised for any `cmd`
   other than `GET_SECTOR_SIZE` and `CTRL_SYNC`, so it returned garbage. Only those two
   are issued at run time (`FF_USE_MKFS` and `FF_USE_TRIM` are both 0), so it now
   defaults to a no-op `RES_OK`.
 - **FIXED** `components/fs/nfs/op.c:handle_deinitialise` — an empty function that never
   replied, so a client issuing `FS_CMD_DEINITIALISE` hung forever. It now replies
   `FS_STATUS_ERROR` when there is no mount, and otherwise does an `nfs_umount_async`
   whose callback calls `nfs_destroy_context`, clears `nfs`, and replies. A failed
   unmount leaves the mount intact so the client can retry.
 - **FIXED** `components/micropython/modfb.c` — `cache_clean` covered the *source* image's
   `width * height * 4` starting at the region base, but the loop writes the
   destination's `config->xres * config->yres * 4` starting after the `fb_config_t`, so
   whenever the sizes differed part of the frame stayed dirty in the cache. It now cleans
   the range actually written. `framebuffer_data_region` is also declared `uintptr_t` to
   match `micropython.c`, rather than `void *`.
 - **FIXED** `components/micropython/machine_i2c.c` — both asserts could take the whole
   PD down on a bus-level anomaly. `assert(returned_addr == addr)` checked something
   `sddf_i2c_nb_return` explicitly does not promise (it cannot match overlapping requests
   to responses, and a failed dequeue leaves `returned_addr` at its initialiser); the
   `err` it already returns reports either. `assert(i2c_bus_release(...))` aborted when
   the virtualiser refused a release, discarding a completed transfer; it now releases
   unconditionally and logs, resolving that `FIXME: not-assert`. No asserts remain here.

### Open — worth looking at

None outstanding. The one below is a toolchain TODO rather than a defect:

- `examples/wasm_test` needs WASI SDK 27's clang to link (see "Without nix" above); it
  compiles but cannot be linked with a host clang.

### Not bugs

- `include/lions/firewall/filter.h` — `if (match == NULL)` is unreachable, but removing
  it is all that is needed: the specificity comparison below it already handles every
  case correctly, because the default rule is maximally unspecific. Dead code, not a
  defect.
- `include/lions/fb/fb.h` `FB_UIO_INIT_ADDRESS 0x300000` — **not** a typo for the
  framebuffer's `0x30000000`. It is a fault-doorbell on a deliberately unmapped address;
  `examples/kitty/src/vmm/vmm.c:119` registers the handler and the guest writes there to
  ring it. The name is a trap, but the value is correct.

## Repo state notes

- Local `main` lags `origin/main`; the work branches `fix/sandbox-revocation` and
  `claude/elegant-ramanujan-9bzdkq` both pointed at `5945f93` as of 2026-10-01.
- **Known-broken nested submodule:** `dep/micropython/lib/micropython-lib/.git` contains a
  stale `gitdir:` path from another checkout (`/mnt/f/Projet Steven 2026/...`). This makes
  plain `git status` / `git diff` **fail** in this working copy. Work around with
  `git -c diff.ignoreSubmodules=all <cmd>`, or repair with
  `git submodule update --init dep/micropython`.
- MAINTAINERS.md maps directories to people (`@dumsum` = Simon, `@Courtney3141` = Courtney,
  `@dreamliner787-9` = Bill, `@midnightveil` = Julia). Several components are `UNASSIGNED`.
  Check it before assuming who to route a change to.

## Adding a new example

1. `examples/<name>/Makefile` (wrapper) + `examples/<name>/<name>.mk` (real build).
2. `examples/<name>/meta.py` — instantiate sdfgen `ProtectionDomain`s, call
   `connect()` + `serialise_config()` on each subsystem, then `sdf.render()`.
3. `examples/<name>/manifest.py` — MicroPython frozen-module manifest.
4. `examples/<name>/ci.sh` script, and add the line to `ci/examples.sh`.
5. Set `MICROPYTHON_FROZEN_MANIFEST` if the example is Python-driven.
