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
#include "vfs_sddf_fs.h"
#include <extmod/vfs.h>
#include <sddf/serial/queue.h>
#include <sddf/i2c/queue.h>
#include "lwip/init.h"
#include "mpconfigport.h"
#include "fs_helpers.h"
#include <libmicrokitco.h>

#define CO_CONTROL_SIZE 0x1000

// Allocate memory for the MicroPython GC heap.
static char heap[MICROPY_HEAP_SIZE];

static char mp_stack[MICROPY_STACK_SIZE];
static char co_controller_mem[CO_CONTROL_SIZE];

cothread_t t_event, t_mp;

char *nfs_share;

/* Shared memory regions for sDDF serial sub-system */
uintptr_t serial_rx_free;
uintptr_t serial_rx_active;
uintptr_t serial_tx_free;
uintptr_t serial_tx_active;
uintptr_t serial_rx_data;
uintptr_t serial_tx_data;

serial_queue_handle_t serial_rx_queue;
serial_queue_handle_t serial_tx_queue;

#ifdef ENABLE_I2C
i2c_queue_handle_t i2c_queue_handle;
uintptr_t i2c_request_region;
uintptr_t i2c_response_region;
uintptr_t i2c_data_region;
#endif

#ifdef ENABLE_FRAMEBUFFER
uintptr_t framebuffer_data_region;
#endif

int active_events = mp_event_source_none;
int mp_blocking_events = mp_event_source_none;

// Map an event source to a Microkit channel. Return 0 on success.
int event_source_to_microkit_channel(int event_source, microkit_channel *ret) {
    switch (event_source) {
        case mp_event_source_serial:
            *ret = SERIAL_RX_CH;
            break;
        case mp_event_source_timer:
            *ret = TIMER_CH;
            break;
        case mp_event_source_nfs:
            *ret = NFS_CH;
            break;
#ifdef ENABLE_FRAMEBUFFER
        case mp_event_source_framebuffer:
            *ret = FRAMEBUFFER_VMM_CH;
            break;
#endif
#ifdef ENABLE_I2C
        case mp_event_source_i2c:
            *ret = I2C_CH;
            break;
#endif
        default:
            return 1;
    }

    return 0;
}

void await(int event_source) {
    microkit_channel ch;
    if (event_source_to_microkit_channel(event_source, &ch) == 0) {
        microkit_cothread_wait(ch);
    } else {
        printf("MP|ERROR: await() called with unknown event source: %d\n", event_source);
    }
}

STATIC bool init_nfs(void) {
    mp_obj_t args[2] = {
        MP_OBJ_TYPE_GET_SLOT(&mp_type_vfs_sddf_fs, make_new)(&mp_type_vfs_sddf_fs, 0, 0, NULL),
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

// @ivanv: I don't know if this the best way of doing this
static void exec_str(const char *src, mp_parse_input_kind_t input_kind) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        // Compile, parse and execute the given string.
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, strlen(src), 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, input_kind);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, true);
        mp_call_function_0(module_fun);
        nlr_pop();
    } else {
        // Uncaught exception: print it out.
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
    }
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

    init_nfs();
    init_networking();

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

    // libmicrokitco will gracefully clean up when a cothread return, no need to do anything here
}

void init(void) {
    serial_queue_init(&serial_rx_queue, (serial_queue_t *)serial_rx_free, (serial_queue_t *)serial_rx_active, false, BUFFER_SIZE, BUFFER_SIZE);
    for (int i = 0; i < NUM_ENTRIES - 1; i++) {
        serial_enqueue_free(&serial_rx_queue, serial_rx_data + ((i + NUM_ENTRIES) * BUFFER_SIZE), BUFFER_SIZE);
    }
    serial_queue_init(&serial_tx_queue, (serial_queue_t *)serial_tx_free, (serial_queue_t *)serial_tx_active, false, BUFFER_SIZE, BUFFER_SIZE);
    for (int i = 0; i < NUM_ENTRIES - 1; i++) {
        serial_enqueue_free(&serial_tx_queue, serial_tx_data + ((i + NUM_ENTRIES) * BUFFER_SIZE), BUFFER_SIZE);
    }

#ifdef ENABLE_I2C
    i2c_queue_handle = i2c_queue_init((i2c_queue_t *)i2c_request_region, (i2c_queue_t *)i2c_response_region);
#endif

    co_err_t co_err = microkit_cothread_init(&co_controller_mem, MICROPY_STACK_SIZE, &mp_stack);
    if (co_err != co_no_err) {
        printf("MP|ERROR: Cannot initialise libmicrokitco, err is: %s", microkit_cothread_pretty_error(co_err));
        while (true) {}
    }

    microkit_cothread_t _mp_cothread_handle;
    co_err = microkit_cothread_spawn(t_mp_entrypoint, ready_true, &_mp_cothread_handle, 0);
    if (co_err != co_no_err) {
        printf("MP|ERROR: Cannot initialise Micropython cothread, err is: %s", microkit_cothread_pretty_error(co_err));
        while (true) {}
    }

    // Run the Micropython cothread
    microkit_cothread_yield();
}

void pyb_lwip_poll(void);
void process_rx(void);
void mpnet_handle_notify(void);

void notified(microkit_channel ch) {
    process_rx();
    pyb_lwip_poll();
    fs_process_completions();

    // We ignore errors because notified can be invoked without the MP cothread awaiting in cases such as an async I/O completing.
    microkit_cothread_recv_ntfn(ch);

    mpnet_handle_notify();
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
