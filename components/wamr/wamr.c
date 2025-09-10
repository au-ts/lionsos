#include <microkit.h>
#include <libmicrokitco.h>

#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/i2c/config.h>
#include <sddf/i2c/queue.h>
#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <sddf/timer/protocol.h>
#include <sddf/network/config.h>
#include <sddf/network/queue.h>
#include <sddf/network/lib_sddf_lwip.h>
#include <lions/fs/config.h>
#include <lions/fs/protocol.h>
#include <lions/firewall/arp.h>
#include <lions/firewall/common.h>
#include <lions/firewall/config.h>
#include <lions/firewall/queue.h>
#include <lions/posix/posix.h>
#include <lions/sock/tcp.h>
#include <lions/util.h>

#include <lions/fs/helpers.h>

#include <stdlib.h>

#include "wasm_export.h"

#include "wamr.h"

#define TIMEOUT (1 * NS_IN_MS)

static char wamr_stack[WAMR_STACK_SIZE];
static co_control_t co_controller_mem;
static void blocking_wait(microkit_channel ch) { microkit_cothread_wait_on_channel(ch); }

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;
__attribute__((__section__(".lib_sddf_lwip_config"))) lib_sddf_lwip_config_t lib_sddf_lwip_config;
__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;

// access to embedded app.wasm
extern const unsigned char _binary_app_wasm_start[];
extern const unsigned char _binary_app_wasm_end[];

bool net_enabled;
bool fs_enabled;
bool serial_rx_enabled;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

serial_queue_handle_t serial_rx_queue_handle;
serial_queue_handle_t serial_tx_queue_handle;

static void wamr_main() {
    libc_init();
    printf("WAMR | Starting WAMR...\n");

    if (net_enabled) {
        printf("WAMR | Initialising network...\n");
        tcp_init_0();
    }

    if (fs_enabled) {
        printf("WAMR | Initialising filesystem...\n");
        fs_cmpl_t completion;
        int err = fs_command_blocking(&completion, (fs_cmd_t) { .type = FS_CMD_INITIALISE });
        if (err || completion.status != FS_STATUS_SUCCESS) {
            printf("WAMR|ERROR: Failed to mount\n");
            return;
        }
    }

    char error_buf[128] = { 0 };

    printf("WAMR | Initialising runtime...\n");
    if (!wasm_runtime_init()) {
        printf("Init runtime environment failed.\n");
        return;
    }

    wasm_module_t wasm_module = NULL;
    printf("WAMR | Loading module...\n");
    if (!(wasm_module = wasm_runtime_load((uint8_t *)_binary_app_wasm_start,
                                          (size_t)(_binary_app_wasm_end - _binary_app_wasm_start), error_buf,
                                          sizeof(error_buf)))) {
        printf("%s\n", error_buf);
        return;
    }

    const char *dir[] = { "/" };
    wasm_runtime_set_wasi_args(wasm_module, dir, 1, NULL, 0, NULL, 0, NULL, 0);

    const char *addr_pool_str[] = { "0.0.0.0/0" };
    wasm_runtime_set_wasi_addr_pool(wasm_module, addr_pool_str, sizeof(addr_pool_str) / sizeof(addr_pool_str[0]));

    wasm_module_inst_t wasm_module_inst = NULL;
    printf("WAMR | Instantiating module...\n");
    if (!(wasm_module_inst = wasm_runtime_instantiate(wasm_module, 8192, 4096, error_buf, sizeof(error_buf)))) {
        printf("%s\n", error_buf);
        return;
    }

    const char *exception = NULL;
    printf("WAMR | Running module...\n");
    wasm_application_execute_main(wasm_module_inst, 0, NULL);
    if ((exception = wasm_runtime_get_exception(wasm_module_inst))) {
        printf("%s\n", exception);
    }
}

void notified(microkit_channel ch) {
    if (ch == timer_config.driver_id) {
        if (net_enabled) {
            sddf_lwip_process_rx();
            sddf_lwip_process_timeout();

            sddf_timer_set_timeout(timer_config.driver_id, TIMEOUT);
        }
    } else if (ch == net_config.rx.id) {
        if (net_enabled) {
            sddf_lwip_process_rx();
        }
    }

    if (fs_enabled) {
        fs_process_completions(NULL);
    }

    microkit_cothread_recv_ntfn(ch);

    if (net_enabled) {
        sddf_lwip_maybe_notify();
    }
}

void init(void) {
    assert(serial_config_check_magic(&serial_config));
    assert(timer_config_check_magic(&timer_config));
    net_enabled = net_config_check_magic(&net_config);
    fs_enabled = fs_config_check_magic(&fs_config);
    serial_rx_enabled = (serial_config.rx.queue.vaddr != NULL);

    if (serial_rx_enabled) {
        serial_queue_init(&serial_rx_queue_handle, serial_config.rx.queue.vaddr, serial_config.rx.data.size,
                          serial_config.rx.data.vaddr);
    }
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size,
                      serial_config.tx.data.vaddr);

    if (fs_enabled) {
        fs_set_blocking_wait(blocking_wait);
        fs_command_queue = fs_config.server.command_queue.vaddr;
        fs_completion_queue = fs_config.server.completion_queue.vaddr;
        fs_share = fs_config.server.share.vaddr;
    }

    stack_ptrs_arg_array_t costacks = { (uintptr_t)wamr_stack };
    microkit_cothread_init(&co_controller_mem, WAMR_STACK_SIZE, costacks);

    if (microkit_cothread_spawn(wamr_main, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        printf("WAMR|ERROR: Cannot initialise WAMR cothread\n");
        assert(false);
    };

    sddf_timer_set_timeout(timer_config.driver_id, TIMEOUT);

    microkit_cothread_yield();
}
