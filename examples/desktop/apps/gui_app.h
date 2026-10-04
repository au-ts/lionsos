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

/*
 * Declare what the window supports, from GUI_FLAG_* in the protocol. The
 * compositor reads this from the state page like everything else there, and a
 * flag only ever affects this window. Call it before the first commit.
 */
void gui_app_set_flags(uint32_t flags);

gfx_surface_t *gui_app_surface(void);

/* Change the window title; the compositor picks it up with the next commit */
void gui_app_set_title(const char *title);

/*
 * Adopt a new content size, sent by the compositor as GUI_EV_RESIZE. Returns
 * false if the size does not fit the surface, in which case nothing changes.
 *
 * The stride is the width, so the pixels move: the caller must redraw the whole
 * surface afterwards and then commit it. These do not commit for you.
 */
bool gui_app_resize(uint32_t width, uint32_t height);

/*
 * As gui_app_resize, but keeps the existing pixels: the overlapping region is
 * carried across the stride change and the area the resize exposes is set to
 * `fill`. For an application whose surface is its document, such as Sketch.
 */
bool gui_app_resize_preserve(uint32_t width, uint32_t height, uint32_t fill);

/* Publish `damage` (content coordinates) and notify the compositor */
void gui_app_commit(gfx_rect_t damage);

/* Publish the whole surface */
void gui_app_commit_all(void);

/* Returns false once there are no more events */
bool gui_app_next_event(gui_event_t *ev);

/* Keyboard helper: tracks the shift keys and returns the typed character, or 0 */
char gui_app_key_to_ascii(gui_event_t *ev);
