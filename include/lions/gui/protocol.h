/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * The protocol between the compositor and GUI applications.
 *
 * Every application slot is a set of shared regions plus one channel:
 *
 *   surface  The window's content as 32-bit BGRA pixels (0xAARRGGBB),
 *            stride == width. Writable by the app, read-only for the
 *            compositor.
 *   state    A gui_state_t page. Writable by the app, read-only for the
 *            compositor.
 *   events   A gui_event_queue_t page carrying input from the compositor to
 *            the app. The compositor produces, the app consumes.
 *
 * To show something the app draws into its surface, records the damaged
 * rectangle in its state page with gui_state_commit(), and notifies the
 * compositor. The compositor notifies the app when it has pushed events.
 *
 * A slot has a window while its state page holds GUI_STATE_MAGIC and a valid
 * size. An app withdraws its window by clearing the magic and committing.
 *
 * The compositor treats everything in the app's regions as untrusted: sizes,
 * titles and damage rectangles are validated and clamped before use, and a
 * misbehaving app can only affect the pixels of its own window.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#define GUI_STATE_MAGIC 0x4c475549 /* "LGUI" */
#define GUI_TITLE_MAX 32

typedef struct gui_rect {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} gui_rect_t;

typedef struct gui_state {
    uint32_t magic;
    /* Size of the content in pixels, 0 until the app has drawn something */
    uint32_t width;
    uint32_t height;
    /* NUL-terminated, the compositor copies at most GUI_TITLE_MAX - 1 bytes */
    char title[GUI_TITLE_MAX];
    /*
     * Incremented after every commit. `damage` is the area changed by the
     * latest commit, in content coordinates. The compositor reads `seq` before
     * and after `damage`, and redraws the whole window if it changed or if
     * it skipped any commits.
     */
    uint32_t seq;
    gui_rect_t damage;
} gui_state_t;

/* Event types */
#define GUI_EV_POINTER_MOTION 1 /* x, y */
#define GUI_EV_POINTER_BUTTON 2 /* code: INPUT_BTN_*, value: INPUT_KEY_PRESSED/RELEASED, x, y */
#define GUI_EV_KEY 3            /* code: INPUT_KEY_*, value: INPUT_KEY_PRESSED/RELEASED/REPEATED */
#define GUI_EV_FOCUS 4          /* value: 1 if the window gained focus, 0 if it lost it */
#define GUI_EV_CLOSE 5          /* the user closed the window; it can be reopened from the launcher */

/* Pointer coordinates are relative to the top left of the content and may lie outside it during a grab */
typedef struct gui_event {
    uint16_t type;
    uint16_t code;
    int32_t value;
    int32_t x;
    int32_t y;
} gui_event_t;

typedef struct gui_event_queue {
    /* Index of the next event to consume, only written by the app */
    uint32_t head;
    /* Index of the next event to produce, only written by the compositor */
    uint32_t tail;
    uint32_t dropped;
    uint32_t _reserved;
    gui_event_t events[];
} gui_event_queue_t;

#define GUI_EVENT_QUEUE_CAPACITY(region_size) (((region_size) - sizeof(gui_event_queue_t)) / sizeof(gui_event_t))

static inline void gui_state_commit(gui_state_t *state, gui_rect_t damage)
{
    state->damage = damage;
    __atomic_store_n(&state->seq, state->seq + 1, __ATOMIC_RELEASE);
}

/*
 * Producer side. The whole queue page is writable by the app, so the
 * compositor cannot trust `head` or `tail`. Indexing modulo the capacity keeps
 * every access inside the page, so a misbehaving app can only lose or corrupt
 * its own events.
 */
static inline int gui_event_enqueue(gui_event_queue_t *q, uint32_t capacity, gui_event_t ev)
{
    uint32_t tail = q->tail;
    uint32_t used = tail - __atomic_load_n(&q->head, __ATOMIC_ACQUIRE);
    if (used >= capacity) {
        q->dropped++;
        return -1;
    }
    q->events[tail % capacity] = ev;
    __atomic_store_n(&q->tail, tail + 1, __ATOMIC_RELEASE);
    return 0;
}

/* Consumer side */
static inline int gui_event_dequeue(gui_event_queue_t *q, uint32_t capacity, gui_event_t *ev)
{
    uint32_t head = q->head;
    if (__atomic_load_n(&q->tail, __ATOMIC_ACQUIRE) == head) {
        return -1;
    }
    *ev = q->events[head % capacity];
    __atomic_store_n(&q->head, head + 1, __ATOMIC_RELEASE);
    return 0;
}
