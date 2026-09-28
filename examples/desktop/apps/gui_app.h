/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Helpers for GUI applications of the desktop compositor. An app calls
 * gui_app_init() from its init(), draws into gui_app_surface(), publishes
 * changes with gui_app_commit(), and drains input with gui_app_next_event()
 * when the compositor notifies it on GUI_COMPOSITOR_CH.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <lions/gui/protocol.h>
#include <gui_config.h>
#include "../src/gfx.h"

/* Set up the surface with the given size and title. Returns false if the size does not fit. */
bool gui_app_init(const char *title, uint32_t width, uint32_t height);

gfx_surface_t *gui_app_surface(void);

/* Publish `damage` (content coordinates) and notify the compositor */
void gui_app_commit(gfx_rect_t damage);

/* Publish the whole surface */
void gui_app_commit_all(void);

/* Returns false once there are no more events */
bool gui_app_next_event(gui_event_t *ev);

/* Keyboard helper: tracks the shift keys and returns the typed character, or 0 */
char gui_app_key_to_ascii(gui_event_t *ev);
