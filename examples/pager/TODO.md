# Features to implement
- Memory-related syscalls: WIP - necessary for frees.
- Demand paging with page replacement algorithm.
- filesystem page cache.

# TODO RIGHT NOW:
- copy the frame in the CoW branch of fault() in src/pager.c
- use the frame_copies CNode: add a get_frame_copy()/put_frame_copy() free list to
  src/frame_table.c, and give the shadow PTE a second software bit so an entry can
  say which of the three CNodes its index refers to. Then rewire clone_leaf() in
  src/proc.c, which currently maps the one frame cap into both the parent's and the
  child's VSpace -- a frame cap carries its own mapping, so that cannot work.