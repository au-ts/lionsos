# Usage
1. Include `backtracer.mk` file.
2. For all targets to be backtraced:
    1. Add `libunwind.a` and `unwind_helpers.o` as targets and add them to be compiled with the target.
    3. Add `-funwind-tables` to `CFLAGS`
    4. Add `--eh-frame-hdr -L{Directory of backtracer}` to `LDFLAGS`
    5. Add `-Tunwind.ld -lunwind` to `LIBS`
    6. Optionally add `--start-group` and `--end-group` to the beginning and end of `LIBS` respectively.
3. For `meta.py`, make sure to add the path to this directory in to `PYTHONPATH`

# Dependencies
- `libc` for ...
- llvm-project's `libunwind`
- `sddf` or some implementation of `printf`
