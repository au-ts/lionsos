/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * The input device class.
 *
 *   input driver(s)  ->  input virtualiser  ->  client(s)
 *
 * Input only flows one way, so each connection is a single-producer,
 * single-consumer ring of events in shared memory plus a notification
 * channel. The producer notifies the consumer after pushing events.
 *
 * Events follow Linux evdev semantics, which is also what virtIO input uses:
 * a device reports a group of EV_KEY/EV_REL/EV_ABS events followed by an
 * EV_SYN/SYN_REPORT that marks the group as complete.
 *
 * Absolute axes are normalised by the driver to [0, INPUT_ABS_MAX] so that
 * clients do not need to know the range of each device.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Event types */
#define INPUT_EV_SYN 0x00
#define INPUT_EV_KEY 0x01
#define INPUT_EV_REL 0x02
#define INPUT_EV_ABS 0x03

#define INPUT_SYN_REPORT 0

/* Relative and absolute axes */
#define INPUT_REL_X 0x00
#define INPUT_REL_Y 0x01
#define INPUT_REL_WHEEL 0x08
#define INPUT_ABS_X 0x00
#define INPUT_ABS_Y 0x01

#define INPUT_ABS_MAX 0xffff

/* Key values of EV_KEY events */
#define INPUT_KEY_RELEASED 0
#define INPUT_KEY_PRESSED 1
#define INPUT_KEY_REPEATED 2

/* Key codes used by LionsOS components, from linux/input-event-codes.h */
#define INPUT_KEY_ESC 1
#define INPUT_KEY_BACKSPACE 14
#define INPUT_KEY_TAB 15
#define INPUT_KEY_ENTER 28
#define INPUT_KEY_LEFTCTRL 29
#define INPUT_KEY_LEFTSHIFT 42
#define INPUT_KEY_RIGHTSHIFT 54
#define INPUT_KEY_LEFTALT 56
#define INPUT_KEY_SPACE 57
#define INPUT_KEY_CAPSLOCK 58
#define INPUT_KEY_RIGHTCTRL 97
#define INPUT_KEY_RIGHTALT 100
#define INPUT_KEY_MAX 0x2ff

#define INPUT_BTN_LEFT 0x110
#define INPUT_BTN_RIGHT 0x111
#define INPUT_BTN_MIDDLE 0x112
#define INPUT_BTN_TOUCH 0x14a

typedef struct input_event {
    uint16_t type;
    uint16_t code;
    int32_t value;
} input_event_t;

typedef struct input_queue {
    /* Index of the next event to consume, only written by the consumer */
    uint32_t head;
    /* Index of the next event to produce, only written by the producer */
    uint32_t tail;
    /* Number of events dropped because the queue was full */
    uint32_t dropped;
    uint32_t _reserved;
    input_event_t events[];
} input_queue_t;

typedef struct input_queue_handle {
    input_queue_t *queue;
    uint32_t capacity;
} input_queue_handle_t;

/* Number of events that fit in a queue region of `region_size` bytes */
#define INPUT_QUEUE_CAPACITY(region_size) (((region_size) - sizeof(input_queue_t)) / sizeof(input_event_t))

static inline void input_queue_init(input_queue_handle_t *h, input_queue_t *queue, uint32_t capacity)
{
    h->queue = queue;
    h->capacity = capacity;
}

static inline bool input_queue_empty(input_queue_handle_t *h)
{
    return __atomic_load_n(&h->queue->tail, __ATOMIC_ACQUIRE) == h->queue->head;
}

static inline bool input_queue_full(input_queue_handle_t *h)
{
    return h->queue->tail - __atomic_load_n(&h->queue->head, __ATOMIC_ACQUIRE) == h->capacity;
}

/*
 * Push an event. Returns 0 on success, or -1 if the queue is full, in which
 * case the event is dropped and counted.
 */
static inline int input_enqueue(input_queue_handle_t *h, input_event_t event)
{
    if (input_queue_full(h)) {
        h->queue->dropped++;
        return -1;
    }

    uint32_t tail = h->queue->tail;
    h->queue->events[tail % h->capacity] = event;
    __atomic_store_n(&h->queue->tail, tail + 1, __ATOMIC_RELEASE);
    return 0;
}

/* Pop an event. Returns 0 on success, or -1 if the queue is empty. */
static inline int input_dequeue(input_queue_handle_t *h, input_event_t *event)
{
    if (input_queue_empty(h)) {
        return -1;
    }

    uint32_t head = h->queue->head;
    *event = h->queue->events[head % h->capacity];
    __atomic_store_n(&h->queue->head, head + 1, __ATOMIC_RELEASE);
    return 0;
}
