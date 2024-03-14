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

/* Data for the Kitty Python script. */
extern char _kitty_python_script[];

// Allocate memory for the MicroPython GC heap.
static char heap[MICROPY_HEAP_SIZE];

static char mp_stack[MICROPY_HEAP_SIZE];
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

i2c_queue_handle_t i2c_queue_handle;
uintptr_t i2c_request_region;
uintptr_t i2c_response_region;
uintptr_t i2c_data_region;

int active_events = mp_event_source_none;
int mp_blocking_events = mp_event_source_none;

void await(int event_source) {
    if (active_events & event_source) {
        active_events &= ~event_source;
        return;
    }
    mp_blocking_events = event_source;
    co_switch(t_event);
    mp_blocking_events = mp_event_source_none;
    active_events &= ~event_source;
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
    // @ivanv: improve/fix, use printf?
    microkit_dbg_puts("MP|ERROR: Assertion failed!\n");
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
start_repl:
    mp_stack_ctrl_init();
    gc_init(heap, heap + sizeof(heap));
    mp_init();

    init_nfs();
    init_networking();

    // Start a normal REPL; will exit when ctrl-D is entered on a blank line.
    pyexec_friendly_repl();
    // exec_str(_kitty_python_script, MP_PARSE_FILE_INPUT);

    // Deinitialise the runtime.
    gc_sweep_all();
    mp_deinit();

    printf("MP|INFO: exited!\n");
    goto start_repl;

    co_switch(t_event);
}

void init(void) {
    serial_queue_init(&serial_rx_queue, (serial_queue_t *)serial_rx_free, (serial_queue_t *)serial_rx_active, false, BUFFER_SIZE, BUFFER_SIZE);
    for (int i = 0; i < NUM_ENTRIES - 1; i++) {
        serial_enqueue_free(&serial_rx_queue, serial_rx_data + ((i + NUM_ENTRIES) * BUFFER_SIZE), BUFFER_SIZE, NULL);
    }
    serial_queue_init(&serial_tx_queue, (serial_queue_t *)serial_tx_free, (serial_queue_t *)serial_tx_active, false, BUFFER_SIZE, BUFFER_SIZE);
    for (int i = 0; i < NUM_ENTRIES - 1; i++) {
        serial_enqueue_free(&serial_tx_queue, serial_tx_data + ((i + NUM_ENTRIES) * BUFFER_SIZE), BUFFER_SIZE, NULL);
    }

    i2c_queue_handle = i2c_queue_init((i2c_queue_t *)i2c_request_region, (i2c_queue_t *)i2c_response_region);

    t_event = co_active();
    // @ivanv: figure out a better stack size
    t_mp = co_derive((void *)mp_stack, MICROPY_HEAP_SIZE, t_mp_entrypoint);
    co_switch(t_mp);
}

void pyb_lwip_poll(void);
void process_rx(void);

void notified(microkit_channel ch) {
    pyb_lwip_poll();
    process_rx();

    switch (ch) {
    case SERIAL_RX_CH:
        active_events |= mp_event_source_serial;
        break;
    case TIMER_CH:
        active_events |= mp_event_source_timer;
        break;
    case VMM_CH:
        /* We have gotten a message from the VMM, which means the framebuffer is ready. */
        active_events |= mp_event_source_framebuffer;
        break;
    case NFS_CH:
        active_events |= mp_event_source_nfs;
        break;
    case I2C_CH:
        active_events |= mp_event_source_i2c;
        break;
    case ETH_RX_CH:
    case ETH_TX_CH:
        /* Nothing to do here right now, but we catch it the case where we get
         * notified by the RX and TX ethernet components since it is expected
         * we get notifications from them. */
        break;
    default:
        printf("MP|ERROR: unexpected notification received from channel: 0x%lx\n", ch);
    }
    if (active_events & mp_blocking_events) {
        co_switch(t_mp);
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
