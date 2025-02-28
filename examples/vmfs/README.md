# Virtualised Filesystem

This example system showcases a Filesystem (FS) being virtualised in a Linux VM on LionsOS. The raw byte data comes from a native block driver (virtIO or MMC) and a FS is exposed to userspace in VM from a FS implementation. A userspace driver then map Linux's File I/O APIs to LionsOS' equivalent, allowing native clients such as Micropython to access the filesystem without having a native FS implementation. This driver accesses the command, completion queue and data regions via UIO, which is coded as a series of memory regions in a DTS overlay.

This approach ensure complex FS implementations are contained in a VM, where vulnerabilities in the implementation does not have the potential to bring down the entire system. We could just reboot the VM if we detect a fault.

## High level overview
```
Block driver -> Block virtualiser -> FS VM -> Micropython
                                                   ^ ^ 
                                                   / /
Ethernet driver -> Network Virtualisers and Copiers /
                                                   /
Timer driver -------------------------------------/
```

## Inside FS VM overview
Assuming an in-kernel FS implementation, the setup is as follows:
```
---- MICROPYTHON
        vfs_fs.c <--------------------------------------|
                                                        |
---- FS VM                                              |
    ---- USERSPACE                                      |
                                                        |
            UIO Driver                         LionsOS FS Protocol
                                                        |
            liburing                                    |
                                                        |
    ---- KERNEL SPACE                                   |
                                                        |
            io_uring                                    |
                                                        |
            FS implementations (FAT, ext4,...)      block -> FS
                                                        |
            virtIO block                                |
                                                        | 
    ---- VIRTUAL MACHINE MONITOR                        |          
                                                        |
            virtIO block                                |
                                                        |
---- BLOCK VIRTUALISER                                  |
                                                        |
---- BLOCK DRIVER                                       |  
                                                     blocks 
        Device registers and DMA >----------------------|

```

## Supported platforms
### QEMU Virt AArch64
An MBR disk with 1 FAT partition is generated during the build process as an image, which is presented as a virtIO block device inside the VM. 

Though you can provide your own disk image with any Filesystems supported by the Linux kernel.

To run the example:
```
$ make qemu MICROKIT_BOARD=qemu_virt_aarch64 MICROKIT_SDK=<absolute path to SDK>
```

#### Boot
On a successful boot, you would see the following and can start playing with the example (note, the networking error is due to our minimal Linux kernel not having a network stack compiled in):
```
LDR|INFO: jumping to kernel
Bootstrapping kernel
Warning: Could not infer GIC interrupt target ID, assuming 0.
available phys memory regions: 1
  [60000000..c0000000]
reserved virt address space regions: 3
  [8060000000..8060244000]
  [8060244000..8062eda000]
  [8062eda000..8062ee4000]
Booting all finished, dropped to user space
MON|INFO: Microkit Bootstrap
MON|INFO: bootinfo untyped list matches expected list
MON|INFO: Number of bootstrap invocations: 0x0000000e
MON|INFO: Number of system invocations:    0x000035ce
MON|INFO: completed bootstrap invocations
MON|INFO: completed system invocations
MON|INFO: PD 'timer_driver' is now passive!
fs_driver_vmm|INFO: starting "fs_driver_vmm"
fs_driver_vmm|INFO: Copying guest kernel image to 0x40000000 (0xcb4a00 bytes)
fs_driver_vmm|INFO: Copying guest DTB to 0x47f00000 (0xabf bytes)
fs_driver_vmm|INFO: Copying guest initial RAM disk to 0x47000000 (0x181f5f bytes)
fs_driver_vmm|INFO: initialised virtual GICv2 driver
fs_driver_vmm|INFO: starting guest at 0x40000000, DTB at 0x47f00000, initial RAM disk at 0x47000000
Begin input
'micropython' is client 0
'fs_driver_vmm' is client 1
MP|INFO: initialising!
fs_driver_vmm|ERROR: vGIC distributor is not enabled for IRQ 0x47
fs_driver_vmm|ERROR: vIRQ 0x47 is not enabled
micropython: mpnetworkport.c:138:netif_status_callback: DHCP request finished, IP address for netif e0 is: 10.0.2.15
Starting syslogd: fs_driver_vmm|ERROR: Unexpected channel, ch: 0x1
OK
Starting klogd: OK
Running sysctl: OK
Saving 256 bits of non-creditable seed for next boot
Starting network: ip: socket: Function not implemented
ip: socket: Function not implemented
FAIL
UIO(FS): *** Starting up
UIO(FS): Block device: /dev/vda
UIO(FS): Mount point: /mnt
UIO(FS): *** Setting up shared configuration data via UIO
UIO(FS): Found dev @ /dev/uio0
UIO(FS): *** Setting up command queue via UIO
UIO(FS): Found dev @ /dev/uio1
UIO(FS): *** Setting up completion queue via UIO
UIO(FS): Found dev @ /dev/uio2
UIO(FS): *** Setting up FS data region via UIO
UIO(FS): Found dev @ /dev/uio3
UIO(FS): *** Setting up fault region via UIO
UIO(FS): Found dev @ /dev/uio4
UIO(FS): *** Enabling UIO interrupt on command queue
UIO(FS): *** Creating epoll object
UIO(FS): *** Binding command queue IRQ to epoll
UIO(FS): *** Initialising liburing for io_uring
UIO(FS): *** Consuming requests already in command queue
UIO(FS): *** All initialisation successful!
UIO(FS): *** You won't see any output from UIO FS anymore. Unless there is a warning or error.
MicroPython v1.22.2 on 2025-02-27; QEMU virt AArch64 with Cortex A53
>>> import os
>>> os.listdir()
[]
>>> os.mkdir("hello world")
>>> os.listdir()
['hello world']
>>> with open("sel4.txt", "w") as f:
...     f.write("Formally verified!")
... 
18
>>> os.listdir()
['hello world', 'sel4.txt']
>>> with open("sel4.txt", "r") as f:
...     print(f.readlines())
... 
['Formally verified!']
>>> 
```

### Maaxboard
The example uses the Block device @ `soc@0/bus@30800000/mmc@30b40000` and by default mounts the first partition. If you need to change this, see `meta.py`.

To get a bootable image, run:
```
make MICROKIT_BOARD=maaxboard
```

## Troubleshooting
If you get a memory fault on boot, try to remove intermediate build artifacts with `rm -rfd build` and re-compile a fresh image.
