# Virtualised Filesystems

This example system showcases a Filesystem (FS) being virtualised in a Linux VM on LionsOS. The raw byte data comes from a native block driver (virtIO or MMC) and a FS is exposed to userspace in VM from a FS implementation. A Userspace IO driver then map Linux's File I/O APIs to LionsOS' equivalent, allowing native clients such as Micropython to access the filesystem without having a native FS implementation.

This approach ensure complex FS implementations are contained in a VM, where vulnerabilities in the implementation does not have the potential to bring down the entire system. We could just reboot the VM if we detect a fault.

Thus the name of this system, "VFS", has a different meaning than what you might be used to.

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
            glibc                                    Syscalls
                                                        |
    ---- KERNEL SPACE                                   |
                                                        |
            Filesystem implementation (FAT, ext4,...)   FS
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
An MBR disk with 1 FAT partition is provided as an image, which is presented as a virtIO block device inside the VM. 

The build script generates a FAT disk image, though you can provide your own disk image with any Filesystems supported by the Linux kernel.

To run the example:
```
$ make qemu MICROKIT_BOARD=qemu_virt_aarch64 MICROKIT_SDK=<absolute path to SDK>
```

# Boot
On a successful boot, you would see the following and can start playing with the example:
```
LDR|INFO: jumping to kernel
Bootstrapping kernel
Warning: Could not infer GIC interrupt target ID, assuming 0.
available phys memory regions: 1
  [60000000..c0000000]
reserved virt address space regions: 3
  [8060000000..8060246000]
  [8060246000..8064634000]
  [8064634000..806463c000]
Booting all finished, dropped to user space
MON|INFO: Microkit Bootstrap
MON|INFO: bootinfo untyped list matches expected list
MON|INFO: Number of bootstrap invocations: 0x0000000e
MON|INFO: Number of system invocations:    0x0000457d
MON|INFO: completed bootstrap invocations
MON|INFO: completed system invocations
MON|INFO: PD 'timer_driver' is now passive!
fs_driver_vmm|INFO: starting "fs_driver_vmm"
fs_driver_vmm|INFO: Copying guest kernel image to 0x40000000 (0x23df200 bytes)
fs_driver_vmm|INFO: Copying guest DTB to 0x47f00000 (0xa47 bytes)
fs_driver_vmm|INFO: Copying guest initial RAM disk to 0x47000000 (0x18636e bytes)
fs_driver_vmm|INFO: initialised virtual GICv2 driver
fs_driver_vmm|INFO: starting guest at 0x40000000, DTB at 0x47f00000, initial RAM disk at 0x47000000
'micropython' is client 0
'fs_driver_vmm' is client 1
MP|INFO: initialising!
fs_driver_vmm|ERROR: vGIC distributor is not enabled for IRQ 0x47
fs_driver_vmm|ERROR: vIRQ 0x47 is not enabled
micropython: mpnetworkport.c:142:netif_status_callback: DHCP request finished, IP address for netif e0 is: 10.0.2.15
[    0.444266] cfs_driver_vmm|ERROR: Unexpected channel, ch: 0x1
acheinfo: Unable to detect cache hierarchy for CPU 0
[    0.487358] loop: module loaded
[    0.492998] virtio_blk virtio1: 1/0/0 default/read/poll queues
[    0.511272] virtio_blk virtio1: [vda] 30712 512-byte logical blocks (15.7 MB/15.0 MiB)
[    0.547366]  vda:
...Linux boot logs...
[    0.801251] Freeing unused kernel memory: 7616K
[    0.804809] Run /init as init process
[    0.808493]   with arguments:
[    0.811743]     /init
[    0.813109]   with environment:
[    0.816355]     HOME=/
[    0.818365]     TERM=linux
[    0.820449]     earlyprintk=serial
Starting syslogd: OK
Starting klogd: OK
Running sysctl: OK
Saving random seed: [    2.723213] random: crng init done
OK
Starting network: OK
fs_driver_init...
UIO(FS): *** Starting up
UIO(FS): Block device: /dev/vda
UIO(FS): Mount point: /mnt
UIO(FS): *** Setting up command queue via UIO
UIO(FS): *** Setting up completion queue via UIO
UIO(FS): *** Setting up FS data region via UIO
UIO(FS): *** Setting up fault region via UIO
UIO(FS): *** Enabling UIO interrupt on command queue
UIO(FS): *** Creating epoll object
UIO(FS): *** Binding command queue IRQ to epoll
UIO(FS): *** Consuming requests already in command queue
UIO(FS): *** All initialisation successful!
UIO(FS): *** You won't see any output from UIO FS anymore. Unless there is a warning or error.
MicroPython v1.22.2 on 2024-12-04; QEMU virt AArch64 with Cortex A53
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
