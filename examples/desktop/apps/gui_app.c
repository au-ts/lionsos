/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdint.h>
#include <microkit.h>
#include <sddf/util/util.h>
#include <sddf/util/printf.h>
#include <lions/input/input.h>
#include "gui_app.h"
#include "../src/keymap.h"

/* Patched in by the system description */
uintptr_t gui_surface;
uintptr_t gui_state;
uintptr_t gui_events;

static gfx_surface_t surface;
static bool shift_left, shift_right;

bool gui_app_init(const char *title, uint32_t width, uint32_t height)
{
    if ((uint64_t)width * height * sizeof(uint32_t) > GUI_SURFACE_REGION_SIZE) {
        sddf_dprintf("GUI APP|ERROR: %ux%u does not fit in the surface region\n", width, height);
        return false;
    }

    surface = (gfx_surface_t) {
        .pixels = (uint32_t *)gui_surface,
        .width = width,
        .height = height,
        .stride = width,
    };
    gfx_reset_clip(&surface);

    gui_state_t *state = (gui_state_t *)gui_state;
    state->magic = GUI_STATE_MAGIC;
    state->width = width;
    state->height = height;
    gui_app_set_title(title);
    return true;
}

void gui_app_set_flags(uint32_t flags)
{
    gui_state_t *state = (gui_state_t *)gui_state;
    state->flags = flags;
}

/* Move the surface to a new content size. The caller redraws and commits. */
static bool resize(uint32_t width, uint32_t height, uint32_t fill, bool preserve)
{
    if (!gui_size_fits(width, height, GUI_SURFACE_REGION_SIZE)) {
        sddf_dprintf("GUI APP|ERROR: %ux%u does not fit in the surface region\n", width, height);
        return false;
    }

    if (preserve) {
        /* Before the stride changes, so the old layout is still readable */
        gfx_relayout(surface.pixels, surface.width, surface.height, width, height, fill);
    }

    surface.width = width;
    surface.height = height;
    surface.stride = width;
    gfx_reset_clip(&surface);

    gui_state_t *state = (gui_state_t *)gui_state;
    state->width = width;
    state->height = height;
    return true;
}

bool gui_app_resize(uint32_t width, uint32_t height)
{
    return resize(width, height, 0, false);
}

bool gui_app_resize_preserve(uint32_t width, uint32_t height, uint32_t fill)
{
    return resize(width, height, fill, true);
}

void gui_app_set_title(const char *title)
{
    gui_state_t *state = (gui_state_t *)gui_state;
    uint32_t i = 0;
    for (; title[i] != '\0' && i < GUI_TITLE_MAX - 1; i++) {
        state->title[i] = title[i];
    }
    state->title[i] = '\0';
}

gfx_surface_t *gui_app_surface(void)
{
    return &surface;
}

void gui_app_commit(gfx_rect_t damage)
{
    gui_state_commit((gui_state_t *)gui_state,
                     (gui_rect_t) { damage.x, damage.y, damage.width, damage.height });
    microkit_notify(GUI_COMPOSITOR_CH);
}

void gui_app_commit_all(void)
{
    gui_app_commit((gfx_rect_t) { 0, 0, (int32_t)surface.width, (int32_t)surface.height });
}

bool gui_app_next_event(gui_event_t *ev)
{
    return gui_event_dequeue((gui_event_queue_t *)gui_events, GUI_EVENT_QUEUE_CAPACITY(GUI_EVENTS_REGION_SIZE),
                             ev) == 0;
}

char gui_app_key_to_ascii(gui_event_t *ev)
{
    if (ev->type != GUI_EV_KEY) {
        return 0;
    }

    bool down = ev->value != INPUT_KEY_RELEASED;
    if (ev->code == INPUT_KEY_LEFTSHIFT) {
        shift_left = down;
        return 0;
    }
    if (ev->code == INPUT_KEY_RIGHTSHIFT) {
        shift_right = down;
        return 0;
    }
    if (!down) {
        return 0;
    }
    return keymap_to_ascii(ev->code, shift_left || shift_right);
}
