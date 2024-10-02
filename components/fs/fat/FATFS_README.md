# ASYNCFATFS Design Doc
# Overview
ASYNCFATFS is an asynchronous file system implementation for The Lions Operating System, based on Elm-Chan's FatFs - Generic FAT Filesystem Module ([FatFs website](http://elm-chan.org/fsw/ff/)). It supports multiple FAT file system formats, including FAT12, FAT16, FAT32, and ExFAT.

# ASYNCFATFS API and Dependencies
ASYNCFATFS provides and relies on a fully asynchronous file system API and block (blk) driver API. The API facilitates asynchronous file operations that allow the system to remain responsive during I/O operations.

# File System Interface for Clients
To initiate a request to the file system, the client must enqueue one or more requests using the interfaces defined in fs_protocol.h, and notify the file system accordingly. The asynchronous nature ensures that the client does not need to block while waiting for the operation to complete.

# How FATFS Is Modified to Be Asynchronous
The asynchronous design of ASYNCFATFS is very similar to Linux’s io_uring, which efficiently handles non-blocking I/O operations using an event-driven model. The system follows a thread-based event-worker design, where requests are processed without blocking, and I/O operations are handled asynchronously.

g### Event-Worker Thread Model
### Event Thread
The event thread in ASYNCFATFS is responsible for managing and dispatching file system tasks. Much like the submission queue in io_uring, this thread monitors for new requests and assigns them to worker threads when available. The event thread operates non-blockingly.

### Worker Thread
When a request is detected, the event thread assigns it to a worker thread. The worker thread handles the actual file system operation, such as reading or writing files, by interacting with the disk subsystem. Once the disk I/O is initiated, the worker thread sleeps until the operation completes, minimizing resource consumption.

When the disk I/O operation completes, the event thread is notified (akin to the completion queue in io_uring), and it wakes up the corresponding worker thread. This thread then completes its task and sends the result back to the client, ensuring that no operation blocks the event thread.

# Structure of ASYNCFATFS
### Core Files and Directories
- **ff15 folder**:
Contains the implementation of Elm-Chan's FatFs (version 0.15 with patch 3).

- **fatfs_event.c**:
This file manages the initialization process, event handling, and request assignment. It handles enqueuing requests, waking up worker threads, and overall event management.

- **fatfs_op.c**:
Defines wrapper functions executed by worker threads. These functions perform input validation, prepare arguments, and call the actual file system operations defined in ff15/source/ff.h. The interaction between fatfs_event.c and fatfs_op.c is facilitated by an array of operation functions, which fatfs_event.c assigns to the worker threads.

- **fs_diskio.c**:
Contains disk I/O functions, which are called by the file system operations. For instance, if the file system needs to read a specific sector from the disk, it calls disk_read. Disk operations are queued as requests to the sddf queue between the file system and the block device (blk virt), where worker threads may block until responses are received.

# Lifecycle of a File System Operation
The lifecycle of a file system operation in ASYNCFATFS follows these steps:

- **Request Submission**:
A client submits a file operation request to the file system. The request is enqueued in a request queue shared between the client and the file system.

- **Event Thread**:
The event thread is notified of the new request. If a worker thread is available and the response queue between the client and the file system is not full, the event thread dequeues the request and checks its validity.

- **Worker Thread Assignment**:
Once validated, the event thread assigns the corresponding function (from the operation function array) to a worker thread. The assigned function includes validation of input arguments and calls the appropriate file system operations defined in the FatFs library.

- **Disk Operations**:
During execution, the file system function may initiate one or more disk operations, such as reading or writing data. These operations are enqueued as block read/write requests in the queue between the file system and the block subsystem (blk virtualizer).

- **Worker Thread Blocking and Wake-Up**:
After enqueuing the disk request, the worker thread goes to sleep, awaiting the response from the block subsystem. Once fatfs_event.c receives the response, it wakes up the worker thread, which then continues processing.

- **Completion**:
The operation is completed, and the result is sent back to the client via the response queue.

# Limitation and Possible Solution

One major limitation of ASYNCFATFS is that, despite using asynchronous block device driver interfaces, the underlying file system implementation—Elm-Chan's FatFs—assumes that the interfaces provided by the block device driver are synchronous. 

### The Problem
To understand the problem, it’s important to recognize that there are two different kinds of block read/write operations:

1. **Dependent Block Requests**: In some cases, the next block request depends on the result of the previous one. For example, in FATFS, when reading the File Allocation Table (FAT) to determine which sectors to read next, the next block request can only be sent after the previous one completes. In this case, whether the block device driver interface is synchronous or asynchronous makes no difference because the requests are inherently sequential.

2. **Independent Block Requests**: In other cases, block requests are independent of one another. For instance, when reading or writing a file, a read request for a certain sector containing part of a file does not affect the next sector to be read. This means that multiple block requests can be issued at once without waiting for each one to complete individually.

Another important concept is that most file systems use a function that maps a file's logical sector to its physical sector on disk. For example, if a file spans physical sectors 34, 435, and 656, the logical sectors are 0, 1, and 2. The function for mapping might look like this:

```c
// Returns the physical sector number
int map_blocks(struct file* f, int logical_sector);
```

Calling this function with logical sectors 0, 1, and 2 would return 34, 435, and 656, respectively.

### Current Implementation in FATFS
Let’s look at how a simplified read operation is implemented in FATFS today:
```c
// A simplified f_read implementation
FRESULT f_read (
    FIL* fp,    /* Opened file to be read */
    void* buff, /* Data buffer to store the read data */
    UINT btr    /* Number of bytes to read */
) {
    while (btr != 0) {
        // Get the logical sector of the file by calculating the offset/sector_size
        int logical_sector = fp->offset / sector_size;
        // Get the physical sector number
        int physical_sector_num = fat_map_blocks(fp, logical_sector);
        // Read the block
        char block_buffer[sector_size];
        block_read_async(physical_sector_num, block_buffer);
        // Pause until result is received
        result = blocked_until_get_the_result();
        if (result != no_error) {
            return error;
        }
        copy_buffer_and_decrease_btr(block_buffer, buff, &btr);
        increase_offset(&fp->offset);
    }
    return success;
}
```
In this implementation, the system enqueues asynchronous block requests but waits for each request to complete before issuing the next one, even when the block requests sent are independent to each other and could be issued to block device driver together. This design is due to Elm-Chan's FatFs assuming synchronous block interfaces.
### Proposed Solution
To improve performance, the implementation could be modified as follows:
```c
FRESULT f_read_but_more_efficient (
    FIL* fp,    /* Opened file to be read */
    void* buff, /* Data buffer to store the read data */
    UINT btr    /* Number of bytes to read */
) {
    int physical_sectors_to_read[1000];
    int number_of_sectors = 0;

    while (btr != 0) {
        // Get the logical sector of the file by calculating the offset/sector_size
        int logical_sector = fp->offset / sector_size;
        // Get the physical sector number
        physical_sectors_to_read[number_of_sectors] = fat_map_blocks(fp, logical_sector);
        number_of_sectors++;
        decrease_btr(&btr);
        increase_offset(&fp->offset);
    }

    char block_buffer[number_of_sectors][sector_size];
    for (int n = 0; n < number_of_sectors; n++) {
        block_read_async(physical_sectors_to_read[n], block_buffer[n]);
    }
    
    result = blocked_until_get_all_the_results();
    if (result != no_error) {
        return error;
    }
    
    copy_buffer(block_buffer, buff);
    return success;
}
```
In this more efficient implementation, we determine all the physical sectors to be read first and then issue all the block read requests to the block device driver at once, which fully takes advantage of the asynchronous block device driver interfaces we have. This design reduces the number of message-passing events and allows the block device driver to combine smaller reads into larger, more efficient read operations, which improves overall performance.

### Key Improvements of proposed method:
- **Efficient Message Passing**: By determining all the necessary physical sectors in advance and issuing the requests in bulk, we reduce the overhead of individual block requests.
- **Block Driver Optimization**: The block device driver can optimize these requests by combining smaller reads into a larger, more efficient operation, thus improving throughput and reducing I/O latency.
