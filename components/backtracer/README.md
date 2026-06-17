# Usage
1. Include `backtracer.mk` file.
2. For all targets to be backtraced:
    1. Add `libunwind.a` and `unwind_helpers.o` as targets and add them to be compiled with the target.
    3. Add `-funwind-tables` to `CFLAGS`
    4. Add `--eh-frame-hdr -L{Directory of backtracer}` to `LDFLAGS`
    5. Add `-Tunwind.ld -lunwind` to `LIBS`
    6. Optionally add `--start-group` and `--end-group` to the beginning and end of `LIBS` respectively.
3. For `meta.py`:
    1. Make sure to add the path to this directory in to `PYTHONPATH`
    2. Import `LionsOS_Backtracer`
    3. For all PDs to be traced, add them to a list and pass them to the `enable_backtracing` function.
       This function returns a parent PD containing all of the children given.
       The function prototype is `enable_backtracing(sdf, architecture, PD_list) -> PD`

See the `backtrace_test` example for more details.

# Dependencies
- `libc` for `libunwind`
- llvm-project's `libunwind`
- `sddf` or some implementation of `printf`
- patched `sdfgen` with Memory region prefilling capabilities (updated sdfgen)

# Cons
- Larger binary sizes
- Depending on `llvm-project` (sorry)
