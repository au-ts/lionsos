#include <microkit.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
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
#include <time.h>
#include <sddf/benchmark/config.h>
#include <sddf/benchmark/bench.h>

#include <dirent.h>

#include <519.h>
#include <minor_pf.h>
__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;
__attribute__((__section__(".benchmark_client_config"))) benchmark_client_config_t benchmark_config;
#define WORKER_STACK_SIZE (64 * 1024)

static char worker_stack[WORKER_STACK_SIZE];
static co_control_t co_controller_mem;

static void blocking_wait(microkit_channel ch) { microkit_cothread_wait_on_channel(ch); }

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

serial_queue_handle_t serial_rx_queue_handle;
serial_queue_handle_t serial_tx_queue_handle;
static uint64_t timespec_to_ns(struct timespec ts)
{
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
void bench_main(void) {
    // printf("this is printed bm\n");
    fs_cmpl_t completion;
    int err = fs_command_blocking(&completion, (fs_cmd_t){ .type = FS_CMD_INITIALISE });
    if (err || completion.status != FS_STATUS_SUCCESS) {
        printf("BENCH|ERROR: Failed to mount, %d, %d\n", err, completion.status);
        return;
    }
    char *argv[] = {
        "lbm",
        "1", 
        "reference.dat", 
        "0", 
        "1", 
        "100_100_130_cf_a.of"
    };
    struct timespec start_ts;
    struct timespec end_ts;
    sddf_notify(benchmark_config.start_ch);
    clock_gettime(CLOCK_MONOTONIC, &start_ts);
    // int rc = fiveonenine(6, argv);
    int rc = minor_pf();
    clock_gettime(CLOCK_MONOTONIC, &end_ts);
    sddf_notify(benchmark_config.stop_ch);

    printf("BENCH|SPEC CPU finished rc=%d\n", rc);
    uint64_t start_ns = timespec_to_ns(start_ts);
    uint64_t end_ns = timespec_to_ns(end_ts);
    uint64_t elapsed_ns = end_ns - start_ns;
    printf("elapsed ns: %llu\n", (unsigned long long)elapsed_ns);
    printf("elapsed us: %llu\n", (unsigned long long)(elapsed_ns / 1000));
    printf("elapsed ms: %llu\n", (unsigned long long)(elapsed_ns / 1000000));
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

    libc_init(NULL, (void *) 0x8000000000, 0x20000000);

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
    fs_process_completions(NULL);
    microkit_cothread_recv_ntfn(ch);
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    // not required.
    return seL4_False;
}

#define NUMMAPS 256
#define PAGE_SIZE 4096