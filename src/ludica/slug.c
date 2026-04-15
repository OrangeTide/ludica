/*
 * slug.c — GPU vector font renderer (Slug algorithm).
 *
 * Loads .slugfont binary files produced by font2slug, creates RGBA16F
 * curve and RG16UI band textures, and renders glyph quads using the
 * Slug fragment shader for resolution-independent text.
 *
 * Based on the Slug algorithm by Eric Lengyel.
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

#include "ludica_internal.h"
#include "ludica_gfx.h"
#include "include/ludica_slug.h"
#include "slug_format.h"
#include <GLES2/gl2.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Generated shader sources */
extern const char slug_vert[];
extern const char slug_frag[];

/* ------------------------------------------------------------------------ */
/* Font slot                                                                 */
/* ------------------------------------------------------------------------ */

#define MAX_SLUG_FONTS 8

typedef struct {
	int used;
	slug_file_header_t hdr;
	slug_glyph_t *glyphs;    /* sorted by codepoint */
	slug_kern_pair_t *kerns;  /* sorted by (left<<32|right) */
	lud_texture_t curve_tex;
	lud_texture_t band_tex;
} slug_font_slot_t;

static slug_font_slot_t font_slots[MAX_SLUG_FONTS];

static slug_font_slot_t *
get_font_slot(lud_slug_font_t f)
{
	if (f.id == 0 || f.id > MAX_SLUG_FONTS) return NULL;
	slug_font_slot_t *s = &font_slots[f.id - 1];
	return s->used ? s : NULL;
}

/* Binary search for glyph by codepoint */
static const slug_glyph_t *
find_glyph(const slug_font_slot_t *s, uint32_t cp)
{
	int lo = 0, hi = (int)s->hdr.glyph_count - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		if (s->glyphs[mid].codepoint == cp) return &s->glyphs[mid];
		if (s->glyphs[mid].codepoint < cp) lo = mid + 1;
		else hi = mid - 1;
	}
	return NULL;
}

/* Binary search for kerning adjustment */
static float
find_kern(const slug_font_slot_t *s, uint32_t left, uint32_t right)
{
	if (!s->kerns || s->hdr.kern_count == 0) return 0.0f;
	int lo = 0, hi = (int)s->hdr.kern_count - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		const slug_kern_pair_t *k = &s->kerns[mid];
		if (k->left == left && k->right == right) return k->adjust;
		if (k->left < left || (k->left == left && k->right < right))
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return 0.0f;
}

/* ------------------------------------------------------------------------ */
/* Batch state                                                               */
/* ------------------------------------------------------------------------ */

/*
 * Vertex format: 6 attributes per vertex, 18 floats = 72 bytes.
 *   a_pos   (vec2)  — screen-space position
 *   a_norm  (vec2)  — outward normal for dilation
 *   a_em    (vec2)  — em-space sample coordinate
 *   a_glyph (vec4)  — (glyph_x, glyph_y, band_max_x, band_max_y) as float
 *   a_band  (vec4)  — (band_scale_x, band_scale_y, band_off_x, band_off_y)
 *   a_color (vec4)  — RGBA
 */

#define SLUG_FLOATS_PER_VERT  18
#define SLUG_VERTS_PER_QUAD   6
#define SLUG_MAX_QUADS        512

static struct {
	int initialized;
	lud_shader_t shader;
	GLuint vbo;
	float verts[SLUG_MAX_QUADS * SLUG_VERTS_PER_QUAD * SLUG_FLOATS_PER_VERT];
	int quad_count;
	float proj[16];
	float em_per_pixel;
	lud_slug_font_t current_font;
} slug_batch;

static void
slug_init(void)
{
	if (slug_batch.initialized) return;
	if (lud__state.gles_version < 3) return;

	slug_batch.shader = lud_make_shader(&(lud_shader_desc_t){
		.vert_src = slug_vert,
		.frag_src = slug_frag,
		.attrs = { "a_pos", "a_norm", "a_em", "a_glyph", "a_band", "a_color" },
		.num_attrs = 6,
	});

	glGenBuffers(1, &slug_batch.vbo);
	slug_batch.initialized = 1;
}

static void
slug_flush(void)
{
	if (slug_batch.quad_count == 0) return;

	slug_font_slot_t *s = get_font_slot(slug_batch.current_font);
	if (!s) { slug_batch.quad_count = 0; return; }

	int vert_count = slug_batch.quad_count * SLUG_VERTS_PER_QUAD;
	int stride = SLUG_FLOATS_PER_VERT * (int)sizeof(float);

	lud_apply_shader(slug_batch.shader);
	lud_uniform_mat4(slug_batch.shader, "u_proj", slug_batch.proj);
	lud_uniform_vec2(slug_batch.shader, "u_em_per_pixel",
	                 slug_batch.em_per_pixel, slug_batch.em_per_pixel);
	lud_uniform_int(slug_batch.shader, "u_curves", 0);
	lud_uniform_int(slug_batch.shader, "u_bands", 1);

	lud_bind_texture(s->curve_tex, 0);
	lud_bind_texture(s->band_tex, 1);

	glBindBuffer(GL_ARRAY_BUFFER, slug_batch.vbo);
	glBufferData(GL_ARRAY_BUFFER,
	             (GLsizeiptr)(vert_count * SLUG_FLOATS_PER_VERT * (int)sizeof(float)),
	             slug_batch.verts, GL_STREAM_DRAW);

	int off = 0;
	/* a_pos (vec2) */
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void *)(intptr_t)(off));
	off += 2 * (int)sizeof(float);
	/* a_norm (vec2) */
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void *)(intptr_t)(off));
	off += 2 * (int)sizeof(float);
	/* a_em (vec2) */
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void *)(intptr_t)(off));
	off += 2 * (int)sizeof(float);
	/* a_glyph (vec4) */
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void *)(intptr_t)(off));
	off += 4 * (int)sizeof(float);
	/* a_band (vec4) */
	glEnableVertexAttribArray(4);
	glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (void *)(intptr_t)(off));
	off += 4 * (int)sizeof(float);
	/* a_color (vec4) */
	glEnableVertexAttribArray(5);
	glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, (void *)(intptr_t)(off));

	glDrawArrays(GL_TRIANGLES, 0, vert_count);

	for (int i = 0; i < 6; i++)
		glDisableVertexAttribArray(i);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	slug_batch.quad_count = 0;
}

/* Push one glyph quad (6 vertices) into the batch */
static void
slug_push_quad(const slug_glyph_t *g,
               float x0, float y0, float x1, float y1,
               float em_x0, float em_y0, float em_x1, float em_y1,
               float r, float gc, float b, float a)
{
	if (slug_batch.quad_count >= SLUG_MAX_QUADS)
		slug_flush();

	/* Glyph flat data (same for all vertices) */
	float glyph_x = (float)g->band_x;
	float glyph_y = (float)g->band_y;
	float band_max_x = (float)(g->vband_count > 0 ? g->vband_count - 1 : 0);
	float band_max_y = (float)(g->hband_count > 0 ? g->hband_count - 1 : 0);

	/* Band scale and offset */
	float bw = g->bbox_x1 - g->bbox_x0;
	float bh = g->bbox_y1 - g->bbox_y0;
	float band_scale_x = (bw > 1e-6f) ? (float)g->vband_count / bw : 0.0f;
	float band_scale_y = (bh > 1e-6f) ? (float)g->hband_count / bh : 0.0f;
	float band_off_x = -g->bbox_x0 * band_scale_x;
	float band_off_y = -g->bbox_y0 * band_scale_y;

	/* Normals for dilation: outward at each corner */
	/* Corners: TL(0), TR(1), BL(2), BR(3) */
	/* Triangle 1: TL, TR, BL  Triangle 2: TR, BR, BL */
	struct {
		float px, py;     /* position */
		float nx, ny;     /* normal */
		float ex, ey;     /* em-space */
	} corners[4] = {
		{ x0, y0, -1.0f, -1.0f, em_x0, em_y1 },  /* TL (y flipped: screen y0 = em y1) */
		{ x1, y0,  1.0f, -1.0f, em_x1, em_y1 },  /* TR */
		{ x0, y1, -1.0f,  1.0f, em_x0, em_y0 },  /* BL (screen y1 = em y0) */
		{ x1, y1,  1.0f,  1.0f, em_x1, em_y0 },  /* BR */
	};
	int tri_idx[6] = { 0, 1, 2, 1, 3, 2 };

	float *v = &slug_batch.verts[slug_batch.quad_count *
	                              SLUG_VERTS_PER_QUAD *
	                              SLUG_FLOATS_PER_VERT];

	for (int i = 0; i < 6; i++) {
		int ci = tri_idx[i];
		float *dst = v + i * SLUG_FLOATS_PER_VERT;
		dst[0]  = corners[ci].px;       /* a_pos.x */
		dst[1]  = corners[ci].py;       /* a_pos.y */
		dst[2]  = corners[ci].nx;       /* a_norm.x */
		dst[3]  = corners[ci].ny;       /* a_norm.y */
		dst[4]  = corners[ci].ex;       /* a_em.x */
		dst[5]  = corners[ci].ey;       /* a_em.y */
		dst[6]  = glyph_x;             /* a_glyph.x */
		dst[7]  = glyph_y;             /* a_glyph.y */
		dst[8]  = band_max_x;          /* a_glyph.z */
		dst[9]  = band_max_y;          /* a_glyph.w */
		dst[10] = band_scale_x;        /* a_band.x */
		dst[11] = band_scale_y;        /* a_band.y */
		dst[12] = band_off_x;          /* a_band.z */
		dst[13] = band_off_y;          /* a_band.w */
		dst[14] = r;                    /* a_color.r */
		dst[15] = gc;                   /* a_color.g */
		dst[16] = b;                    /* a_color.b */
		dst[17] = a;                    /* a_color.a */
	}

	slug_batch.quad_count++;
}

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

lud_slug_font_t
lud_load_slug_font(const char *path)
{
	lud_slug_font_t out = {0};

	if (lud__state.gles_version < 3) {
		lud_err("Slug fonts require GLES3");
		return out;
	}

	/* Find free slot */
	int idx = -1;
	for (int i = 0; i < MAX_SLUG_FONTS; i++) {
		if (!font_slots[i].used) { idx = i; break; }
	}
	if (idx < 0) {
		lud_err("Slug font pool exhausted");
		return out;
	}

	/* Read file */
	FILE *f = fopen(path, "rb");
	if (!f) {
		lud_err("Cannot open '%s'", path);
		return out;
	}

	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET);

	unsigned char *data = malloc((size_t)fsize);
	if ((long)fread(data, 1, (size_t)fsize, f) != fsize) {
		lud_err("Cannot read '%s'", path);
		fclose(f);
		free(data);
		return out;
	}
	fclose(f);

	/* Validate header */
	if (fsize < (long)sizeof(slug_file_header_t)) {
		lud_err("'%s': file too small", path);
		free(data);
		return out;
	}

	slug_file_header_t hdr;
	memcpy(&hdr, data, sizeof(hdr));

	if (memcmp(hdr.magic, SLUG_MAGIC, 4) != 0) {
		lud_err("'%s': bad magic", path);
		free(data);
		return out;
	}
	if (hdr.version != SLUG_VERSION) {
		lud_err("'%s': unsupported version %d", path, hdr.version);
		free(data);
		return out;
	}

	slug_font_slot_t *s = &font_slots[idx];
	memset(s, 0, sizeof(*s));
	s->hdr = hdr;

	/* Copy glyph table */
	s->glyphs = malloc((size_t)hdr.glyph_count * sizeof(slug_glyph_t));
	memcpy(s->glyphs, data + hdr.glyph_table_off,
	       (size_t)hdr.glyph_count * sizeof(slug_glyph_t));

	/* Copy kerning table */
	if (hdr.kern_table_off && hdr.kern_count > 0) {
		s->kerns = malloc((size_t)hdr.kern_count * sizeof(slug_kern_pair_t));
		memcpy(s->kerns, data + hdr.kern_table_off,
		       (size_t)hdr.kern_count * sizeof(slug_kern_pair_t));
	}

	/* Create curve texture (RGBA16F) */
	s->curve_tex = lud_make_texture(&(lud_texture_desc_t){
		.width = hdr.curve_tex_w,
		.height = hdr.curve_tex_h,
		.format = LUD_PIXFMT_RGBA16F,
		.min_filter = LUD_FILTER_NEAREST,
		.mag_filter = LUD_FILTER_NEAREST,
		.data = data + hdr.curve_data_off,
	});

	/* Create band texture (RG16UI) */
	s->band_tex = lud_make_texture(&(lud_texture_desc_t){
		.width = hdr.band_tex_w,
		.height = hdr.band_tex_h,
		.format = LUD_PIXFMT_RG16UI,
		.min_filter = LUD_FILTER_NEAREST,
		.mag_filter = LUD_FILTER_NEAREST,
		.data = data + hdr.band_data_off,
	});

	free(data);

	s->used = 1;
	out.id = (unsigned)(idx + 1);
	return out;
}

void
lud_destroy_slug_font(lud_slug_font_t font)
{
	slug_font_slot_t *s = get_font_slot(font);
	if (!s) return;
	lud_destroy_texture(s->curve_tex);
	lud_destroy_texture(s->band_tex);
	free(s->glyphs);
	free(s->kerns);
	memset(s, 0, sizeof(*s));
}

void
lud_slug_begin(float vx, float vy, float vw, float vh)
{
	slug_init();
	if (!slug_batch.initialized) return;

	/* Orthographic projection, Y-down */
	memset(slug_batch.proj, 0, sizeof(slug_batch.proj));
	slug_batch.proj[0]  =  2.0f / vw;
	slug_batch.proj[5]  = -2.0f / vh;
	slug_batch.proj[10] = -1.0f;
	slug_batch.proj[12] = -(2.0f * vx / vw + 1.0f);
	slug_batch.proj[13] =  (2.0f * vy / vh + 1.0f);
	slug_batch.proj[15] =  1.0f;

	slug_batch.quad_count = 0;
	slug_batch.current_font.id = 0;

	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void
lud_slug_draw(lud_slug_font_t font, float x, float y,
              float size, float r, float g, float b, float a,
              const char *text)
{
	slug_font_slot_t *s = get_font_slot(font);
	if (!s || !text || !slug_batch.initialized) return;

	/* em_scale: how many view units per em */
	float line_height = s->hdr.ascender - s->hdr.descender;
	float em_scale = size / line_height;

	/* em-space size of one screen pixel (view unit = pixel in our ortho) */
	float em_per_pixel = 1.0f / em_scale;

	/* Flush if font or size changed (em_per_pixel is a per-batch uniform) */
	if (slug_batch.current_font.id != font.id ||
	    slug_batch.em_per_pixel != em_per_pixel) {
		slug_flush();
		slug_batch.current_font = font;
		slug_batch.em_per_pixel = em_per_pixel;
	}

	float cursor_x = x;
	uint32_t prev_cp = 0;

	for (const unsigned char *p = (const unsigned char *)text; *p; ) {
		/* Simple ASCII/Latin-1 decode — skip invalid bytes */
		uint32_t cp = *p++;
		if (cp >= 0xC0 && cp < 0xE0 && p[0]) {
			cp = ((cp & 0x1F) << 6) | (*p++ & 0x3F);
		} else if (cp >= 0xE0 && cp < 0xF0 && p[0] && p[1]) {
			cp = ((cp & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F);
			p += 2;
		} else if (cp >= 0xF0 && cp < 0xF8 && p[0] && p[1] && p[2]) {
			cp = ((cp & 0x07) << 18) | ((p[0] & 0x3F) << 12) |
			     ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
			p += 3;
		}

		const slug_glyph_t *gl = find_glyph(s, cp);
		if (!gl) {
			/* Try space as fallback */
			gl = find_glyph(s, ' ');
			if (!gl) continue;
		}

		/* Apply kerning */
		if (prev_cp) {
			float kern = find_kern(s, prev_cp, cp);
			cursor_x += kern * em_scale;
		}

		/* Only draw if glyph has curves */
		if (gl->curve_count > 0 && gl->hband_count > 0) {
			/* Screen-space quad from glyph bbox */
			float sx0 = cursor_x + gl->bbox_x0 * em_scale;
			float sy0 = y - gl->bbox_y1 * em_scale;  /* Y flipped */
			float sx1 = cursor_x + gl->bbox_x1 * em_scale;
			float sy1 = y - gl->bbox_y0 * em_scale;

			slug_push_quad(gl,
			               sx0, sy0, sx1, sy1,
			               gl->bbox_x0, gl->bbox_y0,
			               gl->bbox_x1, gl->bbox_y1,
			               r, g, b, a);
		}

		cursor_x += gl->advance * em_scale;
		prev_cp = cp;
	}
}

float
lud_slug_text_width(lud_slug_font_t font, float size, const char *text)
{
	slug_font_slot_t *s = get_font_slot(font);
	if (!s || !text) return 0.0f;

	float line_height = s->hdr.ascender - s->hdr.descender;
	float em_scale = size / line_height;
	float width = 0.0f;
	uint32_t prev_cp = 0;

	for (const unsigned char *p = (const unsigned char *)text; *p; ) {
		uint32_t cp = *p++;
		if (cp >= 0xC0 && cp < 0xE0 && p[0]) {
			cp = ((cp & 0x1F) << 6) | (*p++ & 0x3F);
		} else if (cp >= 0xE0 && cp < 0xF0 && p[0] && p[1]) {
			cp = ((cp & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F);
			p += 2;
		} else if (cp >= 0xF0 && cp < 0xF8 && p[0] && p[1] && p[2]) {
			cp = ((cp & 0x07) << 18) | ((p[0] & 0x3F) << 12) |
			     ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
			p += 3;
		}

		const slug_glyph_t *gl = find_glyph(s, cp);
		if (!gl) {
			gl = find_glyph(s, ' ');
			if (!gl) continue;
		}

		if (prev_cp) {
			float kern = find_kern(s, prev_cp, cp);
			width += kern * em_scale;
		}

		width += gl->advance * em_scale;
		prev_cp = cp;
	}

	return width;
}

void
lud_slug_end(void)
{
	if (!slug_batch.initialized) return;
	slug_flush();
}
