#include "AsyncFATFs.h"
#include "ff15/source/ff.h"
#include "ff15/source/diskio.h"
#include "../../../dep/sddf/include/sddf/blk/queue.h"
#include "co_helper.h"
#include "../../../include/lions/fs/protocol.h"
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <microkit.h>

#define COROUTINE_STACKSIZE 0x40000

#define Client_CH 1
#define Server_CH 2

#define SDDF_ARGS_SIZE 6

#ifdef FS_DEBUG_PRINT
#include "../../../dep/sddf/include/sddf/util/printf.h"
#endif

blk_queue_handle_t blk_queue_handle_memory;
blk_queue_handle_t *blk_queue_handle = &blk_queue_handle_memory;

struct fs_queue *fatfs_command_queue;
struct fs_queue *fatfs_completion_queue;

blk_req_queue_t *request;
blk_resp_queue_t *response;

// Compromised code here
// Config pointed to the SDDF_blk config
blk_storage_info_t *config;

void* coroutine_stack_one;
void* coroutine_stack_two;
void* coroutine_stack_three;
void* coroutine_stack_four;

uintptr_t client_data_offset;

// File system metadata region
void* fs_metadata;

// Flag for determine if there are blk_requests pushed by the file system
// It is used to determine whether to notify the blk device driver
bool blk_request_pushed = false;

typedef enum {
    FREE,
    INUSE
} space_status;

typedef struct co_args{
    fs_cmd_params_t args;
    uint64_t ret_status;
    fs_cmpl_data_t retv;
    #ifdef FS_USE_MICROLIBCO
    /* Cothread handle of this job into libmicrokitco */
    microkit_cothread_t handle;
    /* SDDF queue semaphore */
    microkit_cothread_sem_t synch_sem;
    #endif
} co_args_t;

typedef struct FS_request{
    /* Client side cmd info */
    uint64_t cmd;
    // This args array have 9 elements
    // The first 6 are for 6 input args in file system protocol
    // The last 3 are for returning operation status and return data
    uint64_t args[9];
    fs_cmd_params_t params;
    
    uint64_t request_id;
    /* FiberPool metadata */
    co_handle_t handle;
    /* self metadata */
    space_status stat;
} fs_request;

// This operations list must be consistent with the file system protocol enum
void (*operation_functions[])() = {
    fat_mount,
    fat_unmount,
    fat_open,
    fat_close,
    fat_stat,
    fat_pread,
    fat_pwrite,
    fat_fsize,
    fat_rename,
    fat_unlink,
    fat_mkdir,
    fat_rmdir,
    fat_opendir,
    fat_closedir,
    fat_sync,
    fat_readdir,
    fat_seekdir,
    fat_telldir,
    fat_rewinddir,
};

static fs_request RequestPool[MAX_COROUTINE_NUM];

void fill_client_response(fs_msg_t* message, const fs_request* finished_request) {
    message->cmpl.status = finished_request->args[Status_bit];
    message->cmpl.data = finished_request->args[First_data_bit];
    message->completion.data[0] = finished_request->args[First_data_bit];
    message->completion.data[1] = finished_request->args[Second_data_bit];
    return;
}

// Setting up the request in the requestpool and push the request to the FiberPool
void setup_request(int32_t index, fs_msg_t message) {
    RequestPool[index].request_id = message.cmd.id;
    RequestPool[index].cmd = message.cmd.type;
    memcpy(RequestPool[index].args, &(message.cmd.params), SDDF_ARGS_SIZE * sizeof(uint64_t));
    FiberPool_push(operation_functions[RequestPool[index].cmd], RequestPool[index].args, 
      2, &(RequestPool[index].handle));
    return;
}

// For debug
#ifdef FS_DEBUG_PRINT
void print_sector_data(uint8_t *buffer, unsigned long size) {
    for (unsigned long i = 0; i < size; i++) {
        if (i % 16 == 0) {
            sddf_printf("\n%04lx  ", i); // Print the offset at the start of each line
        }
        sddf_printf("%02x ", buffer[i]); // Print each byte in hexadecimal
    }
    sddf_printf("\n");
}
#endif

void init(void) {
    // Init the block device queue
    // Have to make sure who initialize this SDDF queue
    blk_queue_init(blk_queue_handle, request, response, BLK_QUEUE_SIZE);
    /*
       This part of the code is for setting up the FiberPool(Coroutine pool) by
       assign stacks and size of the stack to the pool
    */
    struct stack_mem stackmem[4];
    stackmem[0].memory = coroutine_stack_one;
    stackmem[0].size = COROUTINE_STACKSIZE;
    stackmem[1].memory = coroutine_stack_two;
    stackmem[1].size = COROUTINE_STACKSIZE;
    stackmem[2].memory = coroutine_stack_three;
    stackmem[2].size = COROUTINE_STACKSIZE;
    stackmem[3].memory = coroutine_stack_four;
    stackmem[3].size = COROUTINE_STACKSIZE;
    // Init coroutine pool
    co_init(stackmem, 4);
    
    // Init file system metadata
    init_metadata(fs_metadata);
}

// The notified function requires careful management of the state of the file system
/*
  The filesystems should be blockwait for new message if and only if all of working
  coroutines are either free(no tasks assigned to them, no pending replies) or blocked in diskio.
  If the filesystem is blocked here and any working coroutines are free, then the fatfs_command_queue 
  must also be empty.
*/
void notified(microkit_channel ch) {
    #ifdef FS_DEBUG_PRINT
    sddf_printf("FS IRQ received::%d\n", ch);
    #endif
    fs_msg_t message;
    // Compromised code here, polling for server's state until it is ready
    while (!config->ready) {}

    switch (ch) {
        case Client_CH: 
            break;
        case Server_CH: {
            blk_response_status_t status;
            uint16_t success_count;
            uint32_t id;
            while (!blk_resp_queue_empty(blk_queue_handle)) {
                // This id should be the index to the request pool
                blk_dequeue_resp(blk_queue_handle, &status, &success_count, &id);
                
                #ifdef FS_DEBUG_PRINT
                sddf_printf("blk_dequeue_resp: status: %d success_count: %d ID: %d\n", status, success_count, id);
                #endif
                
                co_set_args(RequestPool[id].handle, (void* )(status));
                co_wakeup(RequestPool[id].handle);
            }
            break;
        }
        default:
            #ifdef FS_DEBUG_PRINT
            sddf_printf("Unknown channel:%d\n", ch);
            #endif
            return;
    }

    // This variable track if the fs should send back reply to the file system client
    bool Client_have_replies = false;

    blk_request_pushed = false;

    int32_t index = 0;
    int32_t i;

    // This variable track if there are new requests being popped from request queue and pushed into the couroutine pool or not
    bool New_request_popped = true;
    /**
      I assume this big while loop is the confusing and critical part for dispatching coroutines and send back the results.
    **/
    while (New_request_popped) {
        // Performance bug here, should check if the reason being wake up is from notification from the blk device driver
        // Then decide to yield() or not
        // And should only send back notification to blk device driver if at least one coroutine is block waiting
        co_yield();
        
        /** 
        If the code below get executed, then all the working coroutines are either blocked or finished.
        So the code below would send the result back to client through SDDF and do the cleanup for finished 
        coroutines. After that, the main coroutine coroutine would block wait on new requests or server sending
        responses.
        **/
        /*
          This for loop check if there are coroutines finished and send the result back
        */
        New_request_popped = false;
        for (i = 1; i < MAX_COROUTINE_NUM; i++) {
            if (co_check_if_finished(RequestPool[i].handle) && RequestPool[i].stat == INUSE) {
                message.cmpl.id = RequestPool[i].request_id;
                fill_client_response(fs_queue_idx_empty(fatfs_completion_queue, index), &(RequestPool[i]));
                index++;
                #ifdef FS_DEBUG_PRINT
                sddf_printf("FS enqueue response:status: %lu\n", message.cmpl.status);
                #endif
                RequestPool[i].stat= FREE;
                Client_have_replies = true;
            }
        }
        /*
          This should pop the request from the command_queue to the FiberPool to execute, if no new request is 
          popped, we should exit the whole while loop.
        */
        uint32_t command_queue_size = fs_queue_size_consumer(fatfs_command_queue);
        uint32_t completion_queue_size = fs_queue_size_producer(fatfs_completion_queue);
        while (true) {
            if (!co_havefreeslot() || command_queue_size != 0
                  || completion_queue_size != FS_QUEUE_CAPACITY) {
               break;
            }
            fs_queue_pop(fatfs_command_queue, &message);
            #ifdef FS_DEBUG_PRINT
            sddf_printf("FS dequeue request:CMD type: %lu\n", message.cmd.type);
            #endif
            setup_request(index, message);
            RequestPool[index].stat = INUSE;
            New_request_popped = true;
        }
    }
    // If there are replies to client or server, reply back here
    if (Client_have_replies == true) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("FS notify client\n");
        #endif
        microkit_notify(Client_CH);
    }
    if (blk_request_pushed == true) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("FS notify driver\n");
        #endif
        microkit_notify(Server_CH);
    }
}