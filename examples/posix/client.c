#include <microkit.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/timer/config.h>
#include <lions/fs/helpers.h>
#include <lions/fs/config.h>
#include <lions/fs/protocol.h>
#include <lions/posix/posix.h>
#include <libmicrokitco.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

serial_queue_handle_t serial_tx_queue_handle;
serial_queue_handle_t serial_rx_queue_handle;

#define LIBC_COTHREAD_STACK_SIZE 0x10000

static char libc_cothread_stack[LIBC_COTHREAD_STACK_SIZE];
static co_control_t co_controller_mem;

static void blocking_wait(microkit_channel ch) { microkit_cothread_wait_on_channel(ch); }

void notified(microkit_channel ch) {
    fs_process_completions(NULL);
    microkit_cothread_recv_ntfn(ch);
}

void cont() {
    fs_cmpl_t completion;
    int err = fs_command_blocking(&completion, (fs_cmd_t){ .type = FS_CMD_INITIALISE });
    if (err || completion.status != FS_STATUS_SUCCESS) {
        printf("WAMR|ERROR: Failed to mount\n");
        return;
    }

    printf("CLIENT|INFO: fs init\n");

    FILE *file = fopen("hello.txt", "w+");
    assert(file);

    // printf("CLIENT|INFO: opened on fd %d\n", fd);

    char *hello = "hello there\n";
    int size = fwrite(hello, strlen(hello), 1, file);

    printf("CLIENT|INFO: writing %d bytes\n", size);

    fflush(file);

    char buf[10];
    buf[9] = '\0';

    fseek(file, 0, SEEK_SET);

    size_t bytes_read = fread(buf, 9, 1, file);

    printf("CLIENT|INFO: bytes_read: %d, buf: %s\n", bytes_read, buf);

    printf("CLIENT|INFO: doing fseek\n");
    err = fseek(file, 100, SEEK_CUR);
    assert(!err);
    fwrite(hello, strlen(hello), 1, file);
    fflush(file);

    int closed = fclose(file);
    assert(closed == 0);
}

void init() {
    assert(serial_config_check_magic(&serial_config));
    assert(timer_config_check_magic(&timer_config));
    // bool fs_enabled = fs_config_check_magic(&fs_config);
    bool serial_rx_enabled = (serial_config.rx.queue.vaddr != NULL);

    if (serial_rx_enabled) {
        serial_queue_init(&serial_rx_queue_handle, serial_config.rx.queue.vaddr, serial_config.rx.data.size, serial_config.rx.data.vaddr);
    }
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size, serial_config.tx.data.vaddr);

    fs_set_blocking_wait(blocking_wait);
    fs_command_queue = fs_config.server.command_queue.vaddr;
    fs_completion_queue = fs_config.server.completion_queue.vaddr;
    fs_share = fs_config.server.share.vaddr;

    stack_ptrs_arg_array_t costacks = { (uintptr_t) libc_cothread_stack };
    microkit_cothread_init(&co_controller_mem, LIBC_COTHREAD_STACK_SIZE, costacks);

    libc_init();

    if (microkit_cothread_spawn(cont, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        printf("CLIENT|ERROR: Cannot initialise cothread\n");
        assert(false);
    };

    microkit_cothread_yield();
}
