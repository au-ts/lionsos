#include "libfatfs.h"
#include <stdint.h>
#include <string.h>
#include "../../fs/fat/FiberPool/FiberFlow/FiberFlow.h"
#include "../../fs/fat/libfssharedqueue/fs_shared_queue.h"
#include <microkit.h>

#define FS_Channel 1

extern uintptr_t memory;

extern uint64_t size;

extern Fiber_t main_thread;

extern struct sddf_fs_queue *request_queue, *response_queue;

uintptr_t cur_mem;

union sddf_fs_message request, response;

void* mymalloc(uint64_t size) {
    if (cur_mem + size > memory + size) {
        return (void*)0;
    }
    void *ret = (void *)cur_mem;
    cur_mem += size;
    return ret;
}

void mymalloc_init() {
    cur_mem = memory;
}

FRESULT fat_mount (FATFS* fs, const TCHAR* path, BYTE opt) {
    mymalloc_init();
    FATFS* fs_temp = mymalloc(sizeof(FATFS));
    // Do I need to add one here?
    TCHAR* temp_path = mymalloc(strlen(path) + 10);
    strcpy(temp_path, path);
    struct f_mount_s* temp = mymalloc(sizeof(struct f_mount_s));
    temp->fs = fs_temp;
    temp->opt = opt;
    temp->path = temp_path;
    request.command.request_id = 1;
    request.command.cmd_type = SDDF_FS_CMD_MOUNT;
    memcpy(request.command.args, &temp, sizeof(struct f_mount_s));
    sddf_fs_queue_push(request_queue, request);
    microkit_notify(FS_Channel);
    Fiber_switch(main_thread);
    sddf_fs_queue_pop(response_queue, &response);
    memcpy(fs, fs_temp, sizeof(struct f_mount_s));
    return response.completion.status;
}

FRESULT fat_f_open (FIL* fp, const TCHAR* path, BYTE mode) {
    
}