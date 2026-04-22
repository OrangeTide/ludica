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
 *   lud_load_vfont("assets/fonts/roboto")
 * The loader appends the right extension for the active backend.
 * Explicit extensions (.slugfont, .msdffont) also work.
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

typedef struct { unsigned id; } lud_vfont_t;

/* Pen — tracks the current drawing position in view coordinates.
 * Passed by pointer to lud_vfont_draw(), which advances pen->x
 * by the rendered text width. Caller owns the pen and may read
 * or modify it freely between draw calls. */
typedef struct { float x, y; } lud_pen_t;

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
 * pen->x, pen->y is the baseline-left position in view coordinates.
 * On return, pen->x is advanced past the rendered text (pen->y unchanged).
 * size is the font height in view units (e.g. pixels).
 * Color is RGBA 0.0-1.0. */
void lud_vfont_draw(lud_vfont_t font, lud_pen_t *pen,
                    float size, float r, float g, float b, float a,
                    const char *text);

/* Measure text width in view units for a given size. */
float lud_vfont_text_width(lud_vfont_t font, float size,
                           const char *text);

/* Flush pending draws and tear down rendering state. */
void lud_vfont_end(void);

/* --- Clipping --------------------------------------------------------- */

/* Set a clip rectangle in view coordinates. Text outside this rectangle
 * is not drawn. Applies to all subsequent draw calls within the current
 * begin/end block. Uses GL scissor internally. */
void lud_vfont_set_clip(float x, float y, float w, float h);

/* Remove the active clip rectangle. */
void lud_vfont_clear_clip(void);

/* --- Font metrics ----------------------------------------------------- */

/* Font metrics scaled to view units for a given size.
 * ascender:   distance from baseline to top of tallest glyph (positive).
 * descender:  distance from baseline to bottom of lowest glyph (negative).
 * line_gap:   extra spacing between lines recommended by the font.
 * line_height = ascender - descender + line_gap. */
float lud_vfont_ascender(lud_vfont_t font, float size);
float lud_vfont_descender(lud_vfont_t font, float size);
float lud_vfont_line_gap(lud_vfont_t font, float size);
float lud_vfont_line_height(lud_vfont_t font, float size);

/* Advance pen to the next line.
 * Sets pen->x to left_margin, advances pen->y by line_height. */
void lud_vfont_newline(lud_pen_t *pen, float left_margin,
                       float line_height);

/* --- Layout helpers --------------------------------------------------- */

/* Find the byte offset at which to break text to fit within max_width
 * view units. Breaks at the last space that fits. Returns the length
 * of the input if it fits entirely, or 0 if not even one word fits. */
int lud_vfont_line_break(lud_vfont_t font, float size,
                         const char *text, float max_width);

#endif /* LUDICA_VFONT_H_ */
