# Virtual-Machine Filesystem

## What is this
This is a userspace Linux driver that allow you to re-use Linux's Filesystem (FS) implementations with a native block driver in LionsOS instead of porting one natively (e.g. FAT).

## High-level idea
- The client filesystem command queue, completion queue and data region are mapped into the guest's userspace via UIO.
- Then, the Filesystem UIO driver dequeues all the commands and map it into the corresponding Linux FS calls.
- Via io_uring, the driver will enqueue all the Linux FS calls into io_uring's submission queue and signal the kernel. io_uring was used to improve performance by allowing us to perform multiple I/O operations with only 1 syscall.
- The driver will wait for all SQEs (Submission Queue Entry) to complete. Then for each CQE (Completion Queue Entry), a callback is invoked to enqueue the completion message to the native client's completion queue.
- The driver finally faults on a pre-determined physical address for the (Virtual Machine Monitor) VMM to signal the native FS client.

## Important files & directory
- `${LIONSOS}/dep/liburing`: an io_uring wrapper library that abstract away the low-level details of the kernel API.
- `fs_driver_init`: a short shell script that runs on VM boot to start the driver. It specifies the block device and mount point paths.
- `*.(c|h)`: driver source that will be compiled by `zig cc` in the `vmfs` example to run in the VM.

The shell script and executable binary will be packaged by `packrootfs` from `${LIONSOS}/dep/libvmm/tools` into the CPIO archive so the VM can access it.

## Notes
Not all FS calls go through io_uring. This includes:
- Mount/unmount.
- Truncate.
- Directory calls (FS_CMD_DIR_*)

For these calls, the driver will "flush" the io_uring submission queue, wait for the flushed operations to finish and invoke the required operation. Acting as a barrier to ensure correctness in mixing synchonrous and asynchronous operations.

## Usage
The VMM needs to be aware of this driver running in the guest to receive memory faults acting as notifications. See the `vmfs` example for details.