/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdint.h>
#include <stddef.h>
#include "gfx.h"
#include "font_petme128_8x8.h"

#define FONT_FIRST_CHAR 32
#define FONT_LAST_CHAR 127

static inline int32_t max32(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

static inline int32_t min32(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

/* Intersect r with the surface bounds. Returns false if nothing is left. */
static bool clip(gfx_surface_t *s, gfx_rect_t *r)
{
    int32_t x0 = max32(r->x, 0);
    int32_t y0 = max32(r->y, 0);
    int32_t x1 = min32(r->x + r->width, (int32_t)s->width);
    int32_t y1 = min32(r->y + r->height, (int32_t)s->height);

    if (x1 <= x0 || y1 <= y0) {
        return false;
    }

    *r = (gfx_rect_t) { x0, y0, x1 - x0, y1 - y0 };
    return true;
}

void gfx_fill_rect(gfx_surface_t *s, gfx_rect_t r, uint32_t colour)
{
    if (!clip(s, &r)) {
        return;
    }

    for (int32_t y = r.y; y < r.y + r.height; y++) {
        uint32_t *row = s->pixels + (size_t)y * s->stride;
        for (int32_t x = r.x; x < r.x + r.width; x++) {
            row[x] = colour;
        }
    }
}

void gfx_draw_rect(gfx_surface_t *s, gfx_rect_t r, uint32_t thickness, uint32_t colour)
{
    int32_t t = (int32_t)thickness;
    gfx_fill_rect(s, (gfx_rect_t) { r.x, r.y, r.width, t }, colour);
    gfx_fill_rect(s, (gfx_rect_t) { r.x, r.y + r.height - t, r.width, t }, colour);
    gfx_fill_rect(s, (gfx_rect_t) { r.x, r.y + t, t, r.height - 2 * t }, colour);
    gfx_fill_rect(s, (gfx_rect_t) { r.x + r.width - t, r.y + t, t, r.height - 2 * t }, colour);
}

static inline uint32_t lerp_channel(uint32_t a, uint32_t b, int32_t num, int32_t den)
{
    return (uint32_t)((int32_t)a + ((int32_t)b - (int32_t)a) * num / den);
}

void gfx_fill_vgradient(gfx_surface_t *s, gfx_rect_t r, uint32_t top, uint32_t bottom)
{
    if (r.height <= 0) {
        return;
    }

    for (int32_t i = 0; i < r.height; i++) {
        uint32_t red = lerp_channel((top >> 16) & 0xff, (bottom >> 16) & 0xff, i, r.height);
        uint32_t green = lerp_channel((top >> 8) & 0xff, (bottom >> 8) & 0xff, i, r.height);
        uint32_t blue = lerp_channel(top & 0xff, bottom & 0xff, i, r.height);
        gfx_fill_rect(s, (gfx_rect_t) { r.x, r.y + i, r.width, 1 }, GFX_RGB(red, green, blue));
    }
}

static void draw_glyph(gfx_surface_t *s, int32_t x, int32_t y, char c, uint32_t scale, uint32_t colour)
{
    unsigned char ch = (unsigned char)c;
    if (ch < FONT_FIRST_CHAR || ch > FONT_LAST_CHAR) {
        ch = FONT_LAST_CHAR;
    }

    /* The font is stored column-major: one byte per column, bit 0 is the top row */
    const uint8_t *glyph = &font_petme128_8x8[(ch - FONT_FIRST_CHAR) * GFX_FONT_WIDTH];
    for (int32_t col = 0; col < GFX_FONT_WIDTH; col++) {
        for (int32_t row = 0; row < GFX_FONT_HEIGHT; row++) {
            if (glyph[col] & (1 << row)) {
                gfx_fill_rect(s,
                              (gfx_rect_t) { x + col * (int32_t)scale, y + row * (int32_t)scale, (int32_t)scale,
                                             (int32_t)scale },
                              colour);
            }
        }
    }
}

int32_t gfx_draw_text(gfx_surface_t *s, int32_t x, int32_t y, const char *text, uint32_t scale, uint32_t colour)
{
    for (const char *p = text; *p != '\0'; p++) {
        draw_glyph(s, x, y, *p, scale, colour);
        x += GFX_FONT_WIDTH * (int32_t)scale;
    }
    return x;
}

int32_t gfx_text_width(const char *text, uint32_t scale)
{
    int32_t n = 0;
    for (const char *p = text; *p != '\0'; p++) {
        n++;
    }
    return n * GFX_FONT_WIDTH * (int32_t)scale;
}
