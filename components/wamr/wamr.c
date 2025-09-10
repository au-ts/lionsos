#include <microkit.h>
#include <libmicrokitco.h>

#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/i2c/config.h>
#include <sddf/i2c/queue.h>
#include <sddf/timer/config.h>
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
#include <lions/util.h>

#include <lions/fs/helpers.h>

#include <stdlib.h>

#include "platform_common.h"
#include "wasm_export.h"

#include "wamr.h"

#include "app_wasm.h"

#define TIMEOUT (1 * NS_IN_MS)

static char wamr_stack[WAMR_STACK_SIZE];
static co_control_t co_controller_mem;
static void blocking_wait(microkit_channel ch) { microkit_cothread_wait_on_channel(ch); }

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;
__attribute__((__section__(".lib_sddf_lwip_config"))) lib_sddf_lwip_config_t lib_sddf_lwip_config;
__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;
__attribute__((__section__(".i2c_client_config"))) i2c_client_config_t i2c_config;
__attribute__((__section__(".fw_webserver_config"))) fw_webserver_config_t fw_config;

bool net_enabled;
bool i2c_enabled;
bool fs_enabled;
bool serial_rx_enabled;
bool firewall_enabled;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

serial_queue_handle_t serial_rx_queue_handle;
serial_queue_handle_t serial_tx_queue_handle;

net_queue_handle_t net_rx_handle;
net_queue_handle_t net_tx_handle;

fw_queue_t rx_active;
fw_queue_t rx_free;
fw_queue_t arp_req_queue;
fw_queue_t arp_resp_queue;

i2c_queue_handle_t i2c_queue_handle;

static const void *app_instance_main(wasm_module_inst_t module_inst) {
    const char *exception;

    wasm_application_execute_main(module_inst, 0, NULL);
    exception = wasm_runtime_get_exception(module_inst);
    return exception;
}

static void netif_status_callback(char *ip_addr) {
    printf("%s: %s:%d:%s: DHCP request finished, IP address for %s is: %s\r\n",
           microkit_name, __FILE__, __LINE__, __func__, microkit_name, ip_addr);
}

static void wamr_main() {
    char *ip_string_arg = NULL;
    sddf_lwip_tx_intercept_condition_fn fw_intercept_arp = NULL;
    sddf_lwip_tx_handle_intercept_fn fw_handle_arp = NULL;
    sddf_lwip_netif_status_callback_fn netif_callback = netif_status_callback;

    printf("WAMR | Starting WAMR...\n");

    if (net_enabled) {
        printf("WAMR | Initialising network...\n");
        if (net_config.rx.num_buffers) {
            net_queue_init(&net_rx_handle, net_config.rx.free_queue.vaddr, net_config.rx.active_queue.vaddr,
                           net_config.rx.num_buffers);
        }
        if (net_config.tx.num_buffers) {
            net_queue_init(&net_tx_handle, net_config.tx.free_queue.vaddr, net_config.tx.active_queue.vaddr,
                           net_config.tx.num_buffers);
            net_buffers_init(&net_tx_handle, 0);
        }

        sddf_lwip_init(&lib_sddf_lwip_config, &net_config, &timer_config, net_rx_handle, net_tx_handle, ip_string_arg,
                       printf, netif_callback, NULL, fw_intercept_arp, fw_handle_arp);

        sddf_lwip_maybe_notify();
    }

    if (fs_enabled) {
        printf("WAMR | Initialising filesystem...\n");
        fs_cmpl_t completion;
        int err = fs_command_blocking(&completion, (fs_cmd_t){.type = FS_CMD_INITIALISE});
        if (err || completion.status != FS_STATUS_SUCCESS) {
            printf("WAMR|ERROR: Failed to mount\n");
            return;
        }
    }

    char error_buf[128] = {0};

    printf("WAMR | Initialising runtime...\n");
    if (!wasm_runtime_init()) {
        printf("Init runtime environment failed.\n");
        return;
    }

    wasm_module_t wasm_module = NULL;
    printf("WAMR | Loading module...\n");
    if (!(wasm_module = wasm_runtime_load(app_wasm, app_wasm_len, error_buf, sizeof(error_buf)))) {
        printf("%s\n", error_buf);
        return;
    }

    struct InstantiationArgs2 *inst_args;
    printf("WAMR | Creating instantiate args...\n");
    if (!wasm_runtime_instantiation_args_create(&inst_args)) {
        printf("failed to create instantiate args\n");
        return;
    }

    uint32 stack_size = 8192;
    uint32 heap_size = 4096;
    printf("WAMR | Setting stack and heap size for instantiate args...\n");
    wasm_runtime_instantiation_args_set_default_stack_size(inst_args, stack_size);
    wasm_runtime_instantiation_args_set_host_managed_heap_size(inst_args, heap_size);

    // const char *dir = "/";
    // wasm_runtime_set_wasi_args(wasm_module, &dir, 1, NULL, 0, NULL, 0, NULL, 0);

    wasm_module_inst_t wasm_module_inst = NULL;
    printf("WAMR | Instantiating module...\n");
    if (!(wasm_module_inst = wasm_runtime_instantiate_ex2(wasm_module, inst_args, error_buf, sizeof(error_buf)))) {
        printf("%s\n", error_buf);
        return;
    }

    printf("WAMR | Destroying instantiate args...\n");
    wasm_runtime_instantiation_args_destroy(inst_args);

    const char *exception = NULL;
    printf("WAMR | Running module...\n");
    if ((exception = app_instance_main(wasm_module_inst))) {
        printf("%s\n", exception);
    }
}

void notified(microkit_channel ch) {
    printf("WAMR | Notified on channel %d\n", ch);
    if (net_enabled) {
        sddf_lwip_process_rx();
        sddf_lwip_process_timeout();
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

    firewall_enabled = (fw_config.rx_active.queue.vaddr != NULL);

    if (fs_enabled) {
        fs_set_blocking_wait(blocking_wait);
        fs_command_queue = fs_config.server.command_queue.vaddr;
        fs_completion_queue = fs_config.server.completion_queue.vaddr;
        fs_share = fs_config.server.share.vaddr;
    }

    i2c_enabled = i2c_config_check_magic(&i2c_config);
    if (i2c_enabled) {
        i2c_queue_handle = i2c_queue_init(i2c_config.virt.req_queue.vaddr, i2c_config.virt.resp_queue.vaddr);
    }

    stack_ptrs_arg_array_t costacks = {(uintptr_t)wamr_stack};
    microkit_cothread_init(&co_controller_mem, WAMR_STACK_SIZE, costacks);

    libc_init();

    if (microkit_cothread_spawn(wamr_main, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        printf("WAMR|ERROR: Cannot initialise WAMR cothread\n");
        assert(false);
    };

    microkit_cothread_yield();
}
