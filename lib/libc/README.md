<!--
     Copyright 2025, UNSW
     SPDX-License-Identifier: CC-BY-SA-4.0
-->

# LionsOS libc Integration

This readme explains the LionsOS libc implementation and how to configure and
build it using the provided Makefile snippet.

## Overview

LionsOS uses a custom libc setup based on our
[fork](https://github.com/au-ts/musllibc/tree/lionsos) of
[musllibc](https://musl.libc.org/), plus extra components for POSIX support and
compiler runtime helpers. This setup is compatible with the sDDF build system.

The final libc is built from three main sources:

1. musllibc (our fork): Provides core libc functionality, with system calls
redirected to a general dispatcher.
2. Syscall implementations: Functionality found in `lib/libc/posix/`.
3. Compiler runtime helpers: Low-level arithmetic and runtime support from
`lib/libc/compiler_rt/`.

## musllibc Fork: Syscall Redirection

Our musllibc fork replaces architecture-specific inline assembly syscall traps
(like `svc 0` on AArch64) with a generic function call mechanism. Instead of
invoking syscalls directly with assembly, musl's internal `__syscallN` functions
now call a dispatcher function via `__sysinfo`.

In upstream musl, `__sysinfo` is normally a hook for vDSO (Virtual Dynamic
Shared Object) support on Linux systems, letting musl call kernel-provided
user-space functions without a full syscall. We've repurposed it as a *general*
syscall dispatcher, since all our syscalls are provided in a user-space library.

As a result, all libc syscall invocations are routed through the LionsOS POSIX
syscall handler, which is implemented in the `lib/libc/posix/` layer.
For example:

```c
#define CALL_SYSINFO(n, ...) ((long(*)(long,...))__sysinfo)(n, ##__VA_ARGS__)

static inline long __syscall3(long n, long a1, long a2, long a3) {
    return CALL_SYSINFO(n, a1, a2, a3);
}
```

LionsOS provides the `__sysinfo` implementation in `libc_init()`:

```c
void libc_init() {
    /* Syscall table init */
    __sysinfo = sel4_vsyscall;
...
```

## Usage

To build the LionsOS libc, include the `lib/libc/libc.mk` snippet in your
top-level Makefile:

```make
include $(LIONSOS)/lib/libc/libc.mk
```

This snippet defines and builds the following targets:

- `$(LIONS_LIBC)`: The absolute path to the libc build directory.
- `$(LIONS_LIBC)/include`: The installed musl headers for components requiring
libc. This is also a build target for musllibc, allowing you to specify the
headers as a build prerequisite.
- `$(LIONS_LIBC)/lib/libc.a`: The final static libc archive combining musl,
syscalls, and compiler runtime objects. This must be linked into your final
binary.

For sDDF components, **do not** set `SDDF_CUSTOM_LIBC`. Instead, define the
following:

```make
SDDF_LIBC_INCLUDE := $(LIONS_LIBC)/include
include <path_to_sddf_snippet.mk>
```

This ensures sDDF components use the LionsOS library headers instead of falling
back to sDDF's internal, vendored libc. Since sDDF components list this as a
prerequisite, it also guarantees the headers are available *before* compilation
begins.

## POSIX and Compiler Runtime Extensions

The extra functionality is provided via:
- `lib/libc/posix/*.c`: POSIX wrappers and syscall implementations.
- `lib/libc/compiler_rt/*.c`: Arithmetic helpers and low-level runtime support.

These files are compiled into objects and simply bundled into the final `libc.a`
archive.
