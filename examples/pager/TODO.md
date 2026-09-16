# Features to implement
- Memory-related syscalls: WIP - necessary for frees.
- Demand paging with page replacement algorithm.
- filesystem page cache.

# TODO RIGHT NOW:
The pager now lives in lionsos/components/pager/, so these paths are relative to there.
- copy the frame in the CoW branch of fault() in src/pager.c
- use the frame_copies CNode: add a get_frame_copy()/put_frame_copy() free list to
  src/frame_table.c, and give the shadow PTE a second software bit so an entry can
  say which of the three CNodes its index refers to. Then rewire clone_leaf() in
  src/proc.c, which currently maps the one frame cap into both the parent's and the
  child's VSpace -- a frame cap carries its own mapping, so that cannot work.
- anonymous memory is not zeroed. seL4 only clears an untyped on reset, so a frame
  straight out of untyped_alloc() holds whatever was in RAM, and put_frame() recycles
  frames as they are. It is invisible on qemu because RAM boots zeroed. Zeroing on the
  write-fault path would cost a 4KiB clear per fault, so it wants a scratch window and
  a bulk pass at refill time -- frame_table_zero_gzp() already does the mapping trick.
- nothing reads elf_caps/elf_sizes yet. The Microkit tool fills them in for every
  client, but process_fork() still has to remap the child's ELF frames out of them.