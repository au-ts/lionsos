<!--
     Copyright 2024, UNSW
     SPDX-License-Identifier: CC-BY-SA-4.0
-->

# MaaXBoard images

## Linux kernel

### Details
* Image name: `linux`
* Config name: `linux_config`
* Git remote: https://github.com/torvalds/linux.git
* Tag: v6.1 (commit hash: `830b3c68c1fb1e9176028d02ef86f3cf76aa2476`)
* Toolchain: `aarch64-none-elf`
    * Version: GNU Toolchain for the A-profile Architecture 10.2-2020.11 (arm-10.16)) 10.2.1 20201103

You can also get the Linux config used after booting by running the following
command in userspace: `zcat /proc/config.gz`.

### Instructions for reproducing
```
git clone --depth 1 --branch v6.1 https://github.com/torvalds/linux.git
cp linux_config linux/.config
make -C linux ARCH=arm64 CROSS_COMPILE=aarch64-none-elf- all -j$(nproc)
```

The path to the image is: `linux/arch/arm64/boot/Image`.

## Buildroot RootFS image

### Details
* Image name: `rootfs.cpio.gz`
* Config name: `buildroot_config`
* Version: 2023.08.1

### Instructions for reproducing

```
wget https://buildroot.org/downloads/buildroot-2023.08.1.tar.xz
tar xvf buildroot-2023.08.1.tar.xz
cp buildroot_config buildroot-2023.08.1/.config
make -C buildroot-2023.08.1
```

The root filesystem will be located at: `buildroot-2023.08.1/output/images/rootfs.cpio.gz` along
with the other buildroot artefacts.
