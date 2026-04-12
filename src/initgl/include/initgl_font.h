#ifndef INITGL_FONT_H_
#define INITGL_FONT_H_

#include "initgl_gfx.h"

typedef struct { unsigned id; } initgl_font_t;

/* Create a bitmap font from a texture atlas.
 * chars_wide: number of glyph columns in the texture.
 * glyph_w, glyph_h: size of each glyph cell in pixels.
 * first_char: ASCII code of the first glyph (typically 32). */
initgl_font_t initgl_make_font(initgl_texture_t tex,
                               int chars_wide,
                               int glyph_w, int glyph_h,
                               int first_char);

/* Create the built-in 8x8 font. No file loading needed. */
initgl_font_t initgl_make_default_font(void);

void initgl_destroy_font(initgl_font_t font);

/* Measure text width in pixels (does not draw). */
int initgl_text_width(initgl_font_t font, const char *text);

/* Draw text at (x, y) in world coordinates.
 * Must be called between initgl_sprite_begin/end.
 * scale: 1 = native glyph size, 2 = double, etc. */
void initgl_draw_text(initgl_font_t font, float x, float y,
                      float scale, const char *text);

/* Draw text centered horizontally at x. */
void initgl_draw_text_centered(initgl_font_t font, float x, float y,
                               float scale, const char *text);

/* Draw text word-wrapped to max_width pixels.
 * line_spacing: vertical distance between lines (in world units). */
void initgl_draw_text_wrapped(initgl_font_t font, float x, float y,
                              float scale, float max_width,
                              float line_spacing, const char *text);

#endif /* INITGL_FONT_H_ */
