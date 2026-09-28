/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdint.h>
#include <string.h>
#include <microkit.h>
#include <sddf/util/util.h>
#include <lions/input/input.h>
#include "mu_app.h"

#define TEXT_SCALE 2

static mu_Context ctx;
static mu_app_frame_fn frame_fn;
static mu_app_char_fn char_fn;
static uint32_t width, height;
static uint64_t last_hash;

static int text_width(mu_Font font, const char *str, int len)
{
    (void)font;
    if (len < 0) {
        len = strlen(str);
    }
    return len * GFX_FONT_WIDTH * TEXT_SCALE;
}

static int text_height(mu_Font font)
{
    (void)font;
    return GFX_FONT_HEIGHT * TEXT_SCALE;
}

static uint32_t colour(mu_Color c)
{
    return GFX_RGB(c.r, c.g, c.b);
}

static void set_theme(mu_Style *style)
{
    style->size = mu_vec2(68, GFX_FONT_HEIGHT * TEXT_SCALE);
    style->padding = 6;
    style->spacing = 6;
    style->colors[MU_COLOR_TEXT] = mu_color(0xe6, 0xe6, 0xe6, 0xff);
    style->colors[MU_COLOR_BORDER] = mu_color(0x0f, 0x14, 0x1c, 0xff);
    style->colors[MU_COLOR_WINDOWBG] = mu_color(0x28, 0x2c, 0x34, 0xff);
    style->colors[MU_COLOR_TITLEBG] = mu_color(0x2f, 0x5d, 0xa8, 0xff);
    style->colors[MU_COLOR_TITLETEXT] = mu_color(0xf2, 0xf2, 0xf2, 0xff);
    style->colors[MU_COLOR_PANELBG] = mu_color(0x1e, 0x22, 0x28, 0xff);
    style->colors[MU_COLOR_BUTTON] = mu_color(0x3c, 0x42, 0x4e, 0xff);
    style->colors[MU_COLOR_BUTTONHOVER] = mu_color(0x50, 0x58, 0x68, 0xff);
    style->colors[MU_COLOR_BUTTONFOCUS] = mu_color(0x2f, 0x5d, 0xa8, 0xff);
    style->colors[MU_COLOR_BASE] = mu_color(0x1c, 0x1f, 0x26, 0xff);
    style->colors[MU_COLOR_BASEHOVER] = mu_color(0x24, 0x28, 0x30, 0xff);
    style->colors[MU_COLOR_BASEFOCUS] = mu_color(0x2c, 0x30, 0x3a, 0xff);
    style->colors[MU_COLOR_SCROLLBASE] = mu_color(0x1c, 0x1f, 0x26, 0xff);
    style->colors[MU_COLOR_SCROLLTHUMB] = mu_color(0x50, 0x58, 0x68, 0xff);
}

static void draw_icon(gfx_surface_t *s, int id, mu_Rect r, mu_Color c)
{
    int32_t size = GFX_FONT_WIDTH * TEXT_SCALE;
    int32_t x = r.x + (r.w - size) / 2;
    int32_t y = r.y + (r.h - size) / 2;

    switch (id) {
    case MU_ICON_CHECK:
        gfx_fill_rect(s, (gfx_rect_t) { x + 3, y + 3, size - 6, size - 6 }, colour(c));
        break;
    case MU_ICON_CLOSE:
        gfx_draw_text(s, x, y, "x", TEXT_SCALE, colour(c));
        break;
    case MU_ICON_COLLAPSED:
        gfx_draw_text(s, x, y, "+", TEXT_SCALE, colour(c));
        break;
    case MU_ICON_EXPANDED:
        gfx_draw_text(s, x, y, "-", TEXT_SCALE, colour(c));
        break;
    }
}

static uint64_t hash_commands(void)
{
    /* FNV-1a over the command list, which fully describes the frame */
    uint64_t h = 0xcbf29ce484222325ull;
    for (int i = 0; i < ctx.command_list.idx; i++) {
        h = (h ^ (uint8_t)ctx.command_list.items[i]) * 0x100000001b3ull;
    }
    return h;
}

static void render(void)
{
    gfx_surface_t *s = gui_app_surface();
    mu_Command *cmd = NULL;
    while (mu_next_command(&ctx, &cmd)) {
        switch (cmd->type) {
        case MU_COMMAND_CLIP:
            gfx_set_clip(s, (gfx_rect_t) { cmd->clip.rect.x, cmd->clip.rect.y, cmd->clip.rect.w, cmd->clip.rect.h });
            break;
        case MU_COMMAND_RECT:
            if (cmd->rect.color.a != 0) {
                gfx_fill_rect(s, (gfx_rect_t) { cmd->rect.rect.x, cmd->rect.rect.y, cmd->rect.rect.w, cmd->rect.rect.h },
                              colour(cmd->rect.color));
            }
            break;
        case MU_COMMAND_TEXT:
            gfx_draw_text(s, cmd->text.pos.x, cmd->text.pos.y, cmd->text.str, TEXT_SCALE, colour(cmd->text.color));
            break;
        case MU_COMMAND_ICON:
            draw_icon(s, cmd->icon.id, cmd->icon.rect, cmd->icon.color);
            break;
        }
    }
    gfx_reset_clip(s);
}

static void run_frame(void)
{
    mu_begin(&ctx);
    int opts = MU_OPT_NOTITLE | MU_OPT_NORESIZE | MU_OPT_NOCLOSE;
    if (mu_begin_window_ex(&ctx, "main", mu_rect(0, 0, width, height), opts)) {
        /* Keep the window pinned to the surface */
        mu_Container *win = mu_get_current_container(&ctx);
        win->rect = mu_rect(0, 0, width, height);
        frame_fn(&ctx);
        mu_end_window(&ctx);
    }
    mu_end(&ctx);

    uint64_t hash = hash_commands();
    if (hash == last_hash) {
        return;
    }
    last_hash = hash;
    render();
    gui_app_commit_all();
}

bool mu_app_init(const char *title, uint32_t w, uint32_t h, mu_app_frame_fn frame, mu_app_char_fn on_char)
{
    if (!gui_app_init(title, w, h)) {
        return false;
    }
    width = w;
    height = h;
    frame_fn = frame;
    char_fn = on_char;

    mu_init(&ctx);
    ctx.text_width = text_width;
    ctx.text_height = text_height;
    set_theme(ctx.style);

    run_frame();
    return true;
}

void mu_app_redraw(void)
{
    run_frame();
}

static int to_mu_button(uint16_t code)
{
    switch (code) {
    case INPUT_BTN_RIGHT:
        return MU_MOUSE_RIGHT;
    case INPUT_BTN_MIDDLE:
        return MU_MOUSE_MIDDLE;
    default:
        return MU_MOUSE_LEFT;
    }
}

static int to_mu_key(uint16_t code)
{
    switch (code) {
    case INPUT_KEY_LEFTSHIFT:
    case INPUT_KEY_RIGHTSHIFT:
        return MU_KEY_SHIFT;
    case INPUT_KEY_LEFTCTRL:
    case INPUT_KEY_RIGHTCTRL:
        return MU_KEY_CTRL;
    case INPUT_KEY_LEFTALT:
    case INPUT_KEY_RIGHTALT:
        return MU_KEY_ALT;
    case INPUT_KEY_BACKSPACE:
        return MU_KEY_BACKSPACE;
    case INPUT_KEY_ENTER:
        return MU_KEY_RETURN;
    default:
        return 0;
    }
}

void mu_app_handle_events(void)
{
    bool any = false;
    gui_event_t ev;
    while (gui_app_next_event(&ev)) {
        any = true;
        switch (ev.type) {
        case GUI_EV_POINTER_MOTION:
            mu_input_mousemove(&ctx, ev.x, ev.y);
            break;
        case GUI_EV_POINTER_BUTTON:
            mu_input_mousemove(&ctx, ev.x, ev.y);
            if (ev.value == INPUT_KEY_PRESSED) {
                /* microui decides what is hovered from the previous frame */
                run_frame();
                mu_input_mousedown(&ctx, ev.x, ev.y, to_mu_button(ev.code));
                run_frame();
            } else {
                mu_input_mouseup(&ctx, ev.x, ev.y, to_mu_button(ev.code));
            }
            break;
        case GUI_EV_KEY: {
            int key = to_mu_key(ev.code);
            char c = gui_app_key_to_ascii(&ev);
            if (key != 0) {
                if (ev.value == INPUT_KEY_RELEASED) {
                    mu_input_keyup(&ctx, key);
                } else {
                    mu_input_keydown(&ctx, key);
                }
            }
            if (c != 0) {
                if (char_fn) {
                    char_fn(c);
                }
                char text[2] = { c, '\0' };
                mu_input_text(&ctx, text);
            }
            /* Key presses take effect in the next frame, so run one per key */
            if (ev.value != INPUT_KEY_RELEASED) {
                run_frame();
            }
            break;
        }
        default:
            break;
        }
    }
    if (any) {
        run_frame();
    }
}
