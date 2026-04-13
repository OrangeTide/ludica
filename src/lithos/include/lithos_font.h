#ifndef LITHOS_FONT_H_
#define LITHOS_FONT_H_

#include "lithos_gfx.h"

typedef struct { unsigned id; } lithos_font_t;

/* Create a bitmap font from a texture atlas.
 * chars_wide: number of glyph columns in the texture.
 * glyph_w, glyph_h: size of each glyph cell in pixels.
 * first_char: ASCII code of the first glyph (typically 32). */
lithos_font_t lithos_make_font(lithos_texture_t tex,
                               int chars_wide,
                               int glyph_w, int glyph_h,
                               int first_char);

/* Create the built-in 8x8 font. No file loading needed. */
lithos_font_t lithos_make_default_font(void);

void lithos_destroy_font(lithos_font_t font);

/* Measure text width in pixels (does not draw). */
int lithos_text_width(lithos_font_t font, const char *text);

/* Draw text at (x, y) in world coordinates.
 * Must be called between lithos_sprite_begin/end.
 * scale: 1 = native glyph size, 2 = double, etc. */
void lithos_draw_text(lithos_font_t font, float x, float y,
                      float scale, const char *text);

/* Draw text centered horizontally at x. */
void lithos_draw_text_centered(lithos_font_t font, float x, float y,
                               float scale, const char *text);

/* Draw text word-wrapped to max_width pixels.
 * line_spacing: vertical distance between lines (in world units). */
void lithos_draw_text_wrapped(lithos_font_t font, float x, float y,
                              float scale, float max_width,
                              float line_spacing, const char *text);

#endif /* LITHOS_FONT_H_ */
