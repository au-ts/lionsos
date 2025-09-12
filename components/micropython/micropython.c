/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>
#include <string.h>
#include <stdio.h>
#include "micropython.h"
#include "py/builtin.h"
#include "py/compile.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "py/stackctrl.h"
#include "shared/runtime/gchelper.h"
#include "shared/runtime/pyexec.h"
#include "vfs_fs.h"
#include <extmod/vfs.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/i2c/config.h>
#include <sddf/i2c/queue.h>
#include <sddf/timer/config.h>
#include <sddf/network/config.h>
#include <sddf/network/queue.h>
#include <sddf/network/lib_sddf_lwip.h>
#include <lions/firewall/arp.h>
#include <lions/fs/config.h>
#include <lions/firewall/common.h>
#include <lions/firewall/config.h>
#include <lions/firewall/queue.h>
#include <lions/posix/posix.h>
#include "mpconfigport.h"
#include "mphalport.h"
#include "mpfirewallport.h"
#include "fs_helpers.h"

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;
__attribute__((__section__(".lib_sddf_lwip_config"))) lib_sddf_lwip_config_t lib_sddf_lwip_config;
__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;
__attribute__((__section__(".i2c_client_config"))) i2c_client_config_t i2c_config;
__attribute__((__section__(".fw_webserver_config"))) fw_webserver_config_t fw_config;

/* MicroPython is always built with networking and I2C support, but whether we
 * actually do anything with it depends on how the user has connected the MicroPython PD,
 * that is what these globals are for. */
bool net_enabled;
bool i2c_enabled;
bool fs_enabled;
bool serial_rx_enabled;
bool firewall_enabled;

// Allocate memory for the MicroPython GC heap.
static char heap[MICROPY_HEAP_SIZE];

static char mp_stack[MICROPY_STACK_SIZE];
static co_control_t co_controller_mem;

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

static char fw_ip_string[IPV4_ADDR_BUFLEN];

int mp_mod_network_prefer_dns_use_ip_version = 4;

i2c_queue_handle_t i2c_queue_handle;

#ifdef ENABLE_FRAMEBUFFER
uintptr_t framebuffer_data_region = 0x30000000;
#endif

static bool init_vfs(void) {
    mp_obj_t args[2] = {
        MP_OBJ_TYPE_GET_SLOT(&mp_type_vfs_fs, make_new)(&mp_type_vfs_fs, 0, 0, NULL),
        MP_OBJ_NEW_QSTR(MP_QSTR__slash_),
    };
    mp_vfs_mount(2, args, (mp_map_t *)&mp_const_empty_map);
    MP_STATE_VM(vfs_cur) = MP_STATE_VM(vfs_mount_table);
    return 0;
}

#ifndef NDEBUG
void MP_WEAK __assert_func(const char *file, int line, const char *func, const char *expr) {
    printf("MP: Assertion failure: %s:%d:%s:%s\n", file, line, func, expr);
    while (true) {}
}
#endif

static void netif_status_callback(char *ip_addr) {
    printf("%s: %s:%d:%s: DHCP request finished, IP address for %s is: %s\r\n",
           microkit_name, __FILE__, __LINE__, __func__, microkit_name, ip_addr);
}

void t_mp_entrypoint(void) {
    printf("MP|INFO: initialising!\n");

    // Initialise the MicroPython runtime.
#ifndef EXEC_MODULE
start_repl:
#endif
    mp_stack_ctrl_init();
    gc_init(heap, heap + sizeof(heap));
    mp_init();

    char *ip_string_arg = NULL;
    sddf_lwip_tx_intercept_condition_fn fw_intercept_arp = NULL;
    sddf_lwip_tx_handle_intercept_fn fw_handle_arp = NULL;
    sddf_lwip_netif_status_callback_fn netif_callback = netif_status_callback;
    if (firewall_enabled) {
        assert(net_enabled);
        // Active Rx packets are received from routing component
        fw_queue_init(&rx_active, fw_config.rx_active.queue.vaddr,
            sizeof(net_buff_desc_t), fw_config.rx_active.capacity);
        // Free Rx buffers are returned to the Rx virtualiser
        fw_queue_init(&rx_free, fw_config.rx_free.queue.vaddr,
            sizeof(net_buff_desc_t), fw_config.rx_free.capacity);
        // ARP queue is used to transmit ARP requests to the ARP requestor component
        fw_queue_init(&arp_req_queue, fw_config.arp_queue.request.vaddr,
            sizeof(fw_arp_request_t), fw_config.arp_queue.capacity);
        fw_queue_init(&arp_resp_queue, fw_config.arp_queue.response.vaddr,
            sizeof(fw_arp_request_t), fw_config.arp_queue.capacity);

        // lib sDDF LWIP requires ipv4 string for static ip configuration
        ipaddr_to_string(fw_config.interfaces[fw_config.interface].ip, fw_ip_string);

        // lib sDDF LWIP firewall arguments
        ip_string_arg = fw_ip_string;
        fw_intercept_arp = mpfirewall_intercept_arp;
        fw_handle_arp = mpfirewall_handle_arp;
        netif_callback = fw_netif_status_callback;

        init_firewall_webserver();
    }

    if (net_enabled) {
        if (net_config.rx.num_buffers) {
            net_queue_init(&net_rx_handle, net_config.rx.free_queue.vaddr,
                net_config.rx.active_queue.vaddr, net_config.rx.num_buffers);
        }
        if (net_config.tx.num_buffers) {
            net_queue_init(&net_tx_handle, net_config.tx.free_queue.vaddr,
                net_config.tx.active_queue.vaddr, net_config.tx.num_buffers);
            net_buffers_init(&net_tx_handle, 0);
        }

        sddf_lwip_init(&lib_sddf_lwip_config, &net_config, &timer_config, net_rx_handle,
            net_tx_handle, ip_string_arg, printf, netif_callback, NULL,
            fw_intercept_arp, fw_handle_arp);

        sddf_lwip_maybe_notify();
    }

    if (firewall_enabled) {
        mpfirewall_handle_notify();
    }
    // initialisation of the filesystem utilises the event loop and the event
    // loop unconditionally tries to process incoming network buffers; therefore
    // the networking needs to be initialised before initialising the fs
    if (fs_enabled) {
        init_vfs();
    }

    // Start a normal REPL; will exit when ctrl-D is entered on a blank line.
#ifndef EXEC_MODULE
    pyexec_friendly_repl();
#else
    pyexec_frozen_module(EXEC_MODULE, false);
#endif

    // Deinitialise the runtime.
    gc_sweep_all();
    mp_deinit();

    printf("MP|INFO: exited!\n");
#ifndef EXEC_MODULE
    goto start_repl;
#endif

    // libmicrokitco will gracefully clean up when a cothread return, no need to do anything special here
}

void init(void) {
    // TODO: problem, if one of these asserts fails it crashes micropython since it tries to output
    // to real serial instead of microkit_dbg_puts
    assert(serial_config_check_magic(&serial_config));
    assert(timer_config_check_magic(&timer_config));
    net_enabled = net_config_check_magic(&net_config);
    fs_enabled = fs_config_check_magic(&fs_config);
    serial_rx_enabled = (serial_config.rx.queue.vaddr != NULL);

    if (serial_rx_enabled) {
        serial_queue_init(&serial_rx_queue_handle, serial_config.rx.queue.vaddr, serial_config.rx.data.size, serial_config.rx.data.vaddr);
    }
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size, serial_config.tx.data.vaddr);

    firewall_enabled = (fw_config.rx_active.queue.vaddr != NULL);

    if (fs_enabled) {
        fs_command_queue = fs_config.server.command_queue.vaddr;
        fs_completion_queue = fs_config.server.completion_queue.vaddr;
        fs_share = fs_config.server.share.vaddr;
    }

    i2c_enabled = i2c_config_check_magic(&i2c_config);
    if (i2c_enabled) {
        i2c_queue_handle = i2c_queue_init(i2c_config.virt.req_queue.vaddr, i2c_config.virt.resp_queue.vaddr);
    }

    stack_ptrs_arg_array_t costacks = { (uintptr_t) mp_stack };
    microkit_cothread_init(&co_controller_mem, MICROPY_STACK_SIZE, costacks);

    syscalls_init();

    if (microkit_cothread_spawn(t_mp_entrypoint, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        printf("MP|ERROR: Cannot initialise Micropython cothread\n");
        assert(false);
    }

    // Run the Micropython cothread
    microkit_cothread_yield();
}

void notified(microkit_channel ch) {
    if (firewall_enabled) {
        mpfirewall_process_arp();
        mpfirewall_process_rx();
    }

    if (net_enabled) {
        sddf_lwip_process_rx();
        sddf_lwip_process_timeout();
    }

    if (fs_enabled) {
        fs_process_completions();
    }

    if (serial_rx_enabled && ch == serial_config.rx.id) {
        /* Check for keyboard interrupts */
        if (intercept_serial_rx_interrupt()) {
            /* Interrupt found, awaken Micropython thread early */
            mp_cothread_interrupt();
        }
    }

    // We ignore errors because notified can be invoked without the MP cothread awaiting in cases such as an async I/O completing.
    mp_cothread_maybe_recv(ch);

    if (net_enabled) {
        sddf_lwip_maybe_notify();
    }

    if (firewall_enabled) {
        mpfirewall_handle_notify();
    }
}

// Handle uncaught exceptions (should never be reached in a correct C implementation).
void nlr_jump_fail(void *val) {
    for (;;) {
    }
}

// Do a garbage collection cycle.
void gc_collect(void) {
    gc_collect_start();
    gc_helper_collect_regs_and_stack();
    gc_collect_end();
}
