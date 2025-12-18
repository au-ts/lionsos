/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <lions/posix/posix.h>

#include <microkit.h>
#include <libmicrokitco.h>

#include <stdio.h>
#include <stdlib.h>

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
#include <lions/util.h>
#include <lions/fs/helpers.h>

#include <wasm_export.h>

#define TIMEOUT (1 * NS_IN_MS)
#define WAMR_STACK_SIZE (0x100000)

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

net_queue_handle_t net_rx_handle;
net_queue_handle_t net_tx_handle;

extern libc_socket_config_t socket_config;

static void netif_status_callback(char *ip_addr) {
    printf("%s: %s:%d:%s: DHCP request finished, IP address for %s is: %s\r\n", microkit_name, __FILE__, __LINE__,
           __func__, microkit_name, ip_addr);
}

static void wamr_main() {
    libc_init(&socket_config);

    printf("WAMR | Starting WAMR...\n");

    char error_buf[128] = { 0 };

    printf("WAMR | Initialising runtime...");
    if (!wasm_runtime_init()) {
        printf("Init runtime environment failed.\n");
        return;
    }
    printf("done\n");

    wasm_module_t wasm_module = NULL;
    printf("WAMR | Loading module...");
    if (!(wasm_module = wasm_runtime_load((uint8_t *)_binary_app_wasm_start,
                                          (size_t)(_binary_app_wasm_end - _binary_app_wasm_start), error_buf,
                                          sizeof(error_buf)))) {
        printf("\n%s\n", error_buf);
        return;
    }
    printf("done\n");

    if (fs_enabled) {
        printf("WAMR | Initialising filesystem...");
        fs_set_blocking_wait(blocking_wait);
        fs_command_queue = fs_config.server.command_queue.vaddr;
        fs_completion_queue = fs_config.server.completion_queue.vaddr;
        fs_share = fs_config.server.share.vaddr;

        fs_cmpl_t completion;
        int err = fs_command_blocking(&completion, (fs_cmd_t) { .type = FS_CMD_INITIALISE });
        if (err || completion.status != FS_STATUS_SUCCESS) {
            printf("\nWAMR|ERROR: Failed to mount\n");
            return;
        }

        const char *preopen_dirs[] = { "/" };
        wasm_runtime_set_wasi_args(wasm_module, preopen_dirs, sizeof(preopen_dirs) / sizeof(preopen_dirs[0]), NULL, 0,
                                   NULL, 0, NULL, 0);

        printf("done\n");
    }

    if (net_enabled) {
        printf("WAMR | Initialising network...");
        net_queue_init(&net_rx_handle, net_config.rx.free_queue.vaddr, net_config.rx.active_queue.vaddr,
                       net_config.rx.num_buffers);
        net_queue_init(&net_tx_handle, net_config.tx.free_queue.vaddr, net_config.tx.active_queue.vaddr,
                       net_config.tx.num_buffers);
        net_buffers_init(&net_tx_handle, 0);

        sddf_lwip_init(&lib_sddf_lwip_config, &net_config, &timer_config, net_rx_handle, net_tx_handle, NULL, printf,
                       netif_status_callback, NULL, NULL, NULL);

        sddf_lwip_maybe_notify();

        const char *addr_pool_str[] = { "0.0.0.0/0" };
        wasm_runtime_set_wasi_addr_pool(wasm_module, addr_pool_str, sizeof(addr_pool_str) / sizeof(addr_pool_str[0]));
        printf("done\n");
    }

    wasm_module_inst_t wasm_module_inst = NULL;
    printf("WAMR | Instantiating module...");
    if (!(wasm_module_inst = wasm_runtime_instantiate(wasm_module, 8192, 4096, error_buf, sizeof(error_buf)))) {
        printf("\n%s\n", error_buf);
        return;
    }
    printf("done\n");

    const char *exception = NULL;
    printf("WAMR | Running module...\n");
    wasm_application_execute_main(wasm_module_inst, 0, NULL);
    if ((exception = wasm_runtime_get_exception(wasm_module_inst))) {
        printf("%s\n", exception);
    }
    printf("WAMR | Exiting...\n");
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

    stack_ptrs_arg_array_t costacks = { (uintptr_t)wamr_stack };
    microkit_cothread_init(&co_controller_mem, WAMR_STACK_SIZE, costacks);

    if (microkit_cothread_spawn(wamr_main, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        assert(false);
    };

    sddf_timer_set_timeout(timer_config.driver_id, TIMEOUT);

    microkit_cothread_yield();
}
