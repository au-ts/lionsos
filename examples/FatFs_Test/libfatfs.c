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

void* mymalloc(uint64_t buffer_size) {
    if (cur_mem + buffer_size > memory + size) {
        return (void*)0;
    }
    void *ret = (void *)cur_mem;
    cur_mem += buffer_size;
    return ret;
}

void mymalloc_init() {
    cur_mem = memory;
}

/*
FRESULT fat_mount (FATFS* fs, const TCHAR* path, BYTE opt) {
    union sddf_fs_message request, response;
    mymalloc_init();
    FATFS* fs_temp = mymalloc(sizeof(FATFS));
    // Do I need to add one here?
    TCHAR* temp_path = mymalloc(100);
    strcpy(temp_path, path);
    struct f_mount_s* temp = mymalloc(sizeof(struct f_mount_s));
    temp->fs = fs_temp;
    temp->opt = opt;
    temp->path = temp_path;
    request.command.request_id = 1;
    request.command.cmd_type = SDDF_FS_CMD_MOUNT;
    memcpy(request.command.args, temp, sizeof(struct f_mount_s));
    sddf_fs_queue_push(request_queue, request);
    microkit_notify(FS_Channel);
    Fiber_switch(main_thread);
    sddf_fs_queue_pop(response_queue, &response);
    memcpy(fs, fs_temp, sizeof(FATFS));
    return response.completion.status;
}

FRESULT fat_f_open (FIL* fp, const TCHAR* path, BYTE mode) {
    union sddf_fs_message request, response;
    mymalloc_init();
    FIL *fp_temp = mymalloc(sizeof(FIL));
    TCHAR* temp_path = mymalloc(100);
    strcpy(temp_path, path);
    struct f_open_s* temp = mymalloc(sizeof(struct f_open_s));
    temp->path = temp_path;
    temp->fp = fp_temp;
    temp->mode = mode;
    request.command.request_id = 1;
    request.command.cmd_type = SDDF_FS_CMD_OPEN;
    memcpy(request.command.args, temp, sizeof(struct f_open_s));
    sddf_fs_queue_push(request_queue, request);
    microkit_notify(FS_Channel);
    Fiber_switch(main_thread);
    sddf_fs_queue_pop(response_queue, &response);
    memcpy(fp, fp_temp, sizeof(FIL));
    return response.completion.status;
}
*/


// Zero copy version of the lib, not being used due to issues in driver vm
FRESULT fat_mount (FATFS* fs, const TCHAR* path, BYTE opt) {
    union sddf_fs_message request, response;
    struct f_mount_s* temp = (void*)request.command.args;
    temp->fs = fs;
    temp->opt = opt;
    temp->path = path;
    request.command.request_id = 1;
    request.command.cmd_type = SDDF_FS_CMD_MOUNT;
    sddf_fs_queue_push(request_queue, request);
    microkit_notify(FS_Channel);
    Fiber_switch(main_thread);
    sddf_fs_queue_pop(response_queue, &response);
    return response.completion.status;
}

FRESULT fat_unmount(const TCHAR* path) {
    return fat_mount(0, path, 0);
}

FRESULT fat_f_open (FIL* fp, const TCHAR* path, BYTE mode) {
    union sddf_fs_message request, response;
    struct f_open_s* temp = (void*)request.command.args;
    temp->path = path;
    temp->fp = fp;
    temp->mode = mode;
    request.command.request_id = 1;
    request.command.cmd_type = SDDF_FS_CMD_OPEN;
    sddf_fs_queue_push(request_queue, request);
    microkit_notify(FS_Channel);
    Fiber_switch(main_thread);
    sddf_fs_queue_pop(response_queue, &response);
    return response.completion.status;
}

FRESULT fat_f_pread (FIL* fp, void* buff, FSIZE_t ofs, UINT btr, UINT* br) {
    union sddf_fs_message request, response;
    struct f_pread_s* temp = (void*)request.command.args;
    temp->fp = fp;
    temp->buff = buff;
    temp->ofs = ofs;
    temp->btr = btr;
    temp->br = br;
    request.command.request_id = 1;
    request.command.cmd_type = SDDF_FS_CMD_PREAD;
    sddf_fs_queue_push(request_queue, request);
    microkit_notify(FS_Channel);
    Fiber_switch(main_thread);
    sddf_fs_queue_pop(response_queue, &response);
    return response.completion.status;
}

FRESULT fat_f_pwrite (FIL* fp, void* buff, FSIZE_t ofs, UINT btw, UINT* bw) {
    union sddf_fs_message request, response;
    struct f_pwrite_s* temp = (void*)request.command.args;
    temp->fp = fp;
    temp->buff = buff;
    temp->ofs = ofs;
    temp->btw = btw;
    temp->bw = bw;
    request.command.request_id = 1;
    request.command.cmd_type = SDDF_FS_CMD_PWRITE;
    sddf_fs_queue_push(request_queue, request);
    microkit_notify(FS_Channel);
    Fiber_switch(main_thread);
    sddf_fs_queue_pop(response_queue, &response);
    return response.completion.status;
}

FRESULT fat_f_close (FIL* fp) {
    union sddf_fs_message request, response;
    struct f_close_s* temp = (void*)request.command.args;
    temp->fp = fp;
    request.command.request_id = 1;
    request.command.cmd_type = SDDF_FS_CMD_CLOSE;
    sddf_fs_queue_push(request_queue, request);
    microkit_notify(FS_Channel);
    Fiber_switch(main_thread);
    sddf_fs_queue_pop(response_queue, &response);
    return response.completion.status;
}