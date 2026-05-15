#include <microkit.h>
#include "types.h"
#include <stdint.h>
#include <stdlib.h>
#include <sddf/util/printf.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/timer/config.h>
#include <sddf/util/cache.h>
#include <lions/fs/helpers.h>
#include <lions/fs/config.h>
#include <lions/fs/protocol.h>
#include <lions/posix/posix.h>
#include <libmicrokitco.h>
__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;

#define WORKER_STACK_SIZE (64 * 1024)

static char worker_stack[WORKER_STACK_SIZE];
static co_control_t co_controller_mem;

static void blocking_wait(microkit_channel ch) { microkit_cothread_wait_on_channel(ch); }

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

serial_queue_handle_t serial_rx_queue_handle;
serial_queue_handle_t serial_tx_queue_handle;

int speccpu_main(int, char **argv) {

}

void bench_main(void *arg) {

    fs_cmpl_t completion;
    int err = fs_command_blocking(&completion, (fs_cmd_t){ .type = FS_CMD_INITIALISE });
    if (err || completion.status != FS_STATUS_SUCCESS) {
        printf("BENCH|ERROR: Failed to mount\n");
        return;
    }

    char *argv[] = {
        "20", 
        "reference.dat", 
        "0", 
        "1", 
        "100_100_130_cf_a.of", 
        "0<&-", 
        ">", 
        "lbm.out 2>>", 
        "lbm.err"
    };

    int rc = speccpu_main(1, argv);

    printf("BENCH|SPEC CPU finished rc=%d\n", rc);

    // make the speccpu thing run here.
}

void init(void)
{
    assert(serial_config_check_magic(&serial_config));
    assert(timer_config_check_magic(&timer_config));
    assert(fs_config_check_magic(&fs_config));

    serial_queue_init(&serial_rx_queue_handle, serial_config.rx.queue.vaddr, serial_config.rx.data.size, serial_config.rx.data.vaddr);
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size, serial_config.tx.data.vaddr);

    fs_command_queue = fs_config.server.command_queue.vaddr;
    fs_completion_queue = fs_config.server.completion_queue.vaddr;
    fs_share = fs_config.server.share.vaddr;
    fs_set_blocking_wait(blocking_wait);

    stack_ptrs_arg_array_t costacks = { (uintptr_t) worker_stack };
    microkit_cothread_init(&co_controller_mem, WORKER_STACK_SIZE, costacks);

    libc_init();

    if (microkit_cothread_spawn(bench_main, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        sddf_printf("init(): ERROR: cannot spawn the doom worker coroutine.\n");
        return;
    }

    sddf_printf("init(): initialisation completed, jumping to worker coroutine.\n");
    microkit_cothread_yield();
}

// NOT USED BELOW:
seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo)
{
    // this is not used
    seL4_MessageInfo_t ret;
    return ret;
}



void notified(microkit_channel ch)
{
    // this may not be required
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    // not required.
    return seL4_False;
}

#define NUMMAPS 256
#define PAGE_SIZE 4096

