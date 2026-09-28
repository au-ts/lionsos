/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * A minimal software renderer for 32-bit BGRA surfaces.
 *
 * Pixels are stored as uint32_t 0xAARRGGBB, which on a little-endian machine
 * is laid out in memory as B, G, R, A and therefore matches
 * GPU_FORMAT_B8G8R8A8_UNORM. All drawing operations clip to the surface.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#define GFX_RGB(r, g, b) (0xff000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

#define GFX_FONT_WIDTH 8
#define GFX_FONT_HEIGHT 8

typedef struct gfx_rect {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} gfx_rect_t;

typedef struct gfx_surface {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    /* Distance between the start of two rows, in pixels */
    uint32_t stride;
    /* Drawing is limited to this rectangle, which lies within the surface */
    gfx_rect_t clip;
} gfx_surface_t;

/* Limit drawing to `r` (intersected with the surface bounds) */
void gfx_set_clip(gfx_surface_t *s, gfx_rect_t r);

/* Allow drawing to the whole surface again */
void gfx_reset_clip(gfx_surface_t *s);

bool gfx_rect_contains(gfx_rect_t r, int32_t x, int32_t y);

void gfx_fill_rect(gfx_surface_t *s, gfx_rect_t r, uint32_t colour);

/* Draw a rectangle border of the given thickness */
void gfx_draw_rect(gfx_surface_t *s, gfx_rect_t r, uint32_t thickness, uint32_t colour);

/* Fill with a vertical gradient from `top` to `bottom` */
void gfx_fill_vgradient(gfx_surface_t *s, gfx_rect_t r, uint32_t top, uint32_t bottom);

/*
 * Draw a string at (x, y), each glyph scaled by `scale`. Only printable ASCII is
 * supported, other characters are drawn as a checker-board glyph.
 * Returns the x coordinate just past the last glyph drawn.
 */
int32_t gfx_draw_text(gfx_surface_t *s, int32_t x, int32_t y, const char *text, uint32_t scale, uint32_t colour);

/* Width in pixels of `text` when drawn at `scale` */
int32_t gfx_text_width(const char *text, uint32_t scale);
