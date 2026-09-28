/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * WebAssembly app host: a desktop app slot whose program is chosen at run
 * time.
 *
 * The host mounts the FAT file system, lists the .wasm files in /apps and
 * shows them in its window. Clicking one loads it from the disk and runs it
 * with WAMR in the same window; Esc (or the app calling lions.exit) returns
 * to the list. Apps are compiled freestanding against wasm_apps/lions.h:
 * they import functions from the "lions" module and export callbacks that
 * the host calls:
 *
 *   app_init(width, height)             once, after loading (0, 0 without a window)
 *   app_event(type, code, value, x, y)  for each input event (GUI_EV_*); for
 *                                       key events x is the typed character
 *   app_tick(time_ms)                   after lions.timer_start(timer, ms)
 *
 * Apps start with no authority at all. Each gets only the capabilities
 * listed for it in /apps/<name>.caps and allowed by the host's policy, and
 * every lions.* function takes a handle to one of them (see caps.h). The
 * list shows what each app will be granted before it is run.
 *
 * Everything runs in one cothread, which waits on a semaphore that the
 * compositor and timer notifications signal, so file system requests can
 * block while the host stays event driven.
 *
 * WAMR allocates everything, including apps' linear memory, from a pool in
 * this PD through its own allocator, so memory is returned when an app
 * stops. (The LionsOS libc's mmap never frees, and WAMR would otherwise use
 * it for linear memory.) Every pointer and rectangle passed by an app is
 * validated, so a misbehaving app can at worst fail itself or hang this PD.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <microkit.h>
#include <libmicrokitco.h>
#include <lions/posix/posix.h>
#include <lions/fs/config.h>
#include <lions/fs/protocol.h>
#include <lions/fs/helpers.h>
#include <lions/input/input.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <sddf/util/util.h>
#include <wasm_export.h>
#include "../apps/gui_app.h"
#include "caps.h"

#define WIDTH 480
#define HEIGHT 360

#define APPS_DIR "/apps"
#define MAX_APPS 16
#define NAME_MAX_LEN 48
#define WASM_MAX_SIZE (512 * 1024)

#define RUNTIME_POOL_SIZE (4 * 1024 * 1024)
#define APP_STACK_SIZE (64 * 1024)
#define APP_HEAP_SIZE (64 * 1024)
#define HOST_STACK_SIZE 0x100000
#define MIN_TICK_MS 10

#define COLOUR_BG GFX_RGB(0x1b, 0x1f, 0x27)
#define COLOUR_ROW GFX_RGB(0x28, 0x2c, 0x34)
#define COLOUR_ACCENT GFX_RGB(0xe0, 0x8a, 0x1e)
#define COLOUR_TEXT GFX_RGB(0xe6, 0xe6, 0xe6)
#define COLOUR_MUTED GFX_RGB(0x8a, 0x8f, 0x99)
#define COLOUR_ERROR GFX_RGB(0xd9, 0x4c, 0x3d)

#define ROW_Y0 70
#define ROW_HEIGHT 42

#define CAPS_TEXT_MAX 1024
#define FILE_READ_MAX (64 * 1024)
/* Apps without a .caps file */
#define DEFAULT_CAPS "window\n"

#define LOG_HOST(...) printf("WASM HOST|INFO: " __VA_ARGS__)
#define LOG_HOST_ERR(...) printf("WASM HOST|ERROR: " __VA_ARGS__)
#define COLOUR_REFUSED GFX_RGB(0xd9, 0x4c, 0x3d)

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;

/* Used by the LionsOS libc and file system helpers */
serial_queue_handle_t serial_tx_queue_handle;
fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

static char libc_heap[0x40000];
static char host_stack[HOST_STACK_SIZE];
static co_control_t co_controller_mem;
static microkit_cothread_sem_t wake;
static bool gui_pending, tick_pending;

/*
 * WAMR's own pool allocator, from core/shared/mem-alloc/mem_alloc.h (which
 * cannot be included without WAMR's internal platform headers)
 */
typedef void *mem_allocator_t;
mem_allocator_t mem_allocator_create(void *mem, uint32_t size);
void *mem_allocator_malloc(mem_allocator_t allocator, uint32_t size);
void *mem_allocator_realloc(mem_allocator_t allocator, void *ptr, uint32_t size);
void mem_allocator_free(mem_allocator_t allocator, void *ptr);

static char runtime_pool[RUNTIME_POOL_SIZE];
static mem_allocator_t pool_allocator;
static uint8_t wasm_buf[WASM_MAX_SIZE];

/* The app list */
static char app_names[MAX_APPS][NAME_MAX_LEN];
/* What each app would be granted, shown before it is run */
static char app_grants[MAX_APPS][48];
static int app_refused[MAX_APPS];
static int num_apps;
static char status[64];
static bool status_is_error;

/* The running app, if any */
static bool running;
static char running_name[NAME_MAX_LEN];
static wasm_module_t module;
static wasm_module_inst_t instance;
static wasm_exec_env_t exec_env;
static wasm_function_inst_t fn_event, fn_tick;
static uint32_t tick_ms;
static bool exit_requested;
static gfx_rect_t damage;
static bool damaged;
static caps_table_t caps;
static char caps_text[CAPS_TEXT_MAX + 1];

/* File system access over the LionsOS FS protocol */

static void blocking_wait(microkit_channel ch)
{
    microkit_cothread_wait_on_channel(ch);
}

static bool fs_path_command(uint64_t type, const char *path, uint64_t flags, fs_cmpl_t *cmpl)
{
    ptrdiff_t buf;
    if (fs_buffer_allocate(&buf)) {
        return false;
    }
    size_t len = strlen(path);
    memcpy(fs_buffer_ptr(buf), path, len);

    fs_cmd_t cmd = { .type = type };
    if (type == FS_CMD_DIR_OPEN) {
        cmd.params.dir_open.path = (fs_buffer_t) { .offset = buf, .size = len };
    } else {
        cmd.params.file_open.path = (fs_buffer_t) { .offset = buf, .size = len };
        cmd.params.file_open.flags = flags;
    }
    int err = fs_command_blocking(cmpl, cmd);
    fs_buffer_free(buf);
    return err == 0 && cmpl->status == FS_STATUS_SUCCESS;
}

static bool has_wasm_suffix(const char *name, size_t len)
{
    const char *suffix = ".wasm";
    if (len <= 5) {
        return false;
    }
    for (int i = 0; i < 5; i++) {
        char c = name[len - 5 + i];
        if (c >= 'A' && c <= 'Z') {
            c += 'a' - 'A';
        }
        if (c != suffix[i]) {
            return false;
        }
    }
    return true;
}

static bool file_size(const char *path, uint64_t *size)
{
    fs_cmpl_t cmpl;
    if (!fs_path_command(FS_CMD_FILE_OPEN, path, FS_OPEN_FLAGS_READ_ONLY, &cmpl)) {
        return false;
    }
    uint64_t fd = cmpl.data.file_open.fd;
    bool ok = fs_command_blocking(&cmpl, (fs_cmd_t) { .type = FS_CMD_FILE_SIZE, .params.file_size.fd = fd }) == 0
              && cmpl.status == FS_STATUS_SUCCESS;
    if (ok) {
        *size = cmpl.data.file_size.size;
    }
    fs_command_blocking(&cmpl, (fs_cmd_t) { .type = FS_CMD_FILE_CLOSE, .params.file_close.fd = fd });
    return ok;
}

/* Read up to `max` bytes at `offset` into `dst`; returns the number read or -1 */
static int64_t read_file(const char *path, uint64_t offset, uint8_t *dst, uint64_t max)
{
    fs_cmpl_t cmpl;
    if (!fs_path_command(FS_CMD_FILE_OPEN, path, FS_OPEN_FLAGS_READ_ONLY, &cmpl)) {
        return -1;
    }
    uint64_t fd = cmpl.data.file_open.fd;

    int64_t total = 0;
    ptrdiff_t buf;
    if (fs_buffer_allocate(&buf) != 0) {
        total = -1;
    } else {
        while ((uint64_t)total < max) {
            uint64_t chunk = MIN((uint64_t)FS_BUFFER_SIZE, max - total);
            int err = fs_command_blocking(&cmpl, (fs_cmd_t) {
                .type = FS_CMD_FILE_READ,
                .params.file_read = { .fd = fd, .offset = offset + total, .buf = { .offset = buf, .size = chunk } },
            });
            if (err || cmpl.status != FS_STATUS_SUCCESS) {
                total = -1;
                break;
            }
            uint64_t n = MIN(cmpl.data.file_read.len_read, chunk);
            if (n == 0) {
                break;
            }
            memcpy(dst + total, fs_buffer_ptr(buf), n);
            total += n;
        }
        fs_buffer_free(buf);
    }
    fs_command_blocking(&cmpl, (fs_cmd_t) { .type = FS_CMD_FILE_CLOSE, .params.file_close.fd = fd });
    return total;
}

/* Name of app `index` without ".wasm" */
static void app_base_name(int index, char *buf)
{
    size_t len = strlen(app_names[index]) - 5;
    memcpy(buf, app_names[index], len);
    buf[len] = '\0';
}

/* Read the capability list of app `index` into caps_text; returns its length */
static size_t read_caps_text(int index)
{
    char base[NAME_MAX_LEN];
    char path[sizeof(APPS_DIR) + NAME_MAX_LEN + 8];
    app_base_name(index, base);
    snprintf(path, sizeof(path), "%s/%s.caps", APPS_DIR, base);

    int64_t n = read_file(path, 0, (uint8_t *)caps_text, CAPS_TEXT_MAX);
    if (n < 0) {
        strcpy(caps_text, DEFAULT_CAPS);
        return strlen(caps_text);
    }
    caps_text[n] = '\0';
    return n;
}

static void audit(const char *app, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("AUDIT|%s: ", app);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

static void scan_apps(void)
{
    num_apps = 0;
    fs_cmpl_t cmpl;
    if (!fs_path_command(FS_CMD_DIR_OPEN, APPS_DIR, 0, &cmpl)) {
        snprintf(status, sizeof(status), "No %s directory on the disk", APPS_DIR);
        status_is_error = true;
        return;
    }
    uint64_t dir = cmpl.data.dir_open.fd;

    ptrdiff_t buf;
    if (fs_buffer_allocate(&buf) == 0) {
        while (num_apps < MAX_APPS) {
            int err = fs_command_blocking(&cmpl, (fs_cmd_t) {
                .type = FS_CMD_DIR_READ,
                .params.dir_read = { .fd = dir, .buf = { .offset = buf, .size = FS_BUFFER_SIZE } },
            });
            if (err || cmpl.status != FS_STATUS_SUCCESS) {
                break;
            }
            size_t len = MIN(cmpl.data.dir_read.path_len, (uint64_t)FS_BUFFER_SIZE);
            const char *name = fs_buffer_ptr(buf);
            if (!has_wasm_suffix(name, len) || len >= NAME_MAX_LEN) {
                continue;
            }
            memcpy(app_names[num_apps], name, len);
            app_names[num_apps][len] = '\0';
            num_apps++;
        }
        fs_buffer_free(buf);
    }
    fs_command_blocking(&cmpl, (fs_cmd_t) { .type = FS_CMD_DIR_CLOSE, .params.dir_close.fd = dir });

    /* Preview what each app would be granted, without auditing */
    for (int i = 0; i < num_apps; i++) {
        caps_table_t preview;
        char base[NAME_MAX_LEN];
        app_base_name(i, base);
        size_t len = read_caps_text(i);
        caps_grant(&preview, base, caps_text, len, true);
        caps_summary(&preview, app_grants[i], sizeof(app_grants[i]));
        app_refused[i] = preview.num_denied;
    }
    LOG_HOST("found %d app(s) in %s\n", num_apps, APPS_DIR);
}

/* Read a whole module into wasm_buf, returning its size or 0 on failure */
static uint32_t load_file(const char *path)
{
    uint64_t size;
    if (!file_size(path, &size) || size == 0 || size > WASM_MAX_SIZE) {
        return 0;
    }
    int64_t n = read_file(path, 0, wasm_buf, size);
    return n == (int64_t)size ? (uint32_t)n : 0;
}

/* Drawing helpers for the host's own list view */

static gfx_rect_t row_rect(int i)
{
    return (gfx_rect_t) { 16, ROW_Y0 + i * ROW_HEIGHT, WIDTH - 32, ROW_HEIGHT - 6 };
}

static void draw_list(void)
{
    gfx_surface_t *s = gui_app_surface();
    gfx_fill_rect(s, (gfx_rect_t) { 0, 0, WIDTH, HEIGHT }, COLOUR_BG);
    gfx_draw_text(s, 16, 14, "WebAssembly apps", 2, COLOUR_ACCENT);
    gfx_draw_text(s, 16, 38, "From " APPS_DIR " on the FAT disk", 2, COLOUR_MUTED);

    for (int i = 0; i < num_apps; i++) {
        gfx_rect_t r = row_rect(i);
        if (r.y + r.height > HEIGHT - 50) {
            break;
        }
        gfx_fill_rect(s, r, COLOUR_ROW);
        char name[NAME_MAX_LEN];
        app_base_name(i, name);
        gfx_draw_text(s, r.x + 10, r.y + 5, name, 2, COLOUR_TEXT);

        /* What the app will be allowed to do, and how many requests the policy refuses */
        char grants[64];
        snprintf(grants, sizeof(grants), "may use: %s", app_grants[i]);
        int32_t x = gfx_draw_text(s, r.x + 10, r.y + 25, grants, 1, COLOUR_MUTED);
        if (app_refused[i]) {
            char refused[24];
            snprintf(refused, sizeof(refused), "  (%d refused)", app_refused[i]);
            gfx_draw_text(s, x, r.y + 25, refused, 1, COLOUR_REFUSED);
        }
    }

    const char *hint = status[0] ? status : "Click to run, Esc to return";
    gfx_draw_text(s, 16, HEIGHT - 30, hint, 2, status_is_error ? COLOUR_ERROR : COLOUR_MUTED);
    gui_app_commit_all();
}

/* Native functions imported by apps from the "lions" module */

static void add_damage(int32_t x, int32_t y, int32_t w, int32_t h)
{
    gfx_rect_t r = { x, y, w, h };
    int32_t x0 = MAX(r.x, 0), y0 = MAX(r.y, 0);
    int32_t x1 = MIN((int64_t)r.x + r.width, WIDTH), y1 = MIN((int64_t)r.y + r.height, HEIGHT);
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

static gfx_surface_t *window(int32_t handle, const char *op)
{
    return caps_check(&caps, running_name, handle, CAP_WINDOW, op) ? gui_app_surface() : NULL;
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

/* `pixels` holds w * h 0x00RRGGBB values; WAMR checked that it spans `len` bytes */
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
    return window(win, "width") ? WIDTH : -1;
}

static int32_t n_height(wasm_exec_env_t env, int32_t win)
{
    (void)env;
    return window(win, "height") ? HEIGHT : -1;
}

static int32_t n_set_title(wasm_exec_env_t env, int32_t win, const char *title)
{
    (void)env;
    if (!window(win, "set_title")) {
        return -1;
    }
    gui_app_set_title(title);
    return 0;
}

static int32_t n_timer_start(wasm_exec_env_t env, int32_t timer, int32_t ms)
{
    (void)env;
    if (!caps_check(&caps, running_name, timer, CAP_TIMER, "timer_start")) {
        return -1;
    }
    bool was_ticking = tick_ms != 0;
    tick_ms = ms <= 0 ? 0 : MAX(ms, MIN_TICK_MS);
    if (tick_ms && !was_ticking) {
        sddf_timer_set_timeout(timer_config.driver_id, (uint64_t)tick_ms * NS_IN_MS);
    }
    return 0;
}

static int32_t now_ms(void)
{
    return (int32_t)(sddf_timer_time_now(timer_config.driver_id) / NS_IN_MS);
}

static int32_t n_timer_now(wasm_exec_env_t env, int32_t timer)
{
    (void)env;
    return caps_check(&caps, running_name, timer, CAP_TIMER, "timer_now") ? now_ms() : -1;
}

static int32_t n_console_log(wasm_exec_env_t env, int32_t console, const char *text)
{
    (void)env;
    if (!caps_check(&caps, running_name, console, CAP_CONSOLE, "console_log")) {
        return -1;
    }
    printf("%s: %s\n", running_name, text);
    return 0;
}

static int32_t n_file_size(wasm_exec_env_t env, int32_t file)
{
    (void)env;
    cap_t *cap = caps_check(&caps, running_name, file, CAP_FILE, "file_size");
    return cap ? (int32_t)MIN(cap->size, (uint64_t)INT32_MAX) : -1;
}

/* WAMR checked that `buf` spans `len` bytes of the app's memory */
static int32_t n_file_read(wasm_exec_env_t env, int32_t file, uint32_t offset, uint8_t *buf, uint32_t len)
{
    (void)env;
    cap_t *cap = caps_check(&caps, running_name, file, CAP_FILE, "file_read");
    if (!cap) {
        return -1;
    }
    if (offset >= cap->size) {
        return 0;
    }
    uint64_t n = MIN(MIN((uint64_t)len, cap->size - offset), (uint64_t)FILE_READ_MAX);
    return (int32_t)read_file(cap->path, offset, buf, n);
}

static int32_t n_cap_lookup(wasm_exec_env_t env, const char *name)
{
    (void)env;
    return caps_lookup(&caps, name);
}

static int32_t n_cap_drop(wasm_exec_env_t env, int32_t handle)
{
    (void)env;
    int err = caps_drop(&caps, running_name, handle);
    if (!err && !caps_have(&caps, CAP_TIMER)) {
        tick_ms = 0;
    }
    return err;
}

/* An app may always end itself */
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
 * WAMR allocations, built with WAMR_BUILD_ALLOC_WITH_USAGE.
 *
 * Linear memory must start out zeroed: modules rely on it for their .bss,
 * and WAMR expects it from mmap. The pool reuses memory, so zero linear
 * memory here, including the part added when it grows, for which we track
 * the size of each linear memory block.
 */

#define MAX_LINEAR_MEMORIES 4

static struct {
    void *ptr;
    uint32_t size;
} linear_memories[MAX_LINEAR_MEMORIES];

static int linear_memory_slot(void *ptr)
{
    for (int i = 0; i < MAX_LINEAR_MEMORIES; i++) {
        if (linear_memories[i].ptr == ptr) {
            return i;
        }
    }
    return -1;
}

static void *pool_malloc(mem_alloc_usage_t usage, unsigned int size)
{
    if (usage != Alloc_For_LinearMemory) {
        return mem_allocator_malloc(pool_allocator, size);
    }

    int slot = linear_memory_slot(NULL);
    if (slot < 0) {
        return NULL;
    }
    void *ptr = mem_allocator_malloc(pool_allocator, size);
    if (ptr) {
        memset(ptr, 0, size);
        linear_memories[slot].ptr = ptr;
        linear_memories[slot].size = size;
    }
    return ptr;
}

static void *pool_realloc(mem_alloc_usage_t usage, bool full_size_mmaped, void *ptr, unsigned int size)
{
    (void)full_size_mmaped;
    if (usage != Alloc_For_LinearMemory) {
        return mem_allocator_realloc(pool_allocator, ptr, size);
    }

    int slot = linear_memory_slot(ptr);
    if (slot < 0) {
        return NULL;
    }
    uint32_t old_size = linear_memories[slot].size;
    uint8_t *new_ptr = mem_allocator_realloc(pool_allocator, ptr, size);
    if (new_ptr) {
        if (size > old_size) {
            memset(new_ptr + old_size, 0, size - old_size);
        }
        linear_memories[slot].ptr = new_ptr;
        linear_memories[slot].size = size;
    }
    return new_ptr;
}

static void pool_free(mem_alloc_usage_t usage, void *ptr)
{
    if (usage == Alloc_For_LinearMemory) {
        int slot = linear_memory_slot(ptr);
        if (slot >= 0) {
            linear_memories[slot].ptr = NULL;
        }
    }
    mem_allocator_free(pool_allocator, ptr);
}

/* Running apps */

static void stop_app(const char *reason, bool is_error)
{
    if (exec_env) {
        wasm_runtime_destroy_exec_env(exec_env);
    }
    if (instance) {
        wasm_runtime_deinstantiate(instance);
    }
    if (module) {
        wasm_runtime_unload(module);
    }
    exec_env = NULL;
    instance = NULL;
    module = NULL;
    running = false;
    caps_revoke_all(&caps, running_name);
    tick_ms = 0;
    damaged = false;

    LOG_HOST("%s: %s\n", running_name, reason);
    snprintf(status, sizeof(status), "%s: %s", running_name, reason);
    status_is_error = is_error;
    gui_app_set_title("Apps");
    draw_list();
}

/* Call an app export, then publish what it drew. Returns false if the app stopped. */
static bool call_app(wasm_function_inst_t fn, uint32_t argc, uint32_t *argv)
{
    if (fn && !wasm_runtime_call_wasm(exec_env, fn, argc, argv)) {
        const char *exception = wasm_runtime_get_exception(instance);
        /* Print where the app trapped (function names are kept in the module) */
        wasm_runtime_dump_call_stack(exec_env);
        char reason[48];
        snprintf(reason, sizeof(reason), "%s", exception ? exception : "trapped");
        stop_app(reason, true);
        return false;
    }
    if (exit_requested) {
        exit_requested = false;
        stop_app("exited", false);
        return false;
    }
    if (damaged) {
        gui_app_commit(damage);
        damaged = false;
    }
    return true;
}

static void start_app(int index)
{
    char path[sizeof(APPS_DIR) + NAME_MAX_LEN + 1];
    snprintf(path, sizeof(path), "%s/%s", APPS_DIR, app_names[index]);
    app_base_name(index, running_name);

    LOG_HOST("loading %s\n", path);
    uint32_t size = load_file(path);
    if (size == 0) {
        snprintf(status, sizeof(status), "Could not read %s", app_names[index]);
        status_is_error = true;
        draw_list();
        return;
    }

    char error[128];
    module = wasm_runtime_load(wasm_buf, size, error, sizeof(error));
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
        LOG_HOST_ERR("%s: %s\n", running_name, error);
        error[40] = '\0';
        running = true;
        stop_app(error, true);
        return;
    }

    running = true;
    exit_requested = false;
    status[0] = '\0';
    gui_app_set_title(running_name);

    /* Grant exactly what the app's .caps file lists and the policy allows */
    size_t caps_len = read_caps_text(index);
    caps_grant(&caps, running_name, caps_text, caps_len, false);

    fn_event = wasm_runtime_lookup_function(instance, "app_event");
    fn_tick = wasm_runtime_lookup_function(instance, "app_tick");
    wasm_function_inst_t fn_init = wasm_runtime_lookup_function(instance, "app_init");

    /* Start from a blank window in case the app draws only part of it */
    gfx_fill_rect(gui_app_surface(), (gfx_rect_t) { 0, 0, WIDTH, HEIGHT }, COLOUR_BG);
    add_damage(0, 0, WIDTH, HEIGHT);
    LOG_HOST("running %s (%u bytes)\n", running_name, size);
    bool has_window = caps_have(&caps, CAP_WINDOW);
    uint32_t argv[2] = { has_window ? WIDTH : 0, has_window ? HEIGHT : 0 };
    call_app(fn_init, 2, argv);
}

/* Event handling */

static void list_event(gui_event_t *ev)
{
    if (ev->type != GUI_EV_POINTER_BUTTON || ev->code != INPUT_BTN_LEFT || ev->value != INPUT_KEY_PRESSED) {
        return;
    }
    for (int i = 0; i < num_apps; i++) {
        if (gfx_rect_contains(row_rect(i), ev->x, ev->y)) {
            start_app(i);
            return;
        }
    }
}

static void handle_gui_events(void)
{
    gui_event_t ev;
    while (gui_app_next_event(&ev)) {
        char c = gui_app_key_to_ascii(&ev);
        if (!running) {
            list_event(&ev);
            continue;
        }
        if (ev.type == GUI_EV_KEY && ev.code == INPUT_KEY_ESC && ev.value == INPUT_KEY_PRESSED) {
            stop_app("closed with Esc", false);
            continue;
        }
        /* Input belongs to the window, so only an app holding it receives input */
        if (!caps_have(&caps, CAP_WINDOW)) {
            continue;
        }
        uint32_t argv[5] = { ev.type, ev.code, (uint32_t)ev.value, (uint32_t)ev.x, (uint32_t)ev.y };
        if (ev.type == GUI_EV_KEY) {
            argv[3] = (uint8_t)c;
            argv[4] = 0;
        }
        if (!call_app(fn_event, 5, argv)) {
            continue;
        }
    }
}

static void handle_tick(void)
{
    if (!running || tick_ms == 0 || !caps_have(&caps, CAP_TIMER)) {
        return;
    }
    uint32_t argv[1] = { (uint32_t)now_ms() };
    if (call_app(fn_tick, 1, argv) && tick_ms) {
        sddf_timer_set_timeout(timer_config.driver_id, (uint64_t)tick_ms * NS_IN_MS);
    }
}

static void host_main(void)
{
    libc_init(NULL, libc_heap, sizeof(libc_heap));

    if (!gui_app_init("Apps", WIDTH, HEIGHT)) {
        return;
    }
    snprintf(status, sizeof(status), "Mounting the file system...");
    draw_list();
    status[0] = '\0';

    pool_allocator = mem_allocator_create(runtime_pool, sizeof(runtime_pool));
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
        snprintf(status, sizeof(status), "Could not start the WebAssembly runtime");
        status_is_error = true;
        draw_list();
        return;
    }

    caps_init(file_size, audit);
    fs_set_blocking_wait(blocking_wait);
    fs_command_queue = fs_config.server.command_queue.vaddr;
    fs_completion_queue = fs_config.server.completion_queue.vaddr;
    fs_share = fs_config.server.share.vaddr;
    fs_cmpl_t cmpl;
    if (fs_command_blocking(&cmpl, (fs_cmd_t) { .type = FS_CMD_INITIALISE }) || cmpl.status != FS_STATUS_SUCCESS) {
        snprintf(status, sizeof(status), "Could not mount the file system");
        status_is_error = true;
        draw_list();
        return;
    }

    scan_apps();
    draw_list();

    for (;;) {
        microkit_cothread_semaphore_wait(&wake);
        if (gui_pending) {
            gui_pending = false;
            handle_gui_events();
        }
        if (tick_pending) {
            tick_pending = false;
            handle_tick();
        }
    }
}

void init(void)
{
    assert(serial_config_check_magic(&serial_config));
    assert(timer_config_check_magic(&timer_config));
    assert(fs_config_check_magic(&fs_config));

    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size,
                      serial_config.tx.data.vaddr);

    stack_ptrs_arg_array_t stacks = { (uintptr_t)host_stack };
    microkit_cothread_init(&co_controller_mem, HOST_STACK_SIZE, stacks);
    microkit_cothread_semaphore_init(&wake);
    if (microkit_cothread_spawn((client_entry_t)host_main, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        assert(false);
    }
    microkit_cothread_yield();
}

void notified(microkit_channel ch)
{
    if (ch == GUI_COMPOSITOR_CH) {
        gui_pending = true;
    } else if (ch == timer_config.driver_id) {
        tick_pending = true;
    }

    fs_process_completions(NULL);
    microkit_cothread_recv_ntfn(ch);

    if (gui_pending || tick_pending) {
        microkit_cothread_semaphore_signal(&wake);
    }
}
