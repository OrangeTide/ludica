#ifndef LUDICA_VFONT_H_
#define LUDICA_VFONT_H_

/*
 * ludica_vfont.h — Resolution-independent vector font rendering.
 *
 * Unified front-end for two backends:
 *   - Slug (GLES3): GPU evaluation of quadratic Bezier outlines.
 *   - SDF/MSDF (GLES2+): Signed distance field atlas rendering.
 *
 * Backend is chosen automatically based on GLES version:
 *   GLES3 → Slug (.slugfont), GLES2 → SDF (.msdffont).
 * Override with environment variable LUD_VFONT_BACKEND=slug|msdf|auto.
 *
 * Load with an extension-less base path:
 *   lud_load_vfont("data/roboto")
 * The loader appends the right extension for the active backend.
 * Explicit extensions (.slugfont, .msdffont) also work.
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

typedef struct { unsigned id; } lud_vfont_t;

/* Load a vector font. See header comment for path conventions.
 * Returns zero-handle on failure. */
lud_vfont_t lud_load_vfont(const char *path);

/* Destroy a loaded vector font and free GPU resources. */
void lud_destroy_vfont(lud_vfont_t font);

/* Begin a vector font drawing session.
 * Sets up orthographic projection: (vx,vy) is top-left, (vw,vh) is size.
 * Y-down coordinate system (matches sprite batch convention). */
void lud_vfont_begin(float vx, float vy, float vw, float vh);

/* Draw a string of text.
 * (x, y) is the baseline-left position in view coordinates.
 * size is the font height in view units (e.g. pixels).
 * Color is RGBA 0.0-1.0. */
void lud_vfont_draw(lud_vfont_t font, float x, float y,
                    float size, float r, float g, float b, float a,
                    const char *text);

/* Measure text width in view units for a given size. */
float lud_vfont_text_width(lud_vfont_t font, float size,
                           const char *text);

/* Flush pending draws and tear down rendering state. */
void lud_vfont_end(void);

#endif /* LUDICA_VFONT_H_ */
