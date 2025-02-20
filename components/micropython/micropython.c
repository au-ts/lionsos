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
#include <lions/fs/config.h>
#include "lwip/init.h"
#include "mpconfigport.h"
#include "fs_helpers.h"

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((__section__(".net_client_config"))) net_client_config_t net_config;
__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;

#ifdef ENABLE_I2C
__attribute__((__section__(".i2c_client_config"))) i2c_client_config_t i2c_config;
#endif

bool net_enabled;

// Allocate memory for the MicroPython GC heap.
static char heap[MICROPY_HEAP_SIZE];

static char mp_stack[MICROPY_STACK_SIZE];
static co_control_t co_controller_mem;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

serial_queue_handle_t serial_rx_queue_handle;
serial_queue_handle_t serial_tx_queue_handle;
i2c_queue_handle_t i2c_queue_handle;

#ifdef ENABLE_FRAMEBUFFER
uintptr_t framebuffer_data_region = 0x30000000;
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

void t_mp_entrypoint(void) {
    printf("MP|INFO: initialising!\n");

    // Initialise the MicroPython runtime.
#ifndef EXEC_MODULE
start_repl:
#endif
    mp_stack_ctrl_init();
    gc_init(heap, heap + sizeof(heap));
    mp_init();

    if (net_enabled) {
        init_networking();
    }
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
    // TODO: problem, if one of these asserts fails it crashse micropython since it tries to output
    // to real serial instead of microkit_dbg_puts
    assert(serial_config_check_magic(&serial_config));
    assert(timer_config_check_magic(&timer_config));
    net_enabled = net_config_check_magic(&net_config);
    assert(fs_config_check_magic(&fs_config));

    // TODO: there should be a better solution than this
    net_enabled = net_config_check_magic(&net_config);

    // TODO: hack
    if (serial_config.rx.queue.vaddr != NULL) {
        serial_queue_init(&serial_rx_queue_handle, serial_config.rx.queue.vaddr, serial_config.rx.data.size, serial_config.rx.data.vaddr);
    }
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size, serial_config.tx.data.vaddr);

    fs_command_queue = fs_config.server.command_queue.vaddr;
    fs_completion_queue = fs_config.server.completion_queue.vaddr;
    fs_share = fs_config.server.share.vaddr;

#ifdef ENABLE_I2C
    i2c_queue_handle = i2c_queue_init(i2c_config.virt.req_queue.vaddr, i2c_config.virt.resp_queue.vaddr);
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

void pyb_lwip_poll(void);
void mpnet_process_rx(void);
void mpnet_handle_notify(void);

void notified(microkit_channel ch) {
    if (net_enabled) {
        mpnet_process_rx();
        pyb_lwip_poll();
    }
    fs_process_completions();

    // We ignore errors because notified can be invoked without the MP cothread awaiting in cases such as an async I/O completing.
    microkit_cothread_recv_ntfn(ch);

    if (net_enabled) {
        mpnet_handle_notify();
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
