#ifndef LUDICA_SLUG_H_
#define LUDICA_SLUG_H_

/*
 * ludica_slug.h — GPU vector font rendering (Slug algorithm).
 *
 * Requires GLES3. On GLES2 contexts, lud_load_slug_font() returns a
 * null handle and all other calls are no-ops. Use the bitmap font API
 * (ludica_font.h) as a fallback.
 *
 * Based on the Slug algorithm by Eric Lengyel.
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

#include "ludica_vfont.h"

typedef struct { unsigned id; } lud_slug_font_t;

/* Load a .slugfont file. Returns zero-handle on failure or GLES2. */
lud_slug_font_t lud_load_slug_font(const char *path);

/* Destroy a loaded Slug font and free GPU resources. */
void lud_destroy_slug_font(lud_slug_font_t font);

/* Begin a Slug text drawing session.
 * Sets up orthographic projection: (vx,vy) is top-left, (vw,vh) is size.
 * Y-down coordinate system (matches sprite batch convention). */
void lud_slug_begin(float vx, float vy, float vw, float vh);

/* Draw a string of text.
 * pen->x, pen->y is the baseline-left position in view coordinates.
 * On return, pen->x is advanced past the rendered text.
 * size is the font height in view units (e.g. pixels).
 * Color is RGBA 0.0-1.0. */
void lud_slug_draw(lud_slug_font_t font, lud_pen_t *pen,
                   float size, float r, float g, float b, float a,
                   const char *text);

/* Measure text width in view units for a given size. */
float lud_slug_text_width(lud_slug_font_t font, float size,
                          const char *text);

/* Flush pending draws and tear down Slug rendering state. */
void lud_slug_end(void);

#endif /* LUDICA_SLUG_H_ */
