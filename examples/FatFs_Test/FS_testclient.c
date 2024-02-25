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
#define Coroutine_STACKSIZE 0x200000
struct sddf_fs_queue *request_queue, *response_queue;
union sddf_fs_message request, response;

uintptr_t memory = 0x30600000;
uint64_t size = 0x200000;


// For the test to work
// Make sure the blk_config->blocksize in block.c in block driver vm is set to 512 not any other value
// In real block device driver, the value should be dynamically determined by read the sector 0 of the block device
// But now, make sure that value is hardcoded to a proper block size, and adjust other hardcoded value as well
void test() {
    // FATFS fs;
    mymalloc_init();
    FATFS *fs = mymalloc(sizeof(FATFS));
    char *line = mymalloc(100);
    int res;
    strcpy(line, "");
    // File system mounting test
    res = fat_mount(fs, line, 1);
    printf("Fat file system mounting result: %d\n", res);
    // File system opening test
    FIL *fp = mymalloc(sizeof(FIL));
    strcpy(line, "test_file");
    res = fat_f_open(fp, line, FA_CREATE_NEW | FA_WRITE | FA_READ);
    printf("Fat file system open result: %d\n", res);
    strcpy(line, "Hello! This is my AsyncFatfs!");
    uint32_t *wr_num = mymalloc(sizeof(uint32_t));
    res = fat_f_pwrite(fp, line, 0, strlen(line) + 1, wr_num);
    printf("Fat file system write result: %d, number of chars written: %d\n", res, *wr_num);
    memset(line, 0, 100);
    *wr_num = 0;
    res = fat_f_pread(fp, line, 0, 30, wr_num);
    printf("Fat file system read result: %d, number of chars read: %d\n Here is the content from read:\n%s\n", res, *wr_num, line);
    Fiber_switch(main_thread);
}

void init(void) {
    // printf("Init FiberFlow\n");
    sddf_fs_init(request_queue);
    sddf_fs_init(response_queue);
    Fiber_init(main_thread);
    event_thread = Fiber_create(Coroutine_STACK, Coroutine_STACKSIZE, test);
    Fiber_switch(event_thread);
}

void notified(microkit_channel ch) {
    // printf("FS client IRQ received: %d\n", ch);
    if (ch == 1) {
        Fiber_switch(event_thread);
    }
}

// Potential issue in blk driver vm
// When I try to let the coroutine runs on the data_blk_vmm_1
// Driver vm would reset the register part(the last few bytes in the data region)
// to all zero
// GDB output of the issue:
/*
Breakpoint 1, fat_mount (fs=0x53, path=0x3 <error: Cannot access memory at address 0x3>, opt=0 '\000') at libfatfs.c:90
90          Fiber_switch(main_thread);
(gdb) c
Continuing.

Breakpoint 1, fat_mount (fs=0xffffffd000000000, path=0x3 <error: Cannot access memory at address 0x3>, opt=0 '\000') at libfatfs.c:90
90          Fiber_switch(main_thread);
(gdb) c
Continuing.

Breakpoint 1, fat_mount (fs=0x307ffcb0, path=0x307ffc4c "", opt=1 '\001') at libfatfs.c:90
90          Fiber_switch(main_thread);
(gdb) print /x ((uint64_t *)event_thread)[0]
$1 = 0x307fff30
(gdb) print /x ((uint64_t *)event_thread)
$2 = 0x307fff30
(gdb) print /x ((uint64_t *)event_thread)[1]
$3 = 0x200b94
(gdb) print /x  *((uint64_t *)event_thread)
$4 = 0x307fff30
(gdb) (gdb) print /x ((uint64_t *)event_thread)
Undefined command: "".  Try "help".
(gdb) print /x ((uint64_t *)event_thread)
$5 = 0x307fff30
(gdb) print /x ((uint64_t *)event_thread)[0]
$6 = 0x307fff30
(gdb) print /x ((uint64_t *)event_thread)[1]
$7 = 0x200b94
(gdb) print /x ((uint64_t *)event_thread)[2]
$8 = 0x0
(gdb) print /x ((uint64_t *)event_thread)[3]
$9 = 0x0
(gdb) print /x ((uint64_t *)event_thread)[4]
$10 = 0x0
(gdb) b init
Breakpoint 2 at 0x2000b8: file FS_testclient.c, line 41.
(gdb) delete 2
(gdb) b notified
Breakpoint 3 at 0x200120: file FS_testclient.c, line 50.
(gdb) next

Breakpoint 3, notified (ch=2) at FS_testclient.c:50
50          if (ch == 1) {
(gdb) next
55      }
(gdb) next
handler_loop () at src/main.c:88
88      src/main.c: No such file or directory.
(gdb) c
Continuing.

Breakpoint 3, notified (ch=0) at FS_testclient.c:50
50          if (ch == 1) {
(gdb) c
Continuing.

Breakpoint 3, notified (ch=1) at FS_testclient.c:50
50          if (ch == 1) {
(gdb) next
51              printf("I think something is wrong here\n");
(gdb) next
52              Fiber_switch(event_thread);
(gdb) print /x ((uint64_t *)event_thread)[0]
$11 = 0x0
(gdb) print /x ((uint64_t *)event_thread)[1]
$12 = 0x0
(gdb) print /x ((uint64_t *)event_thread)[2]
$13 = 0x0
(gdb) print /x ((uint64_t *)event_thread)[12]
$14 = 0x0
(gdb)
*/