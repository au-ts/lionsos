# ASYNCFATFS Design Doc
# Overview
ASYNCFATFS is an asynchronous file system implementation for The Lions Operating System, based on Elm-Chan's FatFs - Generic FAT Filesystem Module ([FatFs website](http://elm-chan.org/fsw/ff/)). It supports multiple FAT file system formats, including FAT12, FAT16, FAT32, and ExFAT.

# ASYNCFATFS API and Dependencies
ASYNCFATFS provides and relies on a fully asynchronous file system API and block (blk) driver API. The API facilitates asynchronous file operations that allow the system to remain responsive during I/O operations.

# File System Interface for Clients
To initiate a request to the file system, the client must enqueue one or more requests using the interfaces defined in fs_protocol.h, and notify the file system accordingly. The asynchronous nature ensures that the client does not need to block while waiting for the operation to complete.

# How FATFS Is Modified to Be Asynchronous
The asynchronous design of ASYNCFATFS is very similar to Linux’s io_uring, which efficiently handles non-blocking I/O operations using an event-driven model. The system follows a coroutine-based event-worker design, where requests are processed without blocking, and I/O operations are handled asynchronously.

Event-Worker Coroutine Model
Event Coroutine
The event coroutine in ASYNCFATFS is responsible for managing and dispatching file system tasks. Much like the submission queue in io_uring, this coroutine monitors for new requests and assigns them to worker coroutines when available. The event coroutine operates non-blockingly.

Worker Coroutine
When a request is detected, the event coroutine assigns it to a worker coroutine. The worker coroutine handles the actual file system operation, such as reading or writing files, by interacting with the disk subsystem. Once the disk I/O is initiated, the worker coroutine sleeps until the operation completes, minimizing resource consumption.

When the disk I/O operation completes, the event coroutine is notified (akin to the completion queue in io_uring), and it wakes up the corresponding worker coroutine. This coroutine then completes its task and sends the result back to the client, ensuring that no operation blocks the event thread.

# Structure of ASYNCFATFS
Core Files and Directories
ff15 Folder
Contains the implementation of Elm-Chan's FatFs (version 0.15 with patch 3).

fatfs_event.c
This file manages the initialization process, event handling, and request assignment. It handles enqueuing requests, waking up worker coroutines, and overall event management.

fatfs_op.c
Defines wrapper functions executed by worker coroutines. These functions perform input validation, prepare arguments, and call the actual file system operations defined in ff15/source/ff.h. The interaction between fatfs_event.c and fatfs_op.c is facilitated by an array of operation functions, which fatfs_event.c assigns to the worker coroutines.

fs_diskio.c
Contains disk I/O functions, which are called by the file system operations. For instance, if the file system needs to read a specific sector from the disk, it calls disk_read. Disk operations are queued as requests to the sddf queue between the file system and the block device (blk virt), where worker coroutines may block until responses are received.

co_helper.c
Provides an abstraction layer that simplifies and restricts the interfaces libmicrokitco provides.

# Lifecycle of a File System Operation
The lifecycle of a file system operation in ASYNCFATFS follows these steps:

Request Submission
A client submits a file operation request to the file system. The request is enqueued in a request queue shared between the client and the file system.

Event Coroutine
The event coroutine is notified of the new request. If a worker coroutine is available and the response queue between the client and the file system is not full, the event coroutine dequeues the request and checks its validity.

Worker Coroutine Assignment
Once validated, the event coroutine assigns the corresponding function (from the operation function array) to a worker coroutine. The assigned function includes validation of input arguments and calls the appropriate file system operations defined in the FatFs library.

Disk Operations
During execution, the file system function may initiate one or more disk operations, such as reading or writing data. These operations are enqueued as block read/write requests in the queue between the file system and the block subsystem (blk virt).

Worker Coroutine Blocking and Wake-Up
After enqueuing the disk request, the worker coroutine goes to sleep, awaiting the response from the block subsystem. Once fatfs_event.c receives the response, it wakes up the worker coroutine, which then continues processing.

Completion
The operation is completed, and the result is sent back to the client via the response queue.