/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * The runner: WAMR and one WebAssembly app, in a sandbox (see sandbox.h).
 *
 * The host loads this program into the sandbox afresh for every app, with
 * the app's module, its grants and a mailbox, and starts it at _start. It
 * is not a Microkit PD: it uses no libmicrokit, only the sandbox's channel
 * to the host, the stack and heap the host mapped, and musl for the C
 * library, with no system calls except output, which goes to the host.
 *
 * The runner checks every handle an app passes against its grants, as the
 * in-PD host does, but that check is only for good error reporting: a
 * window or file that was not granted is not mapped at all, and the host
 * checks the grant for every service it provides.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <microkit.h>
#include <sddf/util/util.h>
#include <wasm_export.h>
#include "../src/gfx.h"
#include "sandbox.h"

/* Microkit's capabilities in the sandbox's CSpace, which this program keeps using */
#define RUNNER_INPUT_CAP 1
#define RUNNER_REPLY_CAP 4
#define RUNNER_HOST_NTFN_CAP (BASE_OUTPUT_NOTIFICATION_CAP + SANDBOX_RUNNER_CH)

/* Where Microkit maps a PD's IPC buffer; libsel4 writes received message registers there */
seL4_IPCBuffer *__sel4_ipc_buffer = (seL4_IPCBuffer *)0xfffffff000UL;

#define APP_STACK_SIZE (64 * 1024)
#define APP_HEAP_SIZE (64 * 1024)
/* Room kept below the native stack boundary WAMR checks against */
#define NATIVE_STACK_RESERVE 0x4000
#define MIN_TICK_MS 10

static sandbox_grants_t *const grants = (sandbox_grants_t *)SANDBOX_GRANTS_VADDR;
static sandbox_mailbox_t *const mailbox = (sandbox_mailbox_t *)SANDBOX_MAILBOX_VADDR;
static gui_state_t *const gui_state = (gui_state_t *)SANDBOX_STATE_VADDR;

static gfx_surface_t surface;
static bool dropped[SANDBOX_MAX_GRANTS];
static bool exit_requested;
static gfx_rect_t damage;
static bool damaged;

static wasm_module_inst_t instance;
static wasm_exec_env_t exec_env;
static wasm_function_inst_t fn_event, fn_tick;

/* Talking to the host */

static void signal_host(void)
{
    seL4_Signal(RUNNER_HOST_NTFN_CAP);
}

static void request_typed(uint32_t type, int32_t arg, uint32_t grant_type, const char *text)
{
    uint32_t tail = mailbox->req_tail;
    if (tail - __atomic_load_n(&mailbox->req_head, __ATOMIC_ACQUIRE) >= SANDBOX_REQS) {
        /* The host runs at a higher priority, so this only happens if it is not draining */
        mailbox->reqs_dropped++;
        return;
    }
    sandbox_req_t *req = &mailbox->reqs[tail % SANDBOX_REQS];
    req->type = type;
    req->arg = arg;
    req->grant_type = grant_type;
    req->text[0] = '\0';
    if (text) {
        strncpy(req->text, text, SANDBOX_TEXT_MAX - 1);
        req->text[SANDBOX_TEXT_MAX - 1] = '\0';
    }
    __atomic_store_n(&mailbox->req_tail, tail + 1, __ATOMIC_RELEASE);
    signal_host();
}

static void request(uint32_t type, int32_t arg, const char *text)
{
    request_typed(type, arg, 0, text);
}

/* Wait for the host to signal us */
static void wait_for_host(void)
{
    seL4_Word badge;
    seL4_Recv(RUNNER_INPUT_CAP, &badge, RUNNER_REPLY_CAP);
}

/* Stop running the app; the host will take everything away */
static void halt(uint32_t type, const char *text)
{
    fflush(stdout);
    request(type, 0, text);
    for (;;) {
        wait_for_host();
    }
}

/*
 * musl's system calls. The only ones the runtime makes that matter are
 * writes to stdout and stderr, from diagnostics such as call stack dumps;
 * they go to the host a line at a time.
 */

void __init_libc(char **envp, char *pn);
extern size_t __sysinfo;

static char debug_line[SANDBOX_TEXT_MAX];
static size_t debug_len;

static void debug_out(const char *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == '\n' || debug_len == SANDBOX_TEXT_MAX - 1) {
            debug_line[debug_len] = '\0';
            if (debug_len) {
                request(SANDBOX_REQ_DEBUG, 0, debug_line);
            }
            debug_len = 0;
            if (buf[i] == '\n') {
                continue;
            }
        }
        debug_line[debug_len++] = buf[i];
    }
}

static long runner_syscall(long n, ...)
{
    va_list ap;
    va_start(ap, n);
    long ret = -ENOSYS;
    if (n == SYS_writev) {
        int fd = va_arg(ap, int);
        const struct iovec *iov = va_arg(ap, const struct iovec *);
        int iovcnt = va_arg(ap, int);
        if (fd == 1 || fd == 2) {
            ret = 0;
            for (int i = 0; i < iovcnt; i++) {
                debug_out(iov[i].iov_base, iov[i].iov_len);
                ret += iov[i].iov_len;
            }
        } else {
            ret = -EBADF;
        }
    } else if (n == SYS_ioctl) {
        /* musl asks whether stdout is a terminal */
        ret = -ENOTTY;
    }
    va_end(ap);
    return ret;
}

static void init_libc(void)
{
    static char *envp[] = { NULL, NULL };
    __sysinfo = (size_t)runner_syscall;
    __init_libc(envp, NULL);
    /* stdout is not a terminal, so musl would otherwise buffer it fully */
    setvbuf(stdout, NULL, _IOLBF, 0);
}

/* The time, from the counter seL4 lets user code read (CONFIG_EXPORT_PCNT_USER) */
static uint32_t now_ms(void)
{
    uint64_t count, freq;
    __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(count));
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    return (uint32_t)(count / (freq / 1000));
}

/* Grants */

/* The grant behind `handle` if it has `type`; otherwise the host audits the refusal of `op` */
static sandbox_grant_t *grant(int32_t handle, uint32_t type, const char *op)
{
    if (handle >= 0 && handle < SANDBOX_MAX_GRANTS && !dropped[handle] && grants->grants[handle].live
        && grants->grants[handle].type == type) {
        return &grants->grants[handle];
    }
    request_typed(SANDBOX_REQ_DENIED, handle, type, op);
    return NULL;
}

static gfx_surface_t *window(int32_t handle, const char *op)
{
    return grant(handle, SANDBOX_GRANT_WINDOW, op) ? &surface : NULL;
}

/* Native functions imported by apps from the "lions" module, as in wasm_host.c */

static void add_damage(int32_t x, int32_t y, int32_t w, int32_t h)
{
    int32_t x0 = MAX(x, 0), y0 = MAX(y, 0);
    int32_t x1 = MIN((int64_t)x + w, (int32_t)surface.width);
    int32_t y1 = MIN((int64_t)y + h, (int32_t)surface.height);
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    if (!damaged) {
        damage = (gfx_rect_t) { x0, y0, x1 - x0, y1 - y0 };
        damaged = true;
        return;
    }
    int32_t dx0 = MIN(damage.x, x0), dy0 = MIN(damage.y, y0);
    int32_t dx1 = MAX(damage.x + damage.width, x1), dy1 = MAX(damage.y + damage.height, y1);
    damage = (gfx_rect_t) { dx0, dy0, dx1 - dx0, dy1 - dy0 };
}

static int32_t n_fill_rect(wasm_exec_env_t env, int32_t win, int32_t x, int32_t y, int32_t w, int32_t h, int32_t rgb)
{
    (void)env;
    gfx_surface_t *s = window(win, "fill_rect");
    if (!s) {
        return -1;
    }
    gfx_fill_rect(s, (gfx_rect_t) { x, y, w, h }, 0xff000000u | (uint32_t)rgb);
    return 0;
}

static int32_t n_draw_text(wasm_exec_env_t env, int32_t win, int32_t x, int32_t y, const char *text, int32_t scale,
                           int32_t rgb)
{
    (void)env;
    gfx_surface_t *s = window(win, "draw_text");
    if (!s) {
        return -1;
    }
    scale = MAX(MIN(scale, 8), 1);
    gfx_draw_text(s, x, y, text, scale, 0xff000000u | (uint32_t)rgb);
    return 0;
}

static int32_t n_blit(wasm_exec_env_t env, int32_t win, int32_t x, int32_t y, int32_t w, int32_t h,
                      const uint8_t *pixels, uint32_t len)
{
    (void)env;
    gfx_surface_t *s = window(win, "blit");
    if (!s || w <= 0 || h <= 0 || (uint64_t)w * h * 4 > len) {
        return -1;
    }
    gfx_blit(s, x, y, (const uint32_t *)pixels, w, h, w);
    return 0;
}

static int32_t n_commit(wasm_exec_env_t env, int32_t win, int32_t x, int32_t y, int32_t w, int32_t h)
{
    (void)env;
    if (!window(win, "commit")) {
        return -1;
    }
    add_damage(x, y, w, h);
    return 0;
}

static int32_t n_width(wasm_exec_env_t env, int32_t win)
{
    (void)env;
    return window(win, "width") ? (int32_t)surface.width : -1;
}

static int32_t n_height(wasm_exec_env_t env, int32_t win)
{
    (void)env;
    return window(win, "height") ? (int32_t)surface.height : -1;
}

static int32_t n_set_title(wasm_exec_env_t env, int32_t win, const char *title)
{
    (void)env;
    if (!window(win, "set_title")) {
        return -1;
    }
    uint32_t i = 0;
    for (; title[i] != '\0' && i < GUI_TITLE_MAX - 1; i++) {
        gui_state->title[i] = title[i];
    }
    gui_state->title[i] = '\0';
    return 0;
}

static int32_t n_timer_start(wasm_exec_env_t env, int32_t timer, int32_t ms)
{
    (void)env;
    if (!grant(timer, SANDBOX_GRANT_TIMER, "timer_start")) {
        return -1;
    }
    request(SANDBOX_REQ_TIMER, ms <= 0 ? 0 : MAX(ms, MIN_TICK_MS), NULL);
    return 0;
}

static int32_t n_timer_now(wasm_exec_env_t env, int32_t timer)
{
    (void)env;
    return grant(timer, SANDBOX_GRANT_TIMER, "timer_now") ? (int32_t)now_ms() : -1;
}

static int32_t n_console_log(wasm_exec_env_t env, int32_t console, const char *text)
{
    (void)env;
    if (!grant(console, SANDBOX_GRANT_CONSOLE, "console_log")) {
        return -1;
    }
    request(SANDBOX_REQ_LOG, console, text);
    return 0;
}

static int32_t n_file_size(wasm_exec_env_t env, int32_t file)
{
    (void)env;
    sandbox_grant_t *g = grant(file, SANDBOX_GRANT_FILE, "file_size");
    return g ? (int32_t)MIN(g->size, (uint64_t)INT32_MAX) : -1;
}

/* WAMR checked that `buf` spans `len` bytes of the app's memory */
static int32_t n_file_read(wasm_exec_env_t env, int32_t file, uint32_t offset, uint8_t *buf, uint32_t len)
{
    (void)env;
    sandbox_grant_t *g = grant(file, SANDBOX_GRANT_FILE, "file_read");
    if (!g) {
        return -1;
    }
    if (offset >= g->size) {
        return 0;
    }
    uint64_t n = MIN((uint64_t)len, g->size - offset);
    memcpy(buf, (const uint8_t *)g->vaddr + offset, n);
    return (int32_t)n;
}

static int32_t n_cap_lookup(wasm_exec_env_t env, const char *name)
{
    (void)env;
    for (int i = 0; i < SANDBOX_MAX_GRANTS; i++) {
        if (grants->grants[i].live && !dropped[i] && strcmp(grants->grants[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int32_t n_cap_drop(wasm_exec_env_t env, int32_t handle)
{
    (void)env;
    if (handle < 0 || handle >= SANDBOX_MAX_GRANTS || !grants->grants[handle].live || dropped[handle]) {
        return -1;
    }
    /* The host takes back what backs the capability, so stop using it first */
    dropped[handle] = true;
    request(SANDBOX_REQ_DROP, handle, NULL);
    return 0;
}

static void n_exit(wasm_exec_env_t env)
{
    (void)env;
    exit_requested = true;
}

static NativeSymbol native_symbols[] = {
    { "cap_lookup", n_cap_lookup, "($)i", NULL },
    { "cap_drop", n_cap_drop, "(i)i", NULL },
    { "win_fill_rect", n_fill_rect, "(iiiiii)i", NULL },
    { "win_draw_text", n_draw_text, "(iii$ii)i", NULL },
    { "win_blit", n_blit, "(iiiii*~)i", NULL },
    { "win_commit", n_commit, "(iiiii)i", NULL },
    { "win_width", n_width, "(i)i", NULL },
    { "win_height", n_height, "(i)i", NULL },
    { "win_set_title", n_set_title, "(i$)i", NULL },
    { "timer_start", n_timer_start, "(ii)i", NULL },
    { "timer_now", n_timer_now, "(i)i", NULL },
    { "console_log", n_console_log, "(i$)i", NULL },
    { "file_size", n_file_size, "(i)i", NULL },
    { "file_read", n_file_read, "(ii*~)i", NULL },
    { "exit", n_exit, "()", NULL },
};

/*
 * WAMR's allocations, from a pool over the heap the host mapped. Linear
 * memory is zeroed, as in wasm_host.c; memory from the pool is fresh for
 * every app, but the pool reuses what an app frees.
 */

typedef void *mem_allocator_t;
mem_allocator_t mem_allocator_create(void *mem, uint32_t size);
void *mem_allocator_malloc(mem_allocator_t allocator, uint32_t size);
void *mem_allocator_realloc(mem_allocator_t allocator, void *ptr, uint32_t size);
void mem_allocator_free(mem_allocator_t allocator, void *ptr);

static mem_allocator_t pool;
static void *linear_memory;
static uint32_t linear_memory_size;

static void *pool_malloc(mem_alloc_usage_t usage, unsigned int size)
{
    void *ptr = mem_allocator_malloc(pool, size);
    if (ptr && usage == Alloc_For_LinearMemory) {
        memset(ptr, 0, size);
        linear_memory = ptr;
        linear_memory_size = size;
    }
    return ptr;
}

static void *pool_realloc(mem_alloc_usage_t usage, bool full_size_mmaped, void *ptr, unsigned int size)
{
    (void)full_size_mmaped;
    uint8_t *new_ptr = mem_allocator_realloc(pool, ptr, size);
    if (new_ptr && usage == Alloc_For_LinearMemory && ptr == linear_memory) {
        if (size > linear_memory_size) {
            memset(new_ptr + linear_memory_size, 0, size - linear_memory_size);
        }
        linear_memory = new_ptr;
        linear_memory_size = size;
    }
    return new_ptr;
}

static void pool_free(mem_alloc_usage_t usage, void *ptr)
{
    (void)usage;
    if (ptr == linear_memory) {
        linear_memory = NULL;
    }
    mem_allocator_free(pool, ptr);
}

/* Running the app */

/* Call an app export, then publish what it drew */
static void call_app(wasm_function_inst_t fn, uint32_t argc, uint32_t *argv)
{
    if (fn && !wasm_runtime_call_wasm(exec_env, fn, argc, argv)) {
        /* WAMR has printed where the app trapped (WAMR_BUILD_DUMP_CALL_STACK) */
        const char *exception = wasm_runtime_get_exception(instance);
        halt(SANDBOX_REQ_TRAP, exception ? exception : "trapped");
    }
    if (damaged) {
        gui_state_commit(gui_state, (gui_rect_t) { damage.x, damage.y, damage.width, damage.height });
        damaged = false;
        request(SANDBOX_REQ_COMMIT, 0, NULL);
    }
    if (exit_requested) {
        halt(SANDBOX_REQ_EXIT, NULL);
    }
}

static void handle_messages(void)
{
    uint32_t head = mailbox->msg_head;
    while (__atomic_load_n(&mailbox->msg_tail, __ATOMIC_ACQUIRE) != head) {
        sandbox_msg_t msg = mailbox->msgs[head % SANDBOX_MSGS];
        head++;
        __atomic_store_n(&mailbox->msg_head, head, __ATOMIC_RELEASE);
        if (msg.type == SANDBOX_MSG_EVENT) {
            uint32_t argv[5] = { msg.ev.type, msg.ev.code, (uint32_t)msg.ev.value, (uint32_t)msg.ev.x,
                                 (uint32_t)msg.ev.y };
            call_app(fn_event, 5, argv);
        } else if (msg.type == SANDBOX_MSG_TICK) {
            uint32_t argv[1] = { msg.time_ms };
            call_app(fn_tick, 1, argv);
        }
    }
}

void runner_main(void)
{
    init_libc();
    if (grants->magic != SANDBOX_GRANTS_MAGIC) {
        halt(SANDBOX_REQ_TRAP, "no grants");
    }

    pool = mem_allocator_create((void *)SANDBOX_HEAP_VADDR, SANDBOX_HEAP_SIZE);
    RuntimeInitArgs args = {
        .mem_alloc_type = Alloc_With_Allocator,
        .mem_alloc_option.allocator = {
            .malloc_func = pool_malloc,
            .realloc_func = pool_realloc,
            .free_func = pool_free,
        },
        .native_module_name = "lions",
        .native_symbols = native_symbols,
        .n_native_symbols = ARRAY_SIZE(native_symbols),
    };
    if (!wasm_runtime_full_init(&args)) {
        halt(SANDBOX_REQ_TRAP, "could not start the runtime");
    }

    static char error[SANDBOX_TEXT_MAX];
    uint32_t size = MIN(grants->module_size, (uint32_t)SANDBOX_MODULE_MAX);
    wasm_module_t module = wasm_runtime_load((uint8_t *)SANDBOX_MODULE_VADDR, size, error, sizeof(error));
    if (module) {
        instance = wasm_runtime_instantiate(module, APP_STACK_SIZE, APP_HEAP_SIZE, error, sizeof(error));
    }
    if (instance) {
        exec_env = wasm_runtime_create_exec_env(instance, APP_STACK_SIZE);
        if (!exec_env) {
            snprintf(error, sizeof(error), "could not create exec env");
        }
    }
    if (!exec_env) {
        halt(SANDBOX_REQ_TRAP, error);
    }
    /* The stack is the one the host mapped */
    wasm_runtime_set_native_stack_boundary(exec_env, (uint8_t *)SANDBOX_STACK_VADDR + NATIVE_STACK_RESERVE);

    surface = (gfx_surface_t) {
        .pixels = (uint32_t *)SANDBOX_SURFACE_VADDR,
        .width = grants->width,
        .height = grants->height,
        .stride = grants->width,
    };
    gfx_reset_clip(&surface);

    fn_event = wasm_runtime_lookup_function(instance, "app_event");
    fn_tick = wasm_runtime_lookup_function(instance, "app_tick");
    wasm_function_inst_t fn_init = wasm_runtime_lookup_function(instance, "app_init");

    /* The host cleared the window before starting us */
    uint32_t argv[2] = { grants->width, grants->height };
    call_app(fn_init, 2, argv);

    for (;;) {
        handle_messages();
        wait_for_host();
    }
}

/* The host starts us here, with nothing but the stack it mapped */
__attribute__((naked, section(".text.start"))) void _start(void)
{
    __asm__ volatile("ldr x0, =%0\n"
                     "mov sp, x0\n"
                     "bl runner_main\n"
                     "1: b 1b\n" : : "i"(SANDBOX_STACK_TOP));
}
