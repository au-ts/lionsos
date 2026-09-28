/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * The LionsOS desktop app API for WebAssembly programs.
 *
 * Apps are freestanding wasm32 modules run by the desktop's WebAssembly host
 * (wasm_host/wasm_host.c). They import the functions below from the "lions"
 * module, may use the small C library WAMR provides (memset, memcpy, strlen,
 * snprintf, malloc, ...), and export any of these callbacks:
 *
 *   void app_init(int width, int height);
 *   void app_event(int type, int code, int value, int x, int y);
 *   void app_tick(int time_ms);
 *
 * Drawing goes into the app's window; call lions_commit() with the changed
 * area and the host shows it once the callback returns. Colours are
 * 0xRRGGBB. Press Esc to leave an app and return to the host's list.
 */

#pragma once

#define LIONS_IMPORT(name) __attribute__((import_module("lions"), import_name(#name)))
#define LIONS_EXPORT(name) __attribute__((export_name(#name)))

LIONS_IMPORT(fill_rect) void lions_fill_rect(int x, int y, int w, int h, unsigned rgb);
/* The 8x8 font scaled by `scale` (1 to 8) */
LIONS_IMPORT(draw_text) void lions_draw_text(int x, int y, const char *text, int scale, unsigned rgb);
/* `pixels` holds w * h 0xRRGGBB values, `len` is its size in bytes */
LIONS_IMPORT(blit) void lions_blit(int x, int y, int w, int h, const unsigned *pixels, unsigned len);
LIONS_IMPORT(commit) void lions_commit(int x, int y, int w, int h);
/* Call app_tick every `ms` milliseconds (at least 10), or stop if 0 */
LIONS_IMPORT(set_tick) void lions_set_tick(int ms);
LIONS_IMPORT(exit) void lions_exit(void);
LIONS_IMPORT(width) int lions_width(void);
LIONS_IMPORT(height) int lions_height(void);
LIONS_IMPORT(time_ms) int lions_time_ms(void);
/* Printed on the serial console */
LIONS_IMPORT(log) void lions_log(const char *text);
LIONS_IMPORT(set_title) void lions_set_title(const char *title);

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
