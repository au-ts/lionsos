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

/* Data for the Kitty Python script. */
extern char _kitty_python_script[];

// Allocate memory for the MicroPython GC heap.
static char heap[MICROPY_HEAP_SIZE];

static char mp_stack[MICROPY_HEAP_SIZE];
cothread_t t_event, t_mp;

int active_events = mp_event_source_none;
int mp_blocking_events = mp_event_source_none;

#ifndef NDEBUG
void MP_WEAK __assert_func(const char *file, int line, const char *func, const char *expr) {
    // @ivanv: improve/fix, use printf?
    microkit_dbg_puts("MICROPYTHON|ERROR: Assertion failed!\n");
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
    printf("MICROPYTHON|INFO: initialising!\n");

    // Initialise the MicroPython runtime.
    mp_stack_ctrl_init();
    gc_init(heap, heap + sizeof(heap));
    mp_init();

    // Start a normal REPL; will exit when ctrl-D is entered on a blank line.
    // pyexec_friendly_repl();
    exec_str(_kitty_python_script, MP_PARSE_FILE_INPUT);

    // Deinitialise the runtime.
    gc_sweep_all();
    mp_deinit();

    printf("MICROPYTHON|INFO: exited!\n");
    co_switch(t_event);
}

void init(void) {
    t_event = co_active();
    // @ivanv: figure out a better stack size
    t_mp = co_derive((void *)mp_stack, MICROPY_HEAP_SIZE, t_mp_entrypoint);
    co_switch(t_mp);
}

void notified(microkit_channel ch) {
    switch (ch) {
    case TIMER_CH:
        active_events |= mp_event_source_timer;
        break;
    case VMM_CH:
        printf("MICROPYTHON|INFO: got notification from VMM\n");
        /* We have gotten a message from the VMM, which means the framebuffer is ready. */
        active_events |= mp_event_source_framebuffer;
        break;
    default:
        printf("MICROPYTHON|ERROR: unknown notification received from channel: 0x%lx\n", ch);
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

// There is no filesystem so stat'ing returns nothing.
mp_import_stat_t mp_import_stat(const char *path) {
    return MP_IMPORT_STAT_NO_EXIST;
}

// There is no filesystem so opening a file raises an exception.
mp_lexer_t *mp_lexer_new_from_file(const char *filename) {
    mp_raise_OSError(MP_ENOENT);
}
