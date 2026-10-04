/*
 * Copyright 2026, LionsOS Contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Files: a read-only view of the desktop's namespace.
 *
 * This PD holds no filesystem connection of its own. The FAT server serves one
 * client, so it asks the broker in wasm_host, which owns the namespace and
 * decides what a name means. See include/files_ns.h and README.md.
 *
 * It speaks friendly names ("C:\Applications") and shows the shape of the
 * namespace, including the parts that have nothing behind them yet, rather
 * than hiding them. Nothing here creates, writes, renames or deletes.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <microkit.h>
#include <sddf/util/util.h>
#include <lions/input/input.h>
#include "../../include/files_ns.h"
#include "gui_app.h"

/* Initial size; fixed for now, and README.md says so */
#define WIDTH 420
#define HEIGHT 300

#define SCALE 1
#define CHAR_W GFX_FONT_WIDTH
#define LINE_H (GFX_FONT_HEIGHT + 4)
#define PADDING 10
#define HEADER_H (LINE_H + 6)
#define PREVIEW_LINES 6

#define COLOUR_BG GFX_RGB(0xf7, 0xf5, 0xf0)
#define COLOUR_HEADER GFX_RGB(0xe4, 0xe1, 0xda)
#define COLOUR_TEXT GFX_RGB(0x22, 0x22, 0x22)
#define COLOUR_MUTED GFX_RGB(0x8a, 0x8f, 0x99)
#define COLOUR_SELECTED GFX_RGB(0xd8, 0xe4, 0xf2)

/* Patched in by the system description */
uintptr_t files_page_vaddr;

static files_page_t *page;

static char current[FILES_PATH_MAX] = "C:\\";
static files_entry_t entries[FILES_ENTRIES_MAX];
static uint32_t num_entries;
static int32_t selected = -1;
static uint64_t seq;
static bool waiting;

/* What is being previewed, and its first bytes as text */
static char preview_name[FILES_NAME_MAX];
static char preview_text[FILES_READ_MAX + 1];
static uint32_t preview_len;
static bool preview_is_text = true;

static int32_t visible_rows(void)
{
    int32_t h = (int32_t)gui_app_surface()->height;
    int32_t rows = (h - HEADER_H - 2 * PADDING) / LINE_H;
    if (preview_len > 0) {
        rows -= PREVIEW_LINES + 1;
    }
    return rows > 0 ? rows : 0;
}

static void request(uint32_t kind, const char *path, uint32_t offset, uint32_t length)
{
    page->request.kind = kind;
    page->request.offset = offset;
    page->request.length = length;
    page->request.seq = ++seq;
    size_t len = strlen(path);
    memcpy(page->request.path, path, len + 1);
    waiting = true;
    microkit_notify(FILES_BROKER_CH);
}

static void go_to(const char *path)
{
    strncpy(current, path, sizeof(current) - 1);
    current[sizeof(current) - 1] = '\0';
    selected = -1;
    preview_len = 0;
    preview_text[0] = '\0';
    request(FILES_REQ_LIST, current, 0, 0);
}

/* Parent of `path`, or the same path at the root */
static void parent_of(const char *path, char *out, size_t out_len)
{
    strncpy(out, path, out_len - 1);
    out[out_len - 1] = '\0';
    char *last = NULL;
    for (char *p = out; *p != '\0'; p++) {
        if (*p == '\\') {
            last = p;
        }
    }
    if (last == NULL) {
        return;
    }
    if (last == out) {
        /* C:\ */
        *(last + 1) = '\0';
        return;
    }
    *last = '\0';
}

static void activate(int32_t index)
{
    if (index < 0 || (uint32_t)index >= num_entries) {
        return;
    }
    files_entry_t *e = &entries[index];
    if (!e->available) {
        return;
    }

    selected = index;
    preview_len = 0;
    preview_text[0] = '\0';

    if (!e->is_dir) {
        char child[FILES_PATH_MAX];
        snprintf(child, sizeof(child), "%s\\%s", current, e->name);
        strncpy(preview_name, e->name, sizeof(preview_name) - 1);
        request(FILES_REQ_READ, child, 0, FILES_READ_MAX);
        return;
    }

    if (strcmp(e->name, "..") == 0) {
        char parent[FILES_PATH_MAX];
        parent_of(current, parent, sizeof(parent));
        go_to(parent);
        return;
    }

    char child[FILES_PATH_MAX];
    if (current[strlen(current) - 1] == '\\') {
        snprintf(child, sizeof(child), "%s%s", current, e->name);
    } else {
        snprintf(child, sizeof(child), "%s\\%s", current, e->name);
    }
    go_to(child);
}

/*
 * Show the bytes as text if they mostly are. Anything else is reported as
 * binary rather than drawn as garbage.
 */
static void set_preview(const uint8_t *data, uint32_t len)
{
    uint32_t printable = 0;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t c = data[i];
        if (c == '\n' || c == '\t' || (c >= 0x20 && c < 0x7f)) {
            printable++;
        }
    }
    preview_is_text = len == 0 || printable * 4 >= len * 3;

    uint32_t n = MIN(len, FILES_READ_MAX);
    memcpy(preview_text, data, n);
    preview_text[n] = '\0';
    for (uint32_t i = 0; i < n; i++) {
        if (preview_text[i] == '\r') {
            preview_text[i] = ' ';
        }
    }
    preview_len = n;
}

static void draw(void)
{
    gfx_surface_t *s = gui_app_surface();
    int32_t width = (int32_t)s->width;
    int32_t height = (int32_t)s->height;

    gfx_fill_rect(s, (gfx_rect_t) { 0, 0, width, height }, COLOUR_BG);

    /* Where we are, and how much of it is real */
    gfx_fill_rect(s, (gfx_rect_t) { 0, 0, width, HEADER_H }, COLOUR_HEADER);
    gfx_draw_text(s, PADDING, 4, current, SCALE, COLOUR_TEXT);
    if (waiting) {
        gfx_draw_text(s, width - PADDING - gfx_text_width("...", SCALE), 4, "...", SCALE, COLOUR_MUTED);
    }

    int32_t y = HEADER_H + PADDING;
    int32_t rows = visible_rows();

    if (num_entries == 0 && !waiting) {
        gfx_draw_text(s, PADDING, y, "Nothing here", SCALE, COLOUR_MUTED);
    }

    for (uint32_t i = 0; i < num_entries && (int32_t)i < rows; i++) {
        files_entry_t *e = &entries[i];
        if ((int32_t)i == selected) {
            gfx_fill_rect(s, (gfx_rect_t) { 0, y - 2, width, LINE_H }, COLOUR_SELECTED);
        }

        uint32_t colour = e->available ? COLOUR_TEXT : COLOUR_MUTED;
        const char *mark = e->is_dir ? "[dir] " : "      ";
        int32_t x = gfx_draw_text(s, PADDING, y, mark, SCALE, colour);
        x = gfx_draw_text(s, x, y, e->name, SCALE, colour);

        if (!e->available) {
            gfx_draw_text(s, x + CHAR_W, y, "(not available yet)", SCALE, COLOUR_MUTED);
        } else if (!e->is_dir && e->size > 0) {
            char size_text[32];
            snprintf(size_text, sizeof(size_text), "%lu B", (unsigned long)e->size);
            gfx_draw_text(s, width - PADDING - gfx_text_width(size_text, SCALE), y, size_text, SCALE,
                          COLOUR_MUTED);
        }
        y += LINE_H;
    }

    if (preview_len > 0) {
        y = height - PADDING - (PREVIEW_LINES + 1) * LINE_H;
        gfx_draw_text(s, PADDING, y, preview_name, SCALE, COLOUR_TEXT);
        y += LINE_H;
        if (!preview_is_text) {
            gfx_draw_text(s, PADDING, y, "(binary, not shown)", SCALE, COLOUR_MUTED);
        } else {
            /* Draw at most PREVIEW_LINES lines, wrapped to the width */
            int32_t cols = (width - 2 * PADDING) / CHAR_W;
            if (cols < 1) {
                cols = 1;
            }
            int32_t lines = 0;
            for (uint32_t i = 0; i < preview_len && lines < PREVIEW_LINES;) {
                char line[128];
                uint32_t n = MIN((uint32_t)cols, preview_len - i);
                memcpy(line, preview_text + i, n);
                line[n] = '\0';
                gfx_draw_text(s, PADDING, y, line, SCALE, COLOUR_TEXT);
                i += n;
                y += LINE_H;
                lines++;
            }
        }
    }

    gui_app_commit_all();
}

static void handle_reply(void)
{
    if (page->response.seq != seq) {
        return;
    }
    waiting = false;

    if (page->response.status != FILES_OK) {
        num_entries = 0;
        draw();
        return;
    }

    if (page->request.kind == FILES_REQ_READ) {
        set_preview(page->response.data, page->response.count);
    } else {
        uint32_t n = MIN(page->response.count, FILES_ENTRIES_MAX);
        memcpy(entries, page->response.entries, n * sizeof(files_entry_t));
        num_entries = n;
        selected = -1;
    }
    draw();
}

void init(void)
{
    page = (files_page_t *)files_page_vaddr;
    memset(&page->request, 0, sizeof(page->request));

    if (!gui_app_init("Files", WIDTH, HEIGHT)) {
        return;
    }
    draw();
    request(FILES_REQ_LIST, current, 0, 0);
}

void notified(microkit_channel ch)
{
    if (ch != GUI_COMPOSITOR_CH && ch != FILES_BROKER_CH) {
        return;
    }

    bool dirty = false;
    gui_event_t ev;
    while (gui_app_next_event(&ev)) {
        if (ev.type == GUI_EV_POINTER_BUTTON && ev.code == INPUT_BTN_LEFT
            && ev.value != INPUT_KEY_RELEASED) {
            int32_t row = (ev.y - (HEADER_H + PADDING)) / LINE_H;
            if (ev.y < HEADER_H + PADDING) {
                /* The path bar goes up a level */
                char parent[FILES_PATH_MAX];
                parent_of(current, parent, sizeof(parent));
                if (strcmp(parent, current) != 0) {
                    go_to(parent);
                }
            } else if (row >= 0 && (uint32_t)row < num_entries) {
                activate(row);
            }
        } else if (ev.type == GUI_EV_KEY) {
            /* No editing: this is a viewer */
        }
    }

    if (ch == FILES_BROKER_CH) {
        handle_reply();
        dirty = false;
    }

    if (dirty) {
        draw();
    }
}
