/*
 * ansiview — ANSI Art Viewer.
 *
 * Loads and renders ANSI art files with CP437 font rendering
 * and 16-color palette. Ported from ansiview.c (public domain).
 *
 * Arrow keys: scroll viewport
 * Tab: toggle 8x16 / 8x8 font
 * Escape: quit
 */

#include "lithos.h"
#include "lithos_gfx.h"
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

/* Font atlas layout: 32 glyphs per row, 8 rows = 256 CP437 characters. */
#define FONT_COLS    32

/* ANSI art buffer */
#define ANSI_MAX_W   80
#define ANSI_MAX_H   100

struct ansi_cell { unsigned char ch, attr; };

#define ATTR_FG(attr) ((attr) & 0xf)
#define ATTR_BG(attr) (((attr) & 0xf0) >> 4)
#define MKATTR(fg, bg) ((((bg) << 4) & 0xf0) | ((fg) & 0xf))

static int ansi_width, ansi_height;
static struct ansi_cell *ansi_data;

/* Font state */
static lithos_texture_t font16_tex; /* 8x16 font atlas */
static lithos_texture_t font8_tex;  /* 8x8 font atlas */
static lithos_texture_t font_tex;   /* currently active */
static int font_w, font_h;         /* current glyph dimensions */

static int view_x, view_y;

/* CGA/EGA 16-color palette (RGB float) */
static const float palette[16][3] = {
	{ 0.000f, 0.000f, 0.000f }, /*  0: black */
	{ 0.000f, 0.000f, 0.667f }, /*  1: blue */
	{ 0.000f, 0.667f, 0.000f }, /*  2: green */
	{ 0.000f, 0.667f, 0.667f }, /*  3: cyan */
	{ 0.667f, 0.000f, 0.000f }, /*  4: red */
	{ 0.667f, 0.000f, 0.667f }, /*  5: magenta */
	{ 0.667f, 0.333f, 0.000f }, /*  6: brown */
	{ 0.667f, 0.667f, 0.667f }, /*  7: light gray */
	{ 0.333f, 0.333f, 0.333f }, /*  8: dark gray */
	{ 0.333f, 0.333f, 1.000f }, /*  9: light blue */
	{ 0.333f, 1.000f, 0.333f }, /* 10: light green */
	{ 0.333f, 1.000f, 1.000f }, /* 11: light cyan */
	{ 1.000f, 0.333f, 0.333f }, /* 12: light red */
	{ 1.000f, 0.333f, 1.000f }, /* 13: light magenta */
	{ 1.000f, 1.000f, 0.333f }, /* 14: yellow */
	{ 1.000f, 1.000f, 1.000f }, /* 15: white */
};

static void
clear_ansi(unsigned char attr)
{
	struct ansi_cell fill = { ' ', attr };
	int i;

	for (i = 0; i < ansi_width * ansi_height; i++)
		ansi_data[i] = fill;
}

/*
 * ANSI art loader — parses a subset of ANSI.SYS escape sequences.
 *
 * Supported:
 *   ESC[<n>A    Cursor up
 *   ESC[<n>B    Cursor down
 *   ESC[<n>C    Cursor forward
 *   ESC[<n>D    Cursor back
 *   ESC[<r>;<c>H  Cursor position
 *   ESC[<n>J    Erase display (n=2,3)
 *   ESC[<n>m    SGR color codes
 */
static void
ansi_load(const char *filename)
{
	FILE *f;
	unsigned char buf[80];
	int pos_x, pos_y, fg, bg;
	enum { ST_NORMAL, ST_ESCAPE, ST_CSI } state;
	unsigned short parameters[10];
	unsigned num_parameters;
	unsigned short working_parameter;
	int working_parameter_valid;
	unsigned char ansi_color[] = { 0, 4, 2, 6, 1, 5, 3, 7 };

	f = fopen(filename, "r");
	if (!f)
		return;

	free(ansi_data);
	ansi_width = ANSI_MAX_W;
	ansi_height = ANSI_MAX_H;
	ansi_data = calloc(ansi_height, ansi_width * sizeof(*ansi_data));
	if (!ansi_data) {
		fclose(f);
		return;
	}

	state = ST_NORMAL;
	fg = 7;
	bg = 0;
	pos_x = pos_y = 0;
	num_parameters = 0;
	working_parameter = 0;
	working_parameter_valid = 0;
	clear_ansi(MKATTR(fg, bg));

	while (fgets((char *)buf, sizeof(buf), f)) {
		unsigned char ch, *s = buf;

		while ((ch = *s++)) {
			switch (state) {
			case ST_NORMAL:
				if (pos_x >= ansi_width) {
					pos_x = 0;
					pos_y++;
					if (pos_y >= ansi_height)
						goto done;
				}
				if (ch == '\033') {
					state = ST_ESCAPE;
				} else if (ch == '\b') {
					if (pos_x)
						pos_x--;
				} else if (ch == '\r') {
					pos_x = 0;
				} else if (ch == '\n') {
					pos_x = 0;
					pos_y++;
				} else {
					if (pos_x >= 0 && pos_y >= 0 &&
					    pos_x < ansi_width && pos_y < ansi_height) {
						struct ansi_cell *out = &ansi_data[pos_x + pos_y * ansi_width];
						out->attr = MKATTR(fg, bg);
						out->ch = ch;
					}
					pos_x++;
				}
				break;
			case ST_ESCAPE:
				if (ch == '[') {
					state = ST_CSI;
					num_parameters = 0;
					working_parameter = 0;
					working_parameter_valid = 0;
				} else {
					state = ST_NORMAL;
				}
				break;
			case ST_CSI:
				if (isdigit(ch)) {
					working_parameter = (working_parameter * 10) + (ch - '0');
					working_parameter_valid = 1;
				} else if (ch == ';') {
					parameters[num_parameters++] = working_parameter;
					working_parameter = 0;
					working_parameter_valid = 0;
				} else {
					state = ST_NORMAL;
					if (working_parameter_valid)
						parameters[num_parameters++] = working_parameter;
					if (ch == 'A') {
						pos_y -= num_parameters ? *parameters : 1;
						if (pos_y < 0) pos_y = 0;
					} else if (ch == 'B') {
						pos_y += num_parameters ? *parameters : 1;
						if (pos_y >= ansi_height) pos_y = ansi_height - 1;
					} else if (ch == 'C') {
						pos_x += num_parameters ? *parameters : 1;
						if (pos_x >= ansi_width) pos_x = ansi_width - 1;
					} else if (ch == 'D') {
						pos_x -= num_parameters ? *parameters : 1;
						if (pos_x < 0) pos_x = 0;
					} else if (ch == 'H') {
						if (num_parameters == 0) {
							pos_y = 0;
							pos_x = 0;
						} else if (num_parameters == 1) {
							pos_y = parameters[0] - 1;
						} else if (num_parameters >= 2) {
							if (parameters[0])
								pos_y = parameters[0] - 1;
							if (parameters[1])
								pos_x = parameters[1] - 1;
						}
					} else if (ch == 'm') {
						unsigned i;

						if (!num_parameters) {
							fg = 7;
							bg = 0;
							break;
						}

						for (i = 0; i < num_parameters; i++) {
							unsigned short p = parameters[i];

							if (p == 0) {
								fg = 7;
								bg = 0;
							} else if (p == 1) {
								fg |= 8;
							} else if (p == 2) {
								fg &= 7;
							} else if (p == 5 || p == 6) {
								bg |= 8;
							} else if (p == 25) {
								bg &= 7;
							} else if (p == 7) {
								unsigned char tmp = fg;
								fg = (fg & 8) | (bg & 7);
								bg = (bg & 8) | (tmp & 7);
							} else if (p >= 30 && p <= 37) {
								fg = (fg & 8) | ansi_color[p - 30];
							} else if (p >= 40 && p <= 47) {
								bg = (bg & 8) | ansi_color[p - 40];
							}
						}
					} else if (ch == 'J') {
						if (!num_parameters || parameters[0] == 0) {
							/* clear from cursor to end — simplified */
						} else if (parameters[0] == 2 || parameters[0] == 3) {
							pos_x = 0;
							pos_y = 0;
							clear_ansi(MKATTR(fg, bg));
						}
					}
				}
				break;
			}
		}
	}
done:
	fclose(f);
}

/*
 * Load a font PNG (white-on-transparent, pre-inverted).
 */
static lithos_texture_t
load_font_png(const char *path)
{
	lithos_texture_t tex;

	tex = lithos_load_texture(path, LITHOS_FILTER_NEAREST, LITHOS_FILTER_NEAREST);
	if (tex.id == 0)
		fprintf(stderr, "Failed to load font: %s\n", path);
	return tex;
}

static void
set_font_mode(int use_16)
{
	if (use_16) {
		font_tex = font16_tex;
		font_w = 8;
		font_h = 16;
	} else {
		font_tex = font8_tex;
		font_w = 8;
		font_h = 8;
	}
}

static void
draw_glyph(unsigned char ch, int x, int y, int fg_idx, int bg_idx)
{
	float dx = (float)(x * font_w);
	float dy = (float)(y * font_h);
	int col = ch % FONT_COLS;
	int row = ch / FONT_COLS;
	float sx = (float)(col * font_w);
	float sy = (float)(row * font_h);

	/* background */
	lithos_sprite_rect(dx, dy, font_w, font_h,
	                   palette[bg_idx][0], palette[bg_idx][1], palette[bg_idx][2], 1.0f);

	/* foreground glyph */
	lithos_sprite_draw_tinted(font_tex,
	                          dx, dy, font_w, font_h,
	                          sx, sy, font_w, font_h,
	                          palette[fg_idx][0], palette[fg_idx][1], palette[fg_idx][2], 1.0f);
}

static int
on_event(const lithos_event_t *ev)
{
	if (ev->type != LITHOS_EV_KEY_DOWN)
		return 0;

	switch (ev->key.keycode) {
	case LITHOS_KEY_ESCAPE:
		lithos_quit();
		return 1;
	case LITHOS_KEY_LEFT:
		view_x--;
		return 1;
	case LITHOS_KEY_RIGHT:
		view_x++;
		return 1;
	case LITHOS_KEY_UP:
		view_y--;
		return 1;
	case LITHOS_KEY_DOWN:
		view_y++;
		return 1;
	case LITHOS_KEY_HOME:
		view_x = 0;
		view_y = 0;
		return 1;
	case LITHOS_KEY_END:
		view_y = ansi_height - lithos_height() / font_h;
		return 1;
	case LITHOS_KEY_PAGE_UP:
		view_y -= lithos_height() / font_h;
		return 1;
	case LITHOS_KEY_PAGE_DOWN:
		view_y += lithos_height() / font_h;
		return 1;
	case LITHOS_KEY_TAB:
		if (font_h == 16)
			set_font_mode(0);
		else
			set_font_mode(1);
		return 1;
	default:
		break;
	}

	return 0;
}

static void
init(void)
{
	font16_tex = load_font_png("src/ansiview/assets/font8x16.png");
	font8_tex = load_font_png("src/ansiview/assets/font8x8.png");
	set_font_mode(1); /* default to 8x16 */
	ansi_load("src/ansiview/assets/rad-love.ans");
	view_x = 0;
	view_y = 0;
}

static void
frame(float dt)
{
	int virtual_w, virtual_h;
	int cols_visible, rows_visible;
	int x, y;

	(void)dt;

	virtual_w = lithos_width();
	virtual_h = lithos_height();
	cols_visible = virtual_w / font_w;
	rows_visible = virtual_h / font_h;

	/* clamp viewport */
	if (view_x < 0) view_x = 0;
	if (view_y < 0) view_y = 0;
	if (view_x > ansi_width - cols_visible)
		view_x = ansi_width - cols_visible;
	if (view_y > ansi_height - rows_visible)
		view_y = ansi_height - rows_visible;
	if (view_x < 0) view_x = 0;
	if (view_y < 0) view_y = 0;

	lithos_viewport(0, 0, virtual_w, virtual_h);
	lithos_clear(0.0f, 0.0f, 0.0f, 1.0f);

	lithos_sprite_begin(0, 0, virtual_w, virtual_h);

	for (y = 0; y < rows_visible && (view_y + y) < ansi_height; y++) {
		for (x = 0; x < cols_visible && (view_x + x) < ansi_width; x++) {
			struct ansi_cell *a = &ansi_data[(view_x + x) + (view_y + y) * ansi_width];
			draw_glyph(a->ch, x, y, ATTR_FG(a->attr), ATTR_BG(a->attr));
		}
	}

	lithos_sprite_end();
}

static void
cleanup(void)
{
	lithos_destroy_texture(font16_tex);
	lithos_destroy_texture(font8_tex);
	free(ansi_data);
	ansi_data = NULL;
}

int
main(void)
{
	return lithos_run(&(lithos_desc_t){
		.app_name = "demo04 — ANSI art viewer",
		.width = 640,
		.height = 400,
		.resizable = 1,
		.init = init,
		.frame = frame,
		.cleanup = cleanup,
		.event = on_event,
	});
}
