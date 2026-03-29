<!--
     Copyright 2026, UNSW
     SPDX-License-Identifier: CC-BY-SA-4.0
-->
# WASM Test Example

This example checks the ability of LionsOS to run Web Assembly code.

## BUILDING
To build, you need the Web Assembly SDK from
https://github.com/WebAssembly/wasi-sdk

We have tested with
https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-27/wasi-sdk-27.0-x86_64-linux.tar.gz

Unpack it somewhere, then you can build with:
```
make BUILD_DIR=<path-to-build-dir> WASI_SDK=<path-to-wasi-sdk> \
MICROKIT_SDK=<path-to-microkit-sdk>
```

Supported boards at the time of writing are `qemu_virt_aarch64` and
the `maaxboard`.  The build system defaults to building for qemu.

After the first build, a makefile is set up in the build directory
that saves build-time options.  Doing
```
make -C <build_dir>
```
is sufficient for rebuilding after changing any source code.

# Running

```
make -C <build_dir> qemu
```
will run the test if built for `qemu_virt_aarch64`

If built with `MICROKIT_BOARD=maaxboard` the resulting image is in
*build_dir*`/wasm_test.img`; it is a binary blob.  Use U-Boot to load
it at 0x50000000, then do `go 0x50000000` to run.
