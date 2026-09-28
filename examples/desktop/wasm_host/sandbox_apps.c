/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Apps running in sandboxes, each in a window of its own (SANDBOX=1).
 *
 * Sandbox k is paired with window slot GUI_FIRST_WASM_WINDOW + k of the
 * compositor. The host maps that slot's state and events and speaks for it
 * on channel SANDBOX_WINDOW_CH_BASE + k: it publishes the window when an
 * app starts and withdraws it when the app stops, forwards the window's
 * input to the sandbox, and tells the compositor when the app has
 * committed. The app draws into the surface and state itself, if it holds
 * the window capability, through the frames mapped into its sandbox.
 *
 * Esc in an app's window, or closing it, stops the app. So do its exit or
 * trap, and a fault the kernel reports in its sandbox.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <microkit.h>
#include <sddf/util/util.h>
#include <lions/input/input.h>
#include <gui_config.h>
#include "../src/keymap.h"
#include "wasm_host.h"
#include "caps.h"
#include "sandbox_host.h"
#include "sandbox_apps.h"

/* The state, events and surface of each sandbox's window, as meta.py maps them */
uintptr_t wasm_surfaces;
uintptr_t wasm_states;
uintptr_t wasm_events;

typedef struct app {
    bool running;
    char name[NAME_MAX_LEN];
    caps_table_t caps;
    uint32_t tick_ms;
    int32_t next_tick_ms;
    /* Shift keys held in its window, for typed characters */
    bool shift_left, shift_right;
    bool fault;
    char fault_reason[48];
} app_t;

static app_t apps[SANDBOX_COUNT];
/* Windows with input, and sandboxes with requests, since the last sandbox_apps_process() */
static uint32_t windows_pending, sandboxes_pending;

static gui_state_t *window_state(int k)
{
    return (gui_state_t *)(wasm_states + k * GUI_STATE_REGION_SIZE);
}

static gui_event_queue_t *window_events(int k)
{
    return (gui_event_queue_t *)(wasm_events + k * GUI_EVENTS_REGION_SIZE);
}

static void notify_compositor(int k)
{
    microkit_notify(SANDBOX_WINDOW_CH_BASE + k);
}

/* Show a new window for app k, blank, or saying why it is empty if the app may not draw */
static void publish_window(int k)
{
    app_t *a = &apps[k];
    gfx_surface_t s = {
        .pixels = (uint32_t *)(wasm_surfaces + k * GUI_SURFACE_REGION_SIZE),
        .width = WIDTH,
        .height = HEIGHT,
        .stride = WIDTH,
    };
    gfx_reset_clip(&s);
    gfx_fill_rect(&s, (gfx_rect_t) { 0, 0, WIDTH, HEIGHT }, COLOUR_BG);
    if (!caps_have(&a->caps, CAP_WINDOW)) {
        gfx_draw_text(&s, 16, 16, a->name, 2, COLOUR_ACCENT);
        gfx_draw_text(&s, 16, 48, "runs without a window", 2, COLOUR_TEXT);
        gfx_draw_text(&s, 16, 72, "capability, so it cannot", 2, COLOUR_TEXT);
        gfx_draw_text(&s, 16, 96, "draw here.", 2, COLOUR_TEXT);
        gfx_draw_text(&s, 16, HEIGHT - 30, "Esc stops it", 2, COLOUR_MUTED);
    }

    /* Drop input left over from the window's previous app */
    gui_event_queue_t *q = window_events(k);
    __atomic_store_n(&q->head, __atomic_load_n(&q->tail, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);

    gui_state_t *state = window_state(k);
    state->magic = GUI_STATE_MAGIC;
    state->width = WIDTH;
    state->height = HEIGHT;
    strncpy(state->title, a->name, GUI_TITLE_MAX - 1);
    state->title[GUI_TITLE_MAX - 1] = '\0';
    gui_state_commit(state, (gui_rect_t) { 0, 0, WIDTH, HEIGHT });
    notify_compositor(k);
}

static void withdraw_window(int k)
{
    gui_state_t *state = window_state(k);
    state->magic = 0;
    state->width = 0;
    state->height = 0;
    gui_state_commit(state, (gui_rect_t) { 0, 0, 0, 0 });
    notify_compositor(k);
}

/* Ask the timer for the earliest tick any app is due */
static void schedule_ticks(void)
{
    int32_t now = now_ms();
    int32_t delay = INT32_MAX;
    for (int k = 0; k < SANDBOX_COUNT; k++) {
        if (apps[k].running && apps[k].tick_ms) {
            delay = MIN(delay, MAX(apps[k].next_tick_ms - now, 1));
        }
    }
    if (delay != INT32_MAX) {
        host_set_timeout_ms(delay);
    }
}

static void stop(int k, const char *reason, bool is_error)
{
    app_t *a = &apps[k];
    if (!a->running) {
        return;
    }
    sandbox_stop(k);
    caps_revoke_all(&a->caps, a->name);
    a->running = false;
    a->tick_ms = 0;
    /* A fault the sandbox reported before it stopped is about this app, not the next one */
    a->fault = false;
    withdraw_window(k);
    LOG_HOST("%s: %s\n", a->name, reason);
    host_status(is_error, "%s: %s", a->name, reason);
}

bool sandbox_apps_init(void)
{
    return sandbox_init();
}

void sandbox_apps_launch(const char *name, const char *path, const char *caps_text, size_t caps_len)
{
    int k = 0;
    while (k < SANDBOX_COUNT && apps[k].running) {
        k++;
    }
    if (k == SANDBOX_COUNT) {
        host_status(true, "All %d sandboxes are in use", SANDBOX_COUNT);
        return;
    }
    uint64_t size;
    if (!file_size(path, &size)) {
        host_status(true, "Could not read %s", path);
        return;
    }

    app_t *a = &apps[k];
    memset(a, 0, sizeof(*a));
    strncpy(a->name, name, NAME_MAX_LEN - 1);
    /* Grant exactly what the app's .caps file lists and the policy allows */
    caps_grant(&a->caps, a->name, caps_text, caps_len, false);
    a->running = true;
    publish_window(k);

    LOG_HOST("running %s (%lu bytes) in sandbox %d\n", a->name, (unsigned long)size, k);
    char error[64];
    if (!sandbox_start(k, a->name, path, size, &a->caps, WIDTH, HEIGHT, read_file, error, sizeof(error))) {
        stop(k, error, true);
        return;
    }
    host_status(false, "%s is running", a->name);
}

int sandbox_apps_count_running(const char *name)
{
    int n = 0;
    for (int k = 0; k < SANDBOX_COUNT; k++) {
        n += apps[k].running && strcmp(apps[k].name, name) == 0;
    }
    return n;
}

/* Typed character of a key event, tracking shift in the app's window */
static char key_to_ascii(app_t *a, gui_event_t *ev)
{
    bool down = ev->value != INPUT_KEY_RELEASED;
    if (ev->code == INPUT_KEY_LEFTSHIFT) {
        a->shift_left = down;
        return 0;
    }
    if (ev->code == INPUT_KEY_RIGHTSHIFT) {
        a->shift_right = down;
        return 0;
    }
    return down ? keymap_to_ascii(ev->code, a->shift_left || a->shift_right) : 0;
}

/* Input for app k's window */
static void handle_window(int k)
{
    app_t *a = &apps[k];
    gui_event_queue_t *q = window_events(k);
    gui_event_t ev;
    while (gui_event_dequeue(q, GUI_EVENT_QUEUE_CAPACITY(GUI_EVENTS_REGION_SIZE), &ev) == 0) {
        if (!a->running) {
            continue;
        }
        if (ev.type == GUI_EV_CLOSE) {
            stop(k, "window closed", false);
            continue;
        }
        char c = 0;
        if (ev.type == GUI_EV_KEY) {
            if (ev.code == INPUT_KEY_ESC && ev.value == INPUT_KEY_PRESSED) {
                stop(k, "closed with Esc", false);
                continue;
            }
            c = key_to_ascii(a, &ev);
            ev.x = (uint8_t)c;
            ev.y = 0;
        }
        /* Input belongs to the window, so only an app holding it receives input */
        if (caps_have(&a->caps, CAP_WINDOW)) {
            sandbox_post_event(k, ev);
        }
    }
    if (a->running) {
        sandbox_kick(k);
    }
}

/* Requests from app k. Services check the grant themselves. */
static void handle_requests(int k)
{
    app_t *a = &apps[k];
    sandbox_req_t req;
    while (a->running && sandbox_next_request(k, &req)) {
        switch (req.type) {
        case SANDBOX_REQ_COMMIT:
            if (caps_have(&a->caps, CAP_WINDOW)) {
                notify_compositor(k);
            }
            break;
        case SANDBOX_REQ_LOG:
            if (caps_check(&a->caps, a->name, req.arg, CAP_CONSOLE, "console_log")) {
                printf("%s: %s\n", a->name, req.text);
            }
            break;
        case SANDBOX_REQ_TIMER:
            if (caps_have(&a->caps, CAP_TIMER)) {
                a->tick_ms = req.arg <= 0 ? 0 : MAX(req.arg, MIN_TICK_MS);
                a->next_tick_ms = now_ms() + a->tick_ms;
                schedule_ticks();
            } else {
                audit(a->name, "denied timer_start (no timer capability)");
            }
            break;
        case SANDBOX_REQ_DROP:
            if (req.arg >= 0 && req.arg < CAPS_MAX && a->caps.caps[req.arg].live) {
                cap_type_t type = a->caps.caps[req.arg].type;
                if (caps_drop(&a->caps, a->name, req.arg) == 0) {
                    sandbox_revoke_grant(k, req.arg, type);
                }
                if (!caps_have(&a->caps, CAP_TIMER)) {
                    a->tick_ms = 0;
                }
            }
            break;
        case SANDBOX_REQ_EXIT:
            stop(k, "exited", false);
            break;
        case SANDBOX_REQ_TRAP:
            stop(k, req.text[0] ? req.text : "trapped", true);
            break;
        case SANDBOX_REQ_DEBUG:
            printf("%s|runtime: %s\n", a->name, req.text);
            break;
        case SANDBOX_REQ_DENIED:
            /* The runner refused a call; audit it as the in-PD host does */
            if (req.grant_type >= CAP_WINDOW && req.grant_type <= CAP_FILE) {
                caps_check(&a->caps, a->name, req.arg, req.grant_type, req.text);
            }
            break;
        default:
            break;
        }
    }
}

bool sandbox_apps_notified(microkit_channel ch)
{
    if (ch >= SANDBOX_WINDOW_CH_BASE && ch < SANDBOX_WINDOW_CH_BASE + SANDBOX_COUNT) {
        windows_pending |= BIT(ch - SANDBOX_WINDOW_CH_BASE);
        return true;
    }
    if (ch >= SANDBOX_HOST_CH_BASE && ch < SANDBOX_HOST_CH_BASE + SANDBOX_COUNT) {
        sandboxes_pending |= BIT(ch - SANDBOX_HOST_CH_BASE);
        return true;
    }
    return false;
}

void sandbox_apps_fault(microkit_child child, microkit_msginfo msginfo)
{
    if (child >= SANDBOX_COUNT || !apps[child].running) {
        return;
    }
    app_t *a = &apps[child];
    seL4_Word label = microkit_msginfo_get_label(msginfo);
    if (label == seL4_Fault_VMFault) {
        snprintf(a->fault_reason, sizeof(a->fault_reason), "VM fault at 0x%lx",
                 (unsigned long)seL4_GetMR(seL4_VMFault_Addr));
        LOG_HOST_ERR("%s: VM fault at 0x%lx, ip 0x%lx\n", a->name, (unsigned long)seL4_GetMR(seL4_VMFault_Addr),
                     (unsigned long)seL4_GetMR(seL4_VMFault_IP));
    } else {
        snprintf(a->fault_reason, sizeof(a->fault_reason), "fault %lu", (unsigned long)label);
        LOG_HOST_ERR("%s: fault with label %lu\n", a->name, (unsigned long)label);
    }
    a->fault = true;
}

void sandbox_apps_process(void)
{
    uint32_t windows = windows_pending, sandboxes = sandboxes_pending;
    windows_pending = 0;
    sandboxes_pending = 0;
    for (int k = 0; k < SANDBOX_COUNT; k++) {
        if (apps[k].fault) {
            stop(k, apps[k].fault_reason, true);
        }
        if (windows & BIT(k)) {
            handle_window(k);
        }
        if (sandboxes & BIT(k)) {
            handle_requests(k);
        }
    }
}

void sandbox_apps_tick(void)
{
    int32_t now = now_ms();
    for (int k = 0; k < SANDBOX_COUNT; k++) {
        app_t *a = &apps[k];
        if (a->running && a->tick_ms && now - a->next_tick_ms >= 0) {
            sandbox_post_tick(k, now);
            sandbox_kick(k);
            a->next_tick_ms = now + a->tick_ms;
        }
    }
    schedule_ticks();
}
