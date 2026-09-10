# RR
A basic recording component which does the following to every PD in it's subsystem:
1. Intercepts IPC via middlemaning
2. Controls scheduling of each PD deterministically (naive version of the kernel scheduler)
3. Records IPC events, their contents and their data.
4. Replays deterministically the flow of events, inspectable via GDB.
5. Records interrupts (So any interrupt-only can be RR'd)

# What RR cannot do
1. Inspect and record MMIO shared memory
2. Accurate representation of how the kernel scheduler works
3. Nested `microkit_ppcall`s, IE `pd_1 ppcall -> pd_2 protected which ppcalls -> pd_3 protected ...`

# Overhead
1. TODO! measure this.

# Implementation
- Requires modified, pinned microkit_acacia, microkit, seL4 and rust-seL4
- Through microkit_acacia, we intercept all IPC by inserting a central collection
  of 2 PDs which receive and forward all data.
- Through priority-controlling, making use of kernel scheduler properties, we block and manually
  allow only certain PDs to run at once, in a hopefully deterministic manner.
- During recording, we record all events at a certain pmu cycle count.
- During replaying, we run through each event step by step, scheduling PDs in the recorded manner,
  and using the VPMU's interrupts to ensure that we orchestrate the sender to send

# To implement
- [ ] move the tcbs to cspace instead of parenting so that watched systems can
      have parental-hierarchy.
