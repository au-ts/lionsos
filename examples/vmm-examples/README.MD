Simple VM with passthrough
================================

This directory contains what's needed to run a complete VM with
passthrough of all components on an Odroid C4.

Such a VM can be used to develop new LionsOS components, and is fairly
easy to adapt to other systems.

There are two variants available: `passthrough`, the default, passes
through all devices to the virtual machine.  The other, `virtio-cons`
uses a virtio-console device hooked up to the sDDF UART driver.


What you need
================

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
Use `scp` to transfer these files to your build machine.

There are versions in the Git repo, but these might not match your
root filesystem --- they were taken from an [Armbian](https://github.com/armbian/community/releases/download/24.5.0-trunk.168/Armbian_community_24.5.0-trunk.168_Odroidc4_jammy_current_6.6.21_gnome_desktop.img.xz) 'Jammy' Ubuntu
system.

You will also need to update the commandline in `vmm/overlay.dts`,
especially if you are not running Ubuntu Jammy Armbian.

The Microkit
------------

You can use the prebuilt microkit from
[Prebuilt](https://trustworthy.systems/Downloads/microkit/microkit-sdk-dev-7c679ea-linux-x86-64.tar.gz)

If you choose to build your own.  At the moment, 
the microkit from [Ivan Velickovic's
repository](https://github.com/Ivan-Velickovic/microkit) on the dev
branch is the one to use.

You can build it by following
the instructions in the
[ReadMe](https://github.com/Ivan-Velickovic/microkit)

Building
--------

You need to have `aarch64-none-elf-gcc` etc in your PATH; plus you
need to set the LionsOS and MICROKIT_SDK environment variables.

If the vmm-example repository is checked out at the same level in your
directory tree as LionsOS the default will work.

Otherwise do:
```
cd vmm-examples
MICROKIT_SDK=.../path-to/microkit-sdk-1.2.6 LionsOS=.../path-to/LionsOS make
```


If all goes well, a directory called `build`,and a
bootable image `boot/vmdev.img`, will be created.


You can choose the variant to build on the Make command line:
```
export MICROKIT_SDK=.../path-to/microkit-sdk-1.2.6 
export LionsOS=.../path-to/LionsOS 
make VARIANT=virtio-console
```
