#include "fatfs_decl.h"
#include "ff15/source/ff.h"
#include "ff15/source/diskio.h"
#include "../../../dep/sddf/include/sddf/blk/queue.h"
#include "co_helper.h"
#include "../../../include/lions/fs/protocol.h"
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <microkit.h>

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

typedef struct FS_request{
    /* Client side cmd info */
    uint64_t cmd;
    /* Used for passing data to coroutine and receive response */
    co_data_t shared_data;
    /* Used to track request_id */
    uint64_t request_id;
    /* FiberPool metadata */
    co_handle_t handle;
    /* Self metadata */
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

static fs_request request_pool[WORKER_COROUTINE_NUM];

void fill_client_response(fs_msg_t* message, const fs_request* finished_request) {
    message->cmpl.id = finished_request->request_id;
    message->cmpl.status = finished_request->shared_data.status;
    message->cmpl.data = finished_request->shared_data.result;
    return;
}

// Setting up the request in the request_pool and push the request to the FiberPool
void setup_request(int32_t index, fs_msg_t* message) {
    request_pool[index].request_id = message->cmd.id;
    request_pool[index].cmd = message->cmd.type;
    request_pool[index].shared_data.params = message->cmd.params;
    co_submit_task(operation_functions[request_pool[index].cmd], &request_pool[index].shared_data, &(request_pool[index].handle));
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
    uint64_t stack[WORKER_COROUTINE_NUM];
    stack[0] = (uint64_t)coroutine_stack_one;
    stack[1] = (uint64_t)coroutine_stack_two;
    stack[2] = (uint64_t)coroutine_stack_three;
    stack[3] = (uint64_t)coroutine_stack_four;

    // Init coroutine pool
    co_init(stack, 4);
    
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
    // Remove it when the server side can correctly queue the notification
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
                
                co_set_args(request_pool[id].handle, (void* )(status));
                co_wakeup(request_pool[id].handle);
            }
            break;
        }
        default:
            #ifdef FS_DEBUG_PRINT
            sddf_printf("Unknown channel:%d\n", ch);
            #endif
            return;
    }

    // This variable track if there are new requests being popped from request queue and pushed into the couroutine pool or not
    bool new_request_popped = true;
    // Get the number of elements in the queue is costly so we have a flag defined here to only get the number when needed
    bool queue_size_init = false;
    uint64_t command_queue_size;
    uint64_t completion_queue_size;

    uint32_t fs_request_dequeued = 0;
    uint32_t fs_response_enqueued = 0;
    /**
      I assume this big while loop is the confusing and critical part for dispatching coroutines and send back the results.
    **/
    while (new_request_popped) {
        // Performance issue here, should check if the reason being wake up is from notification from the blk device driver
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
        new_request_popped = false;
        for (int32_t i = 1; i < WORKER_COROUTINE_NUM; i++) {
            if (co_check_if_finished(request_pool[i].handle) && request_pool[i].stat == INUSE) {
                fill_client_response(fs_queue_idx_empty(fatfs_completion_queue, fs_response_enqueued), &(request_pool[i]));
                fs_response_enqueued++;
                #ifdef FS_DEBUG_PRINT
                sddf_printf("FS enqueue response:status: %lu\n", request_pool[i].shared_data.status);
                #endif
                request_pool[i].stat= FREE;
            }
        }

        /*
          This should pop the request from the command_queue to the FiberPool to execute, if no new request is 
          popped, we should exit the whole while loop.
        */
        while (true) {
            co_handle_t index;
            // If there is space and we do not know the size of the queue, get it now
            if (queue_size_init == false && co_havefreeslot(&index)) {
                command_queue_size = fs_queue_size_consumer(fatfs_command_queue);
                completion_queue_size = fs_queue_size_producer(fatfs_completion_queue);
                queue_size_init = true;
            }

            // We only dequeue the request if there is a free slot in the coroutine pool
            if (!co_havefreeslot(&index) || command_queue_size == 0
                  || completion_queue_size == FS_QUEUE_CAPACITY) {
               break;
            }

            // For invalid request, dequeue but do not process
            if (fs_queue_idx_filled(fatfs_command_queue, fs_request_dequeued)->cmd.type >= FS_NUM_COMMANDS) {
                #ifdef FS_DEBUG_PRINT
                sddf_printf("Wrong CMD type: %lu\n", fs_queue_idx_filled(fatfs_command_queue, fs_request_dequeued)->cmd.type);
                #endif
                fs_request_dequeued++;
                command_queue_size--;
                continue;
            }

            // Get request from the head of the queue
            setup_request(index, fs_queue_idx_filled(fatfs_command_queue, fs_request_dequeued));
            fs_request_dequeued++;
            #ifdef FS_DEBUG_PRINT
            sddf_printf("FS dequeue request:CMD type: %lu\n", request_pool[index].cmd);
            #endif
            
            request_pool[index].stat = INUSE;
            new_request_popped = true;
            // Dequeue one request from command queue and reserve a space in completion queue
            command_queue_size--;
            completion_queue_size++;
        }
    }
    // Publish the changes to the fs_queue, If there are replies to client or server, reply back here
    if (fs_request_dequeued) {
        fs_queue_publish_consumption(fatfs_command_queue, fs_request_dequeued);
    }
    if (fs_response_enqueued) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("FS notify client\n");
        #endif
        fs_queue_publish_production(fatfs_completion_queue, fs_response_enqueued);
        microkit_notify(Client_CH);
    }
    if (blk_request_pushed) {
        #ifdef FS_DEBUG_PRINT
        sddf_printf("FS notify driver\n");
        #endif
        microkit_notify(Server_CH);
        blk_request_pushed = false;
    }
}