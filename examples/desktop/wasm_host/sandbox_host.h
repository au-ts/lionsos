/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* The host's side of running apps in sandboxes (see sandbox.h); sandbox k is the host's child k */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <lions/gui/protocol.h>
#include <gui_config.h>
#include "caps.h"
#include "sandbox.h"

#define SANDBOX_COUNT GUI_WASM_WINDOWS

/* Reads up to `max` bytes of `path` at `offset` into `dst`; returns the number read or -1 */
typedef int64_t (*sandbox_read_fn)(const char *path, uint64_t offset, uint8_t *dst, uint64_t max);

/* Set up what the host needs to make sandboxes; returns false if it cannot */
bool sandbox_init(void);

/*
 * Build sandbox k for an app and start it: the runner, the module at
 * `module_path`, and what `caps` grants, with a window of `width` by
 * `height` if it grants one. Returns false, with a reason in `error`, if
 * it cannot, in which case nothing is left behind.
 */
bool sandbox_start(int k, const char *name, const char *module_path, uint64_t module_size, caps_table_t *caps,
                   uint32_t width, uint32_t height, sandbox_read_fn read, char *error, size_t error_len);

/* Stop the app in sandbox k and take back everything it was given */
void sandbox_stop(int k);

/* Take back what backs one grant (the window's frames, or a file's) */
void sandbox_revoke_grant(int k, int handle, cap_type_t type);

/* Queue an event or a tick for the app; sandbox_kick() then wakes it */
bool sandbox_post_event(int k, gui_event_t ev);
bool sandbox_post_tick(int k, uint32_t time_ms);
void sandbox_kick(int k);

/* The next request from the runner in sandbox k, validated; false if there is none */
bool sandbox_next_request(int k, sandbox_req_t *req);
