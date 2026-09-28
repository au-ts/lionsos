/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * The LionsOS desktop app API for WebAssembly programs.
 *
 * Apps are freestanding wasm32 modules run by the desktop's WebAssembly host
 * (wasm_host/wasm_host.c). They may use the small C library WAMR provides
 * (memset, memcpy, strlen, snprintf, malloc, ...) and export any of these
 * callbacks:
 *
 *   void app_init(int width, int height);   0, 0 without a window
 *   void app_event(int type, int code, int value, int x, int y);
 *   void app_tick(int time_ms);
 *
 * Apps start with no authority. Everything they can do goes through a
 * capability listed in /apps/<name>.caps and granted by the host:
 *
 *   window               lions_*_rect, text, blit, commit, title; input events
 *   timer                lions_timer_start, lions_time_ms; app_tick
 *   console              lions_log
 *   file /apps/<path>    lions_file_size, lions_file_read (read-only)
 *
 * Look a capability up by that name to get its handle, then pass the handle
 * to the functions using it. They return LIONS_DENIED (-1) for a handle that
 * is not a live capability of the right type, and the refusal is audited.
 * Colours are 0xRRGGBB. Press Esc to leave an app.
 */

#pragma once

#define LIONS_IMPORT(name) __attribute__((import_module("lions"), import_name(#name)))
#define LIONS_EXPORT(name) __attribute__((export_name(#name)))

#define LIONS_DENIED (-1)

/* Capabilities */
LIONS_IMPORT(cap_lookup) int lions_cap(const char *name);
/* Give a capability up for good */
LIONS_IMPORT(cap_drop) int lions_cap_drop(int cap);

/* window */
LIONS_IMPORT(win_fill_rect) int lions_fill_rect(int win, int x, int y, int w, int h, unsigned rgb);
/* The 8x8 font scaled by `scale` (1 to 8) */
LIONS_IMPORT(win_draw_text) int lions_draw_text(int win, int x, int y, const char *text, int scale, unsigned rgb);
/* `pixels` holds w * h 0xRRGGBB values, `len` is its size in bytes */
LIONS_IMPORT(win_blit) int lions_blit(int win, int x, int y, int w, int h, const unsigned *pixels, unsigned len);
/* Show the changed area once the current callback returns */
LIONS_IMPORT(win_commit) int lions_commit(int win, int x, int y, int w, int h);
LIONS_IMPORT(win_width) int lions_width(int win);
LIONS_IMPORT(win_height) int lions_height(int win);
LIONS_IMPORT(win_set_title) int lions_set_title(int win, const char *title);

/* timer: call app_tick every `ms` milliseconds (at least 10), or stop if 0 */
LIONS_IMPORT(timer_start) int lions_timer_start(int timer, int ms);
LIONS_IMPORT(timer_now) int lions_time_ms(int timer);

/* console: print a line on the serial console */
LIONS_IMPORT(console_log) int lions_log(int console, const char *text);

/* file: its size, and up to `len` bytes read at `offset` (returns the number read) */
LIONS_IMPORT(file_size) int lions_file_size(int file);
LIONS_IMPORT(file_read) int lions_file_read(int file, unsigned offset, void *buf, unsigned len);

/* An app can always end itself */
LIONS_IMPORT(exit) void lions_exit(void);

/* From WAMR's built-in C library */
typedef unsigned long size_t;
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
size_t strlen(const char *s);
int snprintf(char *buf, size_t size, const char *fmt, ...);

/* app_event types, see <lions/gui/protocol.h> */
#define LIONS_EV_POINTER_MOTION 1 /* x, y */
#define LIONS_EV_POINTER_BUTTON 2 /* code: button, value: 1 pressed / 0 released, x, y */
#define LIONS_EV_KEY 3            /* code: evdev key code, value: 1 pressed / 0 released / 2 repeat, x: character */
#define LIONS_EV_FOCUS 4          /* value: 1 gained / 0 lost */

#define LIONS_BTN_LEFT 0x110
#define LIONS_BTN_RIGHT 0x111
