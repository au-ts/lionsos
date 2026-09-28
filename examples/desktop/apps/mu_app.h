/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * microui backend for desktop apps. The app's whole surface is one microui
 * window; the app provides a function that lays out its widgets every frame.
 *
 * Frames run when the compositor delivers events (mu_app_handle_events) or
 * when the app asks for one (mu_app_redraw). A frame is only rendered and
 * committed if its draw commands differ from the previous frame's.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "microui/microui.h"
#include "gui_app.h"

typedef void (*mu_app_frame_fn)(mu_Context *ctx);

/* Called with each character typed while the app has focus, before microui sees it */
typedef void (*mu_app_char_fn)(char c);

bool mu_app_init(const char *title, uint32_t width, uint32_t height, mu_app_frame_fn frame, mu_app_char_fn on_char);

/* Feed pending compositor events to microui and run frames as needed */
void mu_app_handle_events(void);

/* Run a frame, e.g. after the app's state changed for another reason */
void mu_app_redraw(void);
