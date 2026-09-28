/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Application slots of the compositor. Must match meta.py.
 *
 * The regions of slot i are mapped in the compositor at
 *   gui_surfaces + i * GUI_SURFACE_REGION_SIZE   (read-only)
 *   gui_states   + i * GUI_STATE_REGION_SIZE     (read-only)
 *   gui_events   + i * GUI_EVENTS_REGION_SIZE
 * and the app of slot i is on compositor channel GUI_APP_CH_BASE + i.
 */

#pragma once

#include <lions/gui/protocol.h>

#define GUI_NUM_APPS 6

/* Slots whose window opens at startup; the others start closed (see the launcher) */
#define GUI_START_OPEN ((1 << 0) | (1 << 1) | (1 << 2) | (1 << 5))

#define GUI_SURFACE_REGION_SIZE 0x100000
#define GUI_STATE_REGION_SIZE 0x1000
#define GUI_EVENTS_REGION_SIZE 0x1000

#define GUI_APP_CH_BASE 10

/* The compositor's channel as seen from an app */
#define GUI_COMPOSITOR_CH 0
