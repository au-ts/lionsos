Simple VM with passthrough
================================

This directory contains what's needed to run a complete VM with
passthrough of all components (including the GPU) on an Odroid C4.

Such a VM can be used to develop new LionsOS components, and is fairly
easy to adapt to other systems.

What you need
=================

Build and Source dependencies
-----------------------------

It's easiest to build if you have a complete checkout of
[LionsOS](https://github.com/au-ts/LionsOS)

If you follow the instructions at [LionsOS
Docs](https://lionsos.org/docs/kitty/building/) you will have all the
prerequisites.

Payload
-------

You need a kernel, device tree and initrd that work already on your
target system.  The simplest way to get them is to extract them from
`/boot` on your system after it is running.

For Armbian-based systems, the files you need are `/boot/Image`
`/boot/initrd.img` and `/boot/dtb/amlogic/meson-sm1-odroid-c4.dtb`.
Boot your board into Linux, then use `scp` to transfer these files to
your build machine, and put them into the `vmm` subdirectory.

The Makefile will fetch some from our website if they are missing from
`vmm`, but there's no guarantee they'll match what's on your root
filesystem --- they were taken from an
[Armbian](https://github.com/armbian/community/releases/download/24.5.0-trunk.168/Armbian_community_24.5.0-trunk.168_Odroidc4_jammy_current_6.6.21_gnome_desktop.img.xz)
'Jammy' Ubuntu system.

You will also need to update the commandline in `vmm/overlay.dts`,
especially if you are not running Ubuntu Jammy Armbian.

The Microkit
------------

You can use the prebuilt microkit from
[Prebuilt](https://github.com/seL4/microkit/releases/download/1.4.1/microkit-sdk-1.4.1-linux-x86-64.tar.gz)

If you choose to build your own, 
you can build it by following
the instructions in the
[ReadMe](https://github.com/seL4/microkit)

You need version 1.4.1

Building
--------

You need to have `aarch64-none-elf-gcc` etc., in your PATH; plus you
need to set the LIONSOS and MICROKIT_SDK environment variables.

If the vmm-example repository is checked out at the same level in your
directory tree as LionsOS the default will work.

Otherwise do:
```
cd vmm-examples
MICROKIT_SDK=.../path-to/microkit-sdk-1.4.1 LionsOS=.../path-to/LionsOS make
```

If all goes well, a directory called `build`,and a
bootable image `boot/vmdev.img`, will be created.

There are two variants you can build.  The standard variant passes
through _everything_; the `virtio_console` variant uses the sDDF's
UART driver and a virtio console.

To select, do
```
make VARIANT=virtio_console MICROKIT_SDK=... ...
```

