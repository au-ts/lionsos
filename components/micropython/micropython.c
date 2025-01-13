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
#include <serial_config.h>
#include <sddf/i2c/queue.h>
#include <sddf/network/lib_sddf_lwip.h>
#include <sddf/network/queue.h>
#include <ethernet_config.h>
#include "mpconfigport.h"
#include "fs_helpers.h"

// Allocate memory for the MicroPython GC heap.
static char heap[MICROPY_HEAP_SIZE];

static char mp_stack[MICROPY_STACK_SIZE];
static co_control_t co_controller_mem;

char *fs_share;

/*
 * Shared regions for serial communication
 */
char *serial_rx_data;
char *serial_tx_data;
serial_queue_t *serial_rx_queue;
serial_queue_t *serial_tx_queue;
serial_queue_handle_t serial_rx_queue_handle;
serial_queue_handle_t serial_tx_queue_handle;

/*
 * Shared regions for network communication
 */
net_queue_t *net_rx_free;
net_queue_t *net_rx_active;
net_queue_t *net_tx_free;
net_queue_t *net_tx_active;
uintptr_t net_rx_buffer_data_region;
uintptr_t net_tx_buffer_data_region;

net_queue_handle_t net_rx_queue;
net_queue_handle_t net_tx_queue;

#ifdef ENABLE_I2C
i2c_queue_handle_t i2c_queue_handle;
uintptr_t i2c_request_region;
uintptr_t i2c_response_region;
uintptr_t i2c_data_region;
#endif

#ifdef ENABLE_FRAMEBUFFER
uintptr_t framebuffer_data_region;
#endif

STATIC bool init_vfs(void) {
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

    size_t rx_capacity, tx_capacity;
    net_cli_queue_capacity(microkit_name, &rx_capacity, &tx_capacity);
    net_queue_init(&net_rx_queue, net_rx_free, net_rx_active, rx_capacity);
    net_queue_init(&net_tx_queue, net_tx_free, net_tx_active, tx_capacity);
    net_buffers_init(&net_tx_queue, 0);

    sddf_lwip_init(net_rx_queue, net_tx_queue, ETH_RX_CH, ETH_TX_CH,
                   net_rx_buffer_data_region, net_tx_buffer_data_region,
                   TIMER_CH, MAC_ADDR_CLI1,
                   NULL, netif_status_callback, NULL);

    sddf_lwip_maybe_notify();

    // initialisation of the filesystem utilises the event loop and the event
    // loop unconditionally tries to process incoming network buffers; therefore
    // the networking needs to be initialised before initialising the fs
    init_vfs();

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
    serial_cli_queue_init_sys(microkit_name, &serial_rx_queue_handle,
                              serial_rx_queue, serial_rx_data,
                              &serial_tx_queue_handle, serial_tx_queue,
                              serial_tx_data);

#ifdef ENABLE_I2C
    i2c_queue_handle = i2c_queue_init((i2c_queue_t *)i2c_request_region, (i2c_queue_t *)i2c_response_region);
#endif

    stack_ptrs_arg_array_t costacks = { mp_stack };
    microkit_cothread_init(&co_controller_mem, MICROPY_STACK_SIZE, costacks);

    if (microkit_cothread_spawn(t_mp_entrypoint, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        printf("MP|ERROR: Cannot initialise Micropython cothread\n");
        assert(false);
    }

    // Run the Micropython cothread
    microkit_cothread_yield();
}

void mpnet_handle_notify(void);

void notified(microkit_channel ch) {
    sddf_lwip_process_rx();
    sddf_lwip_process_timeout();
    fs_process_completions();

    // We ignore errors because notified can be invoked without the MP cothread awaiting in cases such as an async I/O completing.
    microkit_cothread_recv_ntfn(ch);

    sddf_lwip_maybe_notify();
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
