<!--
     Copyright 2024, UNSW
     SPDX-License-Identifier: CC-BY-SA-4.0
-->

# The Lions Operating System

This is the source code for LionsOS, an operating system based on the
seL4 microkernel using the seL4 Microkit.

LionsOS is aimed at embedded, IoT and cyberphysical systems and is designed to
be formally verifiable, adaptable to a wide class of use cases in the target
domain, while at the same time setting the benchmark for performance of
microkernel-based operating systems. We aim to achieve all three goals by a
highly modular yet ruthlessly performance-oriented design and strict adherence
to the time-honoured KISS principle.

LionsOS and its verification story are under active research and development.

For more information, see the [website](https://lionsos.org).

# Using the reload crate:

# Using 

# For the basic stack unwinder:

Remote stack unwinding is done using the reloading protection domain. Because Microkit is designed so that child PDs fault to their parent, and because we have access to the TCB struct of the faulting PD, we can see the instruction it faulted on. From there, we rely on the frame pointer and link register being adjacent on the stack (true when the code is unoptimised) to walk back through the call stack. Currently this requires mapping the entire stack of the faulting PD into the reloader, in a region called `stack_map`.

LionsOS is normally built with optimisations enabled, using the `-O` flags. Since optimised code doesn't guarantee the link register and frame pointer stay adjacent, I wrote a non-POSIX bash wrapper around clang that strips out these optimisation flags before calling the real clang, and instead adds `-O0 -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -fno-optimize-sibling-calls`. The easiest way to use this wrapper is to alias it in your shell config so it's called in place of clang.

The stack unwinder itself just prints out the address of each frame pointer, with `BACKTRACE_START` and `BACKTRACE_END` marking the addresses of interest. To turn these addresses into readable output, I wrote a script that takes the path to the relevant ELF file and wraps `llvm-addr2line -e` to resolve them to source lines. I'd like to make this faster, but currently don't keep a copy of the ELF file with line number information on the server side, so this step has to happen after the fact.

