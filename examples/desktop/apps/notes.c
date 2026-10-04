/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Notes: a tiny text editor. Shows a caret while focused. */

#include <stdint.h>
#include <string.h>
#include <microkit.h>
#include <sddf/util/util.h>
#include <lions/input/input.h>
#include "gui_app.h"

/* Initial size; the window can be resized, so nothing below may depend on these */
#define WIDTH 420
#define HEIGHT 230
#define SCALE 2
#define CHAR_W (GFX_FONT_WIDTH * SCALE)
#define CHAR_H (GFX_FONT_HEIGHT * SCALE)
#define LINE_H (CHAR_H + 6)
#define PADDING 12

static int32_t cols(void)
{
    int32_t w = (int32_t)gui_app_surface()->width - 2 * PADDING;
    return w > 0 ? w / CHAR_W : 0;
}

static int32_t rows(void)
{
    int32_t h = (int32_t)gui_app_surface()->height - 2 * PADDING;
    return h > 0 ? h / LINE_H : 0;
}

#define NOTES_MAX 512

#define COLOUR_BG GFX_RGB(0xf7, 0xf5, 0xf0)
#define COLOUR_TEXT GFX_RGB(0x22, 0x22, 0x22)
#define COLOUR_HINT GFX_RGB(0x8a, 0x8f, 0x99)

static char text[NOTES_MAX + 1] = "Hello! I am a separate\nprotection domain.\n";
static uint32_t len;
static bool focused;

static void draw(void)
{
    gfx_surface_t *s = gui_app_surface();
    int32_t width = (int32_t)s->width;
    int32_t height = (int32_t)s->height;
    int32_t columns = cols();
    int32_t visible_rows = rows();
    gfx_fill_rect(s, (gfx_rect_t) { 0, 0, width, height }, COLOUR_BG);

    /* Hard-wrap into lines, then show the last visible_rows of them */
    int32_t line_start[NOTES_MAX + 2];
    int32_t line_len[NOTES_MAX + 2];
    int32_t num_lines = 0;
    int32_t start = 0;
    for (int32_t i = 0; i <= (int32_t)len; i++) {
        if (i == (int32_t)len || text[i] == '\n' || i - start == columns) {
            line_start[num_lines] = start;
            line_len[num_lines] = i - start;
            num_lines++;
            start = (i < (int32_t)len && text[i] == '\n') ? i + 1 : i;
        }
    }

    int32_t y = PADDING;
    int32_t caret_x = PADDING;
    for (int32_t l = MAX(0, num_lines - visible_rows); l < num_lines; l++) {
        /* Bounded by the text, not by the width, so it needs no resizing */
        char line[NOTES_MAX + 1];
        memcpy(line, &text[line_start[l]], line_len[l]);
        line[line_len[l]] = '\0';
        caret_x = gfx_draw_text(s, PADDING, y, line, SCALE, COLOUR_TEXT);
        y += LINE_H;
    }

    if (focused) {
        gfx_fill_rect(s, (gfx_rect_t) { caret_x, y - LINE_H + CHAR_H, CHAR_W, 2 }, COLOUR_TEXT);
    } else {
        gfx_draw_text(s, PADDING, height - PADDING - CHAR_H, "Click here to type", SCALE, COLOUR_HINT);
    }

    gui_app_commit_all();
}

static bool handle_key(gui_event_t *ev)
{
    if (ev->value == INPUT_KEY_RELEASED) {
        gui_app_key_to_ascii(ev);
        return false;
    }

    if (ev->code == INPUT_KEY_BACKSPACE) {
        if (len == 0) {
            return false;
        }
        len--;
    } else if (ev->code == INPUT_KEY_ENTER) {
        if (len >= NOTES_MAX) {
            return false;
        }
        text[len++] = '\n';
    } else {
        char c = gui_app_key_to_ascii(ev);
        if (c == 0 || len >= NOTES_MAX) {
            return false;
        }
        text[len++] = c;
    }
    text[len] = '\0';
    return true;
}

void init(void)
{
    len = strlen(text);
    if (gui_app_init("Notes", WIDTH, HEIGHT)) {
        gui_app_set_flags(GUI_FLAG_RESIZABLE);
        draw();
    }
}

void notified(microkit_channel ch)
{
    if (ch != GUI_COMPOSITOR_CH) {
        return;
    }

    bool dirty = false;
    gui_event_t ev;
    while (gui_app_next_event(&ev)) {
        if (ev.type == GUI_EV_FOCUS) {
            focused = ev.value;
            dirty = true;
        } else if (ev.type == GUI_EV_KEY) {
            dirty |= handle_key(&ev);
        } else if (ev.type == GUI_EV_RESIZE) {
            /* The text is the model, so re-wrapping is just drawing again */
            dirty |= gui_app_resize(ev.x, ev.y);
        }
    }
    if (dirty) {
        draw();
    }
}
