#include "AsyncFATFs.h"
#include "ff15/source/ff.h"
#include "ff15/source/diskio.h"
#include "../../../dep/sddf/include/sddf/blk/queue.h"
#include "FiberPool/FiberPool.h"
#include "../../../include/lions/fs/protocol.h"
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <microkit.h>

#define RequestPool_Size 128

#define Coroutine_STACKSIZE 0x40000

#define Client_CH 1
#define Server_CH 2

#define SDDF_ARGS_SIZE 6

#ifdef FS_DEBUG_PRINT
#include "../../vmm/src/util/printf.h"
#endif

blk_queue_handle_t blk_queue_handle_memory;
blk_queue_handle_t *blk_queue_handle = &blk_queue_handle_memory;

struct sddf_fs_queue *Fatfs_command_queue;
struct sddf_fs_queue *Fatfs_completion_queue;

blk_req_queue_t *request;
blk_resp_queue_t *response;

// Compromised code here
// Config pointed to the SDDF_blk config
blk_storage_info_t *config;

void* Coroutine_STACK_ONE;
void* Coroutine_STACK_TWO;
void* Coroutine_STACK_THREE;
void* Coroutine_STACK_FOUR;

// File system metadata region
void* fs_metadata;

// Flag for checking if the fat is mounted or not
bool fat_mounted = false;

typedef enum {
    FREE,
    INUSE
} space_status;

typedef struct FS_request{
    /* Client side cmd info */
    uint64_t cmd;
    // This args array have 9 elements
    // The first 6 are for 6 input args in file system protocol
    // The last 3 are for returning operation status and return data
    uint64_t args[9];
    uint64_t request_id;
    /* FiberPool metadata */
    co_handle handle;
    /* self metadata */
    space_status stat;
} FSRequest;

/*
 The function array presents the function that 
 mapped by the enum
 typedef enum {
    SDDF_FS_CMD_OPEN,
    SDDF_FS_CMD_CLOSE,
    SDDF_FS_CMD_STAT,
    SDDF_FS_CMD_PREAD,
    SDDF_FS_CMD_PWRITE,
    SDDF_FS_CMD_RENAME,
    SDDF_FS_CMD_UNLINK,
    SDDF_FS_CMD_MKDIR,
    SDDF_FS_CMD_RMDIR,
    SDDF_FS_CMD_OPENDIR,
    SDDF_FS_CMD_CLOSEDIR,
    SDDF_FS_CMD_FSYNC,
    SDDF_FS_CMD_READDIR,
    SDDF_FS_CMD_SEEKDIR,
    SDDF_FS_CMD_TELLDIR,
    SDDF_FS_CMD_REWINDDIR,
} FS_CMD;
*/

// This operations list must be consistent with the file system protocol enum
void (*operation_functions[])() = {
    fat_open,
    fat_close,
    fat_stat,
    fat_pread,
    fat_pwrite,
    fat_rename,
    fat_unlink,
    fat_mkdir,
    fat_rmdir,
    fat_opendir,
    fat_closedir,
    fat_sync,
    fat_seekdir,
    fat_readdir,
    fat_rewinddir,
};

static FSRequest RequestPool[MAX_COROUTINE_NUM];

void Fill_Client_Response(union sddf_fs_message* message, const FSRequest* Finished_Request) {
    message->completion.status = Finished_Request->args[Status_bit];
    message->completion.data[0] = Finished_Request->args[First_data_bit];
    message->completion.data[1] = Finished_Request->args[Second_data_bit];
    return;
}

// Setting up the request in the requestpool and push the request to the FiberPool
void SetUp_request(int32_t index, union sddf_fs_message message) {
    RequestPool[index].request_id = message.command.request_id;
    RequestPool[index].cmd = message.command.cmd_type;
    memcpy(RequestPool[index].args, message.command.args, SDDF_ARGS_SIZE * sizeof(uint64_t));
    FiberPool_push(operation_functions[RequestPool[index].cmd], RequestPool[index].args, 
      2, &(RequestPool[index].handle));
    return;
}

void init(void) {
    // Init the block device queue
    // Have to make sure who initialize this SDDF queue
    blk_queue_init(blk_queue_handle, request, response, BLK_QUEUE_SIZE);
    /*
       This part of the code is for setting up the FiberPool(Coroutine pool) by
       assign stacks and size of the stack to the pool
    */
    struct stack_mem stackmem[4];
    stackmem[0].memory = Coroutine_STACK_ONE;
    stackmem[0].size = Coroutine_STACKSIZE;
    stackmem[1].memory = Coroutine_STACK_TWO;
    stackmem[1].size = Coroutine_STACKSIZE;
    stackmem[2].memory = Coroutine_STACK_THREE;
    stackmem[2].size = Coroutine_STACKSIZE;
    stackmem[3].memory = Coroutine_STACK_FOUR;
    stackmem[3].size = Coroutine_STACKSIZE;
    FiberPool_init(stackmem, 4, 1);
    
    // Init file system metadata
    init_metadata(fs_metadata);

    // In init process, let the coroutine execuate a mount command
    // Compromised code here, mount file system itself
    // This part can be a little bit ugly as I have not thought of mount the file system itself 
    // at the beginning of design, I assume the mount come from a request from the client
    // But it turns out the file system should mount itself as the protocol suggests
    RequestPool[1].request_id = 1;
    RequestPool[1].stat = INUSE;
    FiberPool_push(fat_mount, RequestPool[1].args, 2, &(RequestPool[1].handle));
}

// The notified function requires careful management of the state of the file system
/*
  The filesystems should be blockwait for new message if and only if all of working
  coroutines are either free(no tasks assigned to them, no pending replies) or blocked in diskio.
  If the filesystem is blocked here and any working coroutines are free, then the Fatfs_command_queue 
  must also be empty.
*/
void notified(microkit_channel ch) {
    //printf("FS IRQ received: %d\n", ch);
    union sddf_fs_message message;
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
                blk_dequeue_resp(blk_queue_handle, &status, &success_count, &id);
                
                #ifdef FS_DEBUG_PRINT
                printf_("blk_dequeue_resp: status: %d addr: 0x%lx count: %d success_count: %d ID: %d\n", status, addr, count, success_count, id);
                #endif

                FiberPool_SetArgs(RequestPool[id].handle, (void* )(status));
                Fiber_wake(RequestPool[id].handle);
            }
            break;
        }
        default:
            return;
    }

    // This variable track if the fs should send back reply to the file system client
    bool Client_have_replies = false;
    // Flag for determine if there are blk_requests pushed by the file system
    // It is used to determine whether to notify the blk device driver
    bool blk_request_pushed = false;
    
    // Compromised code here, mount file system itself
    // This part can be a little bit ugly as I have not thought of mount the file system itself 
    // at the beginning of design, I assume the mount come from a request from the client
    // But it turns out the file system should mount itself as the protocol suggests
    if (!fat_mounted) {
        Fiber_yield();
        if (blk_request_pushed == true) {
            microkit_notify(Server_CH);
        }
        if (RequestPool[1].handle == INVALID_COHANDLE && RequestPool[1].stat == INUSE) {
            RequestPool[1].stat = FREE;
            fat_mounted = true;
        }
    }

    int32_t index;
    int32_t i;

    // This variable track if there are new requests being popped from request queue and pushed into the couroutine pool or not
    bool New_request_popped = fat_mounted;
    /**
      I assume this big while loop is the confusing and critical part for dispatching coroutines and send back the results.
    **/
    while (New_request_popped) {
        // Performance bug here, should check if the reason being wake up is from notification from the blk device driver
        // Then decide to yield() or not
        // And should only send back notification to blk device driver if at least one coroutine is block waiting
        Fiber_yield();
        
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
            if (RequestPool[i].handle == INVALID_COHANDLE && RequestPool[i].stat == INUSE) {
                message.completion.request_id = RequestPool[i].request_id;
                Fill_Client_Response(&message, &(RequestPool[i]));
                sddf_fs_queue_push(Fatfs_completion_queue, message);
                RequestPool[i].stat= FREE;
                Client_have_replies = true;
            }
        }
        /*
          This should pop the request from the command_queue to the FiberPool to execute, if no new request is 
          popped, we should exit the whole while loop.
        */
        while (true) {
            index = FiberPool_FindFree();
            if (index == INVALID_COHANDLE || sddf_fs_queue_empty(Fatfs_command_queue) 
                  || sddf_fs_queue_empty(Fatfs_completion_queue)) {
               break;
            }
            sddf_fs_queue_pop(Fatfs_command_queue, &message);
            SetUp_request(index, message);
            RequestPool[index].stat = INUSE;
            New_request_popped = true;
        }
    }
    // If there are replies to client or server, reply back here
    if (Client_have_replies == true) {
        microkit_notify(Client_CH);
    }
    if (blk_request_pushed == true) {
        microkit_notify(Server_CH);
    }
}