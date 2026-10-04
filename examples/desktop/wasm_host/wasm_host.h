/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* What wasm_host.c shares with sandbox_apps.c */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <lions/fs/protocol.h>
#include "../src/gfx.h"

/* The size of the list's window, and of each app's */
#define WIDTH 480
#define HEIGHT 360

#define NAME_MAX_LEN 48
#define MIN_TICK_MS 10

#define COLOUR_BG GFX_RGB(0x1b, 0x1f, 0x27)
#define COLOUR_ACCENT GFX_RGB(0xe0, 0x8a, 0x1e)
#define COLOUR_TEXT GFX_RGB(0xe6, 0xe6, 0xe6)
#define COLOUR_MUTED GFX_RGB(0x8a, 0x8f, 0x99)

#define LOG_HOST(...) printf("WASM HOST|INFO: " __VA_ARGS__)
#define LOG_HOST_ERR(...) printf("WASM HOST|ERROR: " __VA_ARGS__)

/* Open `path` with `type` (FS_CMD_FILE_OPEN or FS_CMD_DIR_OPEN); true on success */
bool fs_path_command(uint64_t type, const char *path, uint64_t flags, fs_cmpl_t *cmpl);

/* The size of the file at `path`, or false if it cannot be read */
bool file_size(const char *path, uint64_t *size);

/* Read up to `max` bytes at `offset` into `dst`; returns the number read or -1 */
int64_t read_file(const char *path, uint64_t offset, uint8_t *dst, uint64_t max);

/* One line of the audit log about `app` */
void audit(const char *app, const char *fmt, ...);

/* Show a status line under the list, and redraw it */
void host_status(bool is_error, const char *fmt, ...);

/*
 * The filesystem broker behind the Files application: set up the shared page,
 * and answer one request. Called from the host's cothread, so it may block on
 * the filesystem. See wasm_host/files_broker.c and include/files_ns.h.
 */
void files_broker_init(void);
void files_broker_handle(void);

/* The time, and a timeout from the timer driver, in ms */
int32_t now_ms(void);
void host_set_timeout_ms(uint32_t ms);
