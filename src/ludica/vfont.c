/*
 * vfont.c — Vector font dispatch layer.
 *
 * Routes lud_vfont_* calls to either the Slug (GLES3) or SDF/MSDF
 * (GLES2) backend based on the loaded font's type.
 *
 * Architecture notes:
 *
 * - The pen (lud_pen_t) is caller-owned and passed by pointer to draw.
 *   It tracks the current drawing position and is advanced by draw.
 *   Different text segments can use independent pens in the same frame.
 *
 * - Clipping is session state within begin/end.  begin/end is not
 *   reentrant — both backends batch geometry and flush on end or
 *   backend switch.  Clip uses GL scissor and applies to all draws
 *   until cleared or the session ends.
 *
 * - Font metrics (ascender, descender, line_gap) are stored in both
 *   .slugfont and .msdffont files.  The dispatch layer reads them
 *   from whichever backend owns the font handle.
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

#include "ludica_internal.h"
#include "include/ludica_vfont.h"
#include "include/ludica_slug.h"
#include <GLES2/gl2.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* msdf.c internal API */
unsigned lud_msdf_load(const char *path);
void     lud_msdf_destroy(unsigned id);
void     lud_msdf_begin(float vx, float vy, float vw, float vh);
void     lud_msdf_draw(unsigned id, lud_pen_t *pen,
                       float size, float r, float g, float b, float a,
                       const char *text);
float    lud_msdf_text_width(unsigned id, float size, const char *text);
int      lud_msdf_metrics(unsigned id, float *ascender, float *descender,
                          float *line_gap);
void     lud_msdf_end(void);

/* slug.c internal API */
int      lud_slug_metrics(lud_slug_font_t font, float *ascender,
                          float *descender, float *line_gap);

/* ------------------------------------------------------------------------ */
/* Slot management                                                           */
/* ------------------------------------------------------------------------ */

#define MAX_VFONTS 16

typedef enum { VFONT_NONE = 0, VFONT_SLUG, VFONT_MSDF } vfont_backend_t;

typedef struct {
	int used;
	vfont_backend_t backend;
	unsigned slot_id;
} vfont_slot_t;

static vfont_slot_t vfont_slots[MAX_VFONTS];

static vfont_slot_t *
vfont_get(lud_vfont_t f)
{
	if (f.id == 0 || f.id > MAX_VFONTS) return NULL;
	vfont_slot_t *s = &vfont_slots[f.id - 1];
	return s->used ? s : NULL;
}

/* ------------------------------------------------------------------------ */
/* Backend preference                                                        */
/* ------------------------------------------------------------------------ */

static vfont_backend_t
preferred_backend(void)
{
	const char *env = getenv("LUD_VFONT_BACKEND");
	if (env) {
		if (strcmp(env, "slug") == 0) {
			if (lud__state.gles_version < 3) {
				lud_err("LUD_VFONT_BACKEND=slug requires GLES3");
				return VFONT_MSDF;
			}
			return VFONT_SLUG;
		}
		if (strcmp(env, "msdf") == 0) return VFONT_MSDF;
	}
	return (lud__state.gles_version >= 3) ? VFONT_SLUG : VFONT_MSDF;
}

/* ------------------------------------------------------------------------ */
/* Path helpers                                                              */
/* ------------------------------------------------------------------------ */

static int
ends_with(const char *s, const char *suffix)
{
	size_t slen = strlen(s);
	size_t xlen = strlen(suffix);
	if (slen < xlen) return 0;
	return memcmp(s + slen - xlen, suffix, xlen) == 0;
}

static int
file_exists(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (f) { fclose(f); return 1; }
	return 0;
}

/* ------------------------------------------------------------------------ */
/* Load / destroy                                                            */
/* ------------------------------------------------------------------------ */

lud_vfont_t
lud_load_vfont(const char *path)
{
	lud_vfont_t out = {0};

	/* Explicit extension — load directly */
	if (ends_with(path, ".slugfont")) {
		lud_slug_font_t sf = lud_load_slug_font(path);
		if (sf.id == 0) return out;

		int idx = -1;
		for (int i = 0; i < MAX_VFONTS; i++) {
			if (!vfont_slots[i].used) { idx = i; break; }
		}
		if (idx < 0) {
			lud_destroy_slug_font(sf);
			lud_err("vfont pool exhausted");
			return out;
		}
		vfont_slots[idx] = (vfont_slot_t){ 1, VFONT_SLUG, sf.id };
		out.id = (unsigned)(idx + 1);
		return out;
	}

	if (ends_with(path, ".msdffont")) {
		unsigned mid = lud_msdf_load(path);
		if (mid == 0) return out;

		int idx = -1;
		for (int i = 0; i < MAX_VFONTS; i++) {
			if (!vfont_slots[i].used) { idx = i; break; }
		}
		if (idx < 0) {
			lud_msdf_destroy(mid);
			lud_err("vfont pool exhausted");
			return out;
		}
		vfont_slots[idx] = (vfont_slot_t){ 1, VFONT_MSDF, mid };
		out.id = (unsigned)(idx + 1);
		return out;
	}

	/* Extension-less path — try preferred backend first, then fallback */
	char buf[1024];
	vfont_backend_t pref = preferred_backend();

	const char *first_ext  = (pref == VFONT_SLUG) ? ".slugfont" : ".msdffont";
	const char *second_ext = (pref == VFONT_SLUG) ? ".msdffont" : ".slugfont";

	if (strlen(path) + 10 >= sizeof(buf)) {
		lud_err("vfont path too long");
		return out;
	}

	snprintf(buf, sizeof(buf), "%s%s", path, first_ext);
	if (file_exists(buf))
		return lud_load_vfont(buf);

	snprintf(buf, sizeof(buf), "%s%s", path, second_ext);
	if (file_exists(buf))
		return lud_load_vfont(buf);

	lud_err("Cannot find font '%s' (.slugfont or .msdffont)", path);
	return out;
}

void
lud_destroy_vfont(lud_vfont_t font)
{
	vfont_slot_t *s = vfont_get(font);
	if (!s) return;

	if (s->backend == VFONT_SLUG)
		lud_destroy_slug_font((lud_slug_font_t){ s->slot_id });
	else
		lud_msdf_destroy(s->slot_id);

	memset(s, 0, sizeof(*s));
}

/* ------------------------------------------------------------------------ */
/* Session state                                                             */
/* ------------------------------------------------------------------------ */

static struct {
	vfont_backend_t active;
	float vx, vy, vw, vh;
	int clipping;
} vfont_state;

void
lud_vfont_begin(float vx, float vy, float vw, float vh)
{
	vfont_state.active = VFONT_NONE;
	vfont_state.vx = vx;
	vfont_state.vy = vy;
	vfont_state.vw = vw;
	vfont_state.vh = vh;
	vfont_state.clipping = 0;
}

static void
vfont_switch(vfont_backend_t target)
{
	if (vfont_state.active == target) return;

	/* Flush previous backend */
	if (vfont_state.active == VFONT_SLUG)
		lud_slug_end();
	else if (vfont_state.active == VFONT_MSDF)
		lud_msdf_end();

	/* Start new backend */
	if (target == VFONT_SLUG)
		lud_slug_begin(vfont_state.vx, vfont_state.vy,
		               vfont_state.vw, vfont_state.vh);
	else if (target == VFONT_MSDF)
		lud_msdf_begin(vfont_state.vx, vfont_state.vy,
		               vfont_state.vw, vfont_state.vh);

	vfont_state.active = target;
}

/* ------------------------------------------------------------------------ */
/* Clipping                                                                  */
/* ------------------------------------------------------------------------ */

void
lud_vfont_set_clip(float x, float y, float w, float h)
{
	/* Flush current batch before changing scissor */
	if (vfont_state.active == VFONT_SLUG)
		lud_slug_end();
	else if (vfont_state.active == VFONT_MSDF)
		lud_msdf_end();

	/* Restart the backend so the next draw picks up the scissor */
	if (vfont_state.active == VFONT_SLUG)
		lud_slug_begin(vfont_state.vx, vfont_state.vy,
		               vfont_state.vw, vfont_state.vh);
	else if (vfont_state.active == VFONT_MSDF)
		lud_msdf_begin(vfont_state.vx, vfont_state.vy,
		               vfont_state.vw, vfont_state.vh);

	/* Convert view coordinates to framebuffer pixels for glScissor.
	 * View is Y-down, GL scissor is Y-up from bottom-left. */
	int fb_w = lud_width();
	int fb_h = lud_height();
	float sx = (x - vfont_state.vx) / vfont_state.vw * (float)fb_w;
	float sy = (y - vfont_state.vy) / vfont_state.vh * (float)fb_h;
	float sw = w / vfont_state.vw * (float)fb_w;
	float sh = h / vfont_state.vh * (float)fb_h;

	/* Flip Y for GL */
	float gl_y = (float)fb_h - sy - sh;

	glEnable(GL_SCISSOR_TEST);
	glScissor((GLint)sx, (GLint)gl_y, (GLsizei)sw, (GLsizei)sh);
	vfont_state.clipping = 1;
}

void
lud_vfont_clear_clip(void)
{
	if (!vfont_state.clipping) return;

	/* Flush current batch before removing scissor */
	if (vfont_state.active == VFONT_SLUG)
		lud_slug_end();
	else if (vfont_state.active == VFONT_MSDF)
		lud_msdf_end();

	if (vfont_state.active == VFONT_SLUG)
		lud_slug_begin(vfont_state.vx, vfont_state.vy,
		               vfont_state.vw, vfont_state.vh);
	else if (vfont_state.active == VFONT_MSDF)
		lud_msdf_begin(vfont_state.vx, vfont_state.vy,
		               vfont_state.vw, vfont_state.vh);

	glDisable(GL_SCISSOR_TEST);
	vfont_state.clipping = 0;
}

/* ------------------------------------------------------------------------ */
/* Drawing                                                                   */
/* ------------------------------------------------------------------------ */

void
lud_vfont_draw(lud_vfont_t font, lud_pen_t *pen,
               float size, float r, float g, float b, float a,
               const char *text)
{
	vfont_slot_t *s = vfont_get(font);
	if (!s || !pen) return;

	vfont_switch(s->backend);

	if (s->backend == VFONT_SLUG)
		lud_slug_draw((lud_slug_font_t){ s->slot_id },
		              pen, size, r, g, b, a, text);
	else
		lud_msdf_draw(s->slot_id,
		              pen, size, r, g, b, a, text);
}

float
lud_vfont_text_width(lud_vfont_t font, float size, const char *text)
{
	vfont_slot_t *s = vfont_get(font);
	if (!s) return 0.0f;

	if (s->backend == VFONT_SLUG)
		return lud_slug_text_width((lud_slug_font_t){ s->slot_id },
		                           size, text);
	else
		return lud_msdf_text_width(s->slot_id, size, text);
}

void
lud_vfont_end(void)
{
	if (vfont_state.active == VFONT_SLUG)
		lud_slug_end();
	else if (vfont_state.active == VFONT_MSDF)
		lud_msdf_end();

	if (vfont_state.clipping) {
		glDisable(GL_SCISSOR_TEST);
		vfont_state.clipping = 0;
	}

	vfont_state.active = VFONT_NONE;
}

/* ------------------------------------------------------------------------ */
/* Font metrics                                                              */
/* ------------------------------------------------------------------------ */

static int
vfont_raw_metrics(lud_vfont_t font, float *asc, float *desc, float *gap)
{
	vfont_slot_t *s = vfont_get(font);
	if (!s) return 0;

	if (s->backend == VFONT_SLUG)
		return lud_slug_metrics((lud_slug_font_t){ s->slot_id },
		                        asc, desc, gap);
	else
		return lud_msdf_metrics(s->slot_id, asc, desc, gap);
}

static float
vfont_em_scale(lud_vfont_t font, float size)
{
	float asc, desc, gap;
	if (!vfont_raw_metrics(font, &asc, &desc, &gap)) return 0.0f;
	return size / (asc - desc);
}

float
lud_vfont_ascender(lud_vfont_t font, float size)
{
	float asc, desc, gap;
	if (!vfont_raw_metrics(font, &asc, &desc, &gap)) return 0.0f;
	return asc * vfont_em_scale(font, size);
}

float
lud_vfont_descender(lud_vfont_t font, float size)
{
	float asc, desc, gap;
	if (!vfont_raw_metrics(font, &asc, &desc, &gap)) return 0.0f;
	return desc * vfont_em_scale(font, size);
}

float
lud_vfont_line_gap(lud_vfont_t font, float size)
{
	float asc, desc, gap;
	if (!vfont_raw_metrics(font, &asc, &desc, &gap)) return 0.0f;
	return gap * vfont_em_scale(font, size);
}

float
lud_vfont_line_height(lud_vfont_t font, float size)
{
	float asc, desc, gap;
	if (!vfont_raw_metrics(font, &asc, &desc, &gap)) return 0.0f;
	float s = vfont_em_scale(font, size);
	return (asc - desc + gap) * s;
}

void
lud_vfont_newline(lud_pen_t *pen, float left_margin, float line_height)
{
	if (!pen) return;
	pen->x = left_margin;
	pen->y += line_height;
}

/* ------------------------------------------------------------------------ */
/* Layout helpers                                                            */
/* ------------------------------------------------------------------------ */

int
lud_vfont_line_break(lud_vfont_t font, float size,
                     const char *text, float max_width)
{
	if (!text) return 0;

	int len = (int)strlen(text);
	float width = 0.0f;
	int last_space = -1;

	for (int i = 0; i < len; ) {
		int start = i;

		/* Decode one UTF-8 character to find its byte length */
		unsigned char c = (unsigned char)text[i];
		int char_len = 1;
		if (c >= 0xF0) char_len = 4;
		else if (c >= 0xE0) char_len = 3;
		else if (c >= 0xC0) char_len = 2;

		if (c == ' ') last_space = i;

		/* Measure this character */
		char buf[5] = {0};
		for (int j = 0; j < char_len && i + j < len; j++)
			buf[j] = text[i + j];
		float cw = lud_vfont_text_width(font, size, buf);

		width += cw;
		if (width > max_width) {
			if (last_space > 0) return last_space;
			if (start > 0) return start;
			return 0;
		}

		i += char_len;
	}

	return len;
}
