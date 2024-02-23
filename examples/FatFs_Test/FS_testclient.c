#include "../../fs/fat/FiberPool/FiberFlow/FiberFlow.h"
#include "../../fs/fat/libfssharedqueue/fs_shared_queue.h"
#include "../../vmm/src/util/printf.h"
#include "libfatfs.h"
#include <stdint.h>
#include <string.h>
#include <microkit.h>

#define FS_Channel 1

static uint8_t Registerpart[AArch64_RegsterPart];

Fiber_t main_thread = (void *)Registerpart;
Fiber_t event_thread;
void* Coroutine_STACK;
#define Coroutine_STACKSIZE 0x40000
struct sddf_fs_queue *request_queue, *response_queue;
union sddf_fs_message request, response;

uintptr_t memory = 0x30600000;
uint64_t size = 0x200000;

void test() {
    FATFS fs;
    const TCHAR* path = "";
    int res;
    res = fat_mount(&fs, path, 1);
    printf("Fat file system mounting result: %d\n", res);
    Fiber_switch(main_thread);
}

void init(void) {
    printf("Init FiberFlow\n");
    sddf_fs_init(request_queue);
    sddf_fs_init(response_queue);
    Fiber_init(main_thread);
    event_thread = Fiber_create(Coroutine_STACK, Coroutine_STACKSIZE, test);
    Fiber_switch(event_thread);
}

void notified(microkit_channel ch) {
    printf("FS client IRQ received: %d\n", ch);
    if (ch == 1) {
        Fiber_switch(event_thread);  
    }
}

