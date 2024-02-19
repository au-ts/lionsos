#include "../../fs/fat/FiberPool/FiberFlow/FiberFlow.h"
#include "../../fs/fat/AsyncFATFs.h"
#include "../../fs/fat/libfssharedqueue/fs_shared_queue.h"
#include "../../vmm/src/util/printf.h"
#include <string.h>

static uint8_t Registerpart[AArch64_RegsterPart];

Fiber_t main_thread = (void *)Registerpart;
Fiber_t event_thread;
void* Coroutine_STACK;
#define Coroutine_STACKSIZE 0x40000
struct sddf_fs_queue *request_queue, *response_queue;
union sddf_fs_message request, response;

void* freememory;

void send_f_mount(
  FATFS*       fs,    /* [IN] Filesystem object */
  const TCHAR* path,  /* [IN] Logical drive number */
  BYTE         opt    /* [IN] Initialization option */
) {
    printf("Start mounting file system!\n");
    struct f_mount_s mount_s;
    void* free = freememory;
    FATFS* temp_fs = free;
    free += sizeof(FATFS);
    mount_s.path = free;
    memcpy(free, path, 10);
    free += 10;
    mount_s.fs = temp_fs;
    mount_s.opt = opt;
    request.command.request_id = 1;
    request.command.cmd_type = SDDF_FS_CMD_MOUNT;
    memcpy(request.command.args, &mount_s, sizeof(mount_s));
    sddf_fs_queue_push(request_queue, request);
    Fiber_switch(main_thread);
    sddf_fs_queue_pop(response_queue, &response);
    printf("Fat file system mounting result: %d\n", response.completion.status);
    Fiber_switch(main_thread);
}

void test() {
    FATFS *fs = freememory;
    freememory += sizeof(FATFS);
    struct f_mount_s mount_s;
    mount_s.fs = fs;
    TCHAR* DriverPath = freememory;
    freememory+= sizeof("00");
    DriverPath = "";
    mount_s.path = DriverPath;
    mount_s.opt = 1;
}

void init(void) {
    Fiber_init(main_thread);
    Fiber_create(event_thread, Coroutine_STACKSIZE, test);
    Fiber_switch(event_thread);
}

void notified(int ch) {
    Fiber_switch(event_thread);
}

