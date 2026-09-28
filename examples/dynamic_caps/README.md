<!--
     Copyright 2026, LionsOS Contributors
     SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Dynamic capabilities (experimental)

A spike answering one question: can a Microkit system give a single manager
PD real seL4 authority, so that it can make, grant and revoke kernel objects
at run time, with the kernel enforcing the result? It can, with a small
Microkit patch.

This is step 3 of the plan for the desktop's apps. Today, WebAssembly apps in
`examples/desktop` run inside one PD, and their capabilities are enforced by
that PD (see `examples/desktop/wasm_host/caps.h`). Here the enforcement is by
seL4 itself: the sandboxed program is a real thread in its own address space,
and all it can reach is what the manager maps and grants.

## What it does

`dynamic_caps.system` has a `manager` PD and an empty child PD, `sandbox`.
Through `<cspace>`, the manager holds:

| Slot | Capability |
|------|------------|
| 1 | a 1 MiB Untyped, which the manager alone owns (`cap_untyped`, new) |
| 2 | the sandbox's root CNode (`cap_cnode`, new) |
| 3 | the sandbox's VSpace |
| 4 | its own VSpace |
| 5 | its own root CNode (`cap_cnode`, new) |
| 6 | its own TCB |

At run time the manager:

1. Makes frames, page tables and a notification from the Untyped, and copies
   the program (`program.c`, a flat binary in the manager's image) into the
   frames through a window in its own VSpace.
2. Maps the code read-only and executable, and a stack and a shared page
   non-executable, into the sandbox's VSpace.
3. Mints a badged, send-only copy of the notification into slot 7 of the
   sandbox's CNode.
4. Restarts the sandbox's thread at the program's entry point.

The program counts in the shared page and signals the notification. Then:

* **Round 1.** The manager revokes the Untyped. seL4 deletes every object
  made from it, wherever the caps to them are, including the program's
  memory, its page tables and its copy of the notification. The program's
  next instruction fetch faults, and the kernel delivers the fault to the
  manager.
* **Round 2.** The manager makes everything again from the same Untyped, and
  starts the program again. This time it revokes only the notification. The
  program keeps running, but its signals now go to an empty slot, which seL4
  drops: they never reach the manager. Finally the manager stops the program
  and revokes the Untyped.

Output on QEMU (debug kernel):

```
MANAGER: round 1: spawning
MANAGER: made 10 objects from the untyped, starting the program
MANAGER: signal with badge 1, the program's counter is 1
MANAGER: signal with badge 1, the program's counter is 2
MANAGER: signal with badge 1, the program's counter is 3
MANAGER: revoked the untyped: the program's memory, page tables and notification are gone
MANAGER: the kernel reports a VM fault in the sandbox: ip 0x0000000080000048, address 0x0000000080000048 (instruction fetch)
MANAGER: round 2: spawning again from the same untyped
MANAGER: made 10 objects from the untyped, starting the program
MANAGER: signal with badge 1, the program's counter is 1
MANAGER: signal with badge 1, the program's counter is 2
MANAGER: revoked only the sandbox's copy of the notification
<<seL4(CPU 0) [decodeInvocation/643 T0x80602d1800 "sandbox" @80000044]: Attempted to invoke a null cap #2017612633061982208.>>
<<seL4(CPU 0) [decodeInvocation/643 T0x80602d1800 "sandbox" @80000044]: Attempted to invoke a null cap #2017612633061982208.>>
<<seL4(CPU 0) [decodeInvocation/643 T0x80602d1800 "sandbox" @80000044]: Attempted to invoke a null cap #2017612633061982208.>>
MANAGER: the program counted from 2 to 5 and 0 signals reached us
MANAGER: revoked the untyped: the program's memory, page tables and notification are gone
MANAGER: DONE
```

The kernel lines are from the debug kernel, which reports each dropped
signal. The CPtr is 7 << 58, the sandbox's slot 7.

## The Microkit patch

Microkit 2.3.1's `<cspace>` can only hand out TCB, SchedContext and VSpace
caps. `microkit-cspace-untyped-cnode.patch` (69 lines, against the `2.3.1` tag)
adds two elements:

* `<cap_untyped slot=".." size=".." />`: a fresh Untyped of a power-of-two
  size, carved from free memory by the CapDL initialiser. It is a root object
  of the spec, so the tool's allocation checks count it like any other
  object.
* `<cap_cnode slot=".." pd=".." />`: a cap to a PD's root CNode. Its guard,
  52 bits on 64-bit platforms, makes up the rest of a 64-bit lookup, so that
  the holder names slot `j` of that CNode with the CPtr `(slot << 58) | j`.
  As a root for CNode invocations, it addresses slots with depth 58.

It also teaches the tool the size of an Untyped when sorting root objects,
which the initialiser relies on. A system that uses neither element builds
to a byte-identical image: this was checked with Microkit's `hello` example
and with `examples/desktop`.

Build a patched SDK from source:

```sh
git clone --branch 2.3.1 https://github.com/seL4/microkit.git
cd microkit
git apply /path/to/lionsos/examples/dynamic_caps/microkit-cspace-untyped-cnode.patch
cargo build --release -p microkit-tool
# Copy the stock 2.3.1 SDK and replace its tool
cp -r /path/to/microkit-sdk-2.3.1 /path/to/microkit-sdk-2.3.1-caps
cp target/release/microkit /path/to/microkit-sdk-2.3.1-caps/bin/microkit
```

Only the host tool changes; the kernel, the initialiser, the monitor and
libmicrokit are those of the stock SDK.

## Building and running

For `qemu_virt_aarch64` only. It needs clang, ld.lld and llvm-objcopy.

```sh
make MICROKIT_SDK=/path/to/microkit-sdk-2.3.1-caps qemu
```

This example is not built by CI, since CI uses the stock SDK.

## Findings

* **Feasible, and small.** 69 lines in the Microkit tool are enough for a PD
  to run a real, kernel-enforced sandbox: it makes objects, maps memory into
  another address space, grants capabilities into another CSpace, and takes
  them all back with one `seL4_CNode_Revoke`.
* **Revocation is complete and cheap.** Revoking the Untyped removed every
  derived object, including page tables and the caps granted into the
  sandbox's CSpace, and reset the Untyped for reuse. The manager needs no
  bookkeeping to find what it gave away.
* **Revoking one capability is precise.** Revoking the notification removed
  only the sandbox's copy. The program kept running with its memory intact.
* **Faults go to the manager** through Microkit's existing `fault()` entry
  point, since the sandbox is its child.
* **A dropped signal is not a fault.** A Send or Signal to an empty slot is
  silently dropped (a debug kernel prints it). Only a failed lookup, such as
  a CPtr through a missing CNode, raises a cap fault. A sandbox therefore
  learns that it lost a capability only if the protocol tells it so.

## Limits and caveats

* **The sandbox's thread, SchedContext and CSpace are still static.** The
  manager reuses the sandbox PD's TCB and SchedContext, so the number of
  concurrent sandboxes is fixed in the system description. Making threads at
  run time also needs a SchedControl cap to give them budget, which is a
  much larger grant of authority and is not in this patch.
* **The sandbox's own Microkit image is still mapped.** The program is
  loaded next to the empty PD's ELF, stack and IPC buffer. A real design
  would give the sandbox a VSpace with nothing but what the manager maps,
  which also needs an ASID pool or a VSpace made at build time.
* **Only 63 slots.** A PD's root CNode has 64 slots (`PD_ROOT_CAP_BITS` is
  6), slot 0 holding Microkit's own CNode. The manager gets slots 1 to 63
  for everything it makes. A real manager would retype a CNode of its own
  and use it as a second level. Note also that the tool accepts `<cspace>`
  slots up to 127 (`CAP_MAP_MAX_SLOT`), which do not exist in a 64-slot
  CNode; libmicrokit already rejects them.
* **A PD's own TCB is not in its CSpace.** `microkit.h` defines `TCB_CAP`
  (6), but Microkit 2.3.1 fills that slot only in the benchmark
  configuration, so the manager is given its own TCB with `cap_tcb`. It
  needs it as the authority to run the sandbox at its own priority in round
  2, because the program never blocks.
* **The Untyped's size is fixed at build time,** and the manager must never
  hand the Untyped itself, or a CNode cap with rights to it, to a sandbox.
