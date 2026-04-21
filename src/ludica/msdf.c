/*
 * msdf.c — SDF/MSDF font renderer.
 *
 * Loads .msdffont binary files produced by font2msdf, creates an R8 or
 * RGB8 atlas texture, and renders glyph quads using a simple SDF
 * fragment shader.  Works on GLES2 and GLES3.
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

#include "ludica_internal.h"
#include "ludica_gfx.h"
#include "include/ludica_vfont.h"
#include "msdf_format.h"
#include "slug_format.h" /* slug_kern_pair_t */
#include <GLES2/gl2.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern const char msdf_vert[];
extern const char msdf_frag[];

/* ------------------------------------------------------------------------ */
/* Font slot                                                                 */
/* ------------------------------------------------------------------------ */

#define MAX_MSDF_FONTS 8

typedef struct {
	int used;
	msdf_file_header_t hdr;
	msdf_glyph_t *glyphs;
	slug_kern_pair_t *kerns;
	lud_texture_t atlas_tex;
} msdf_font_slot_t;

static msdf_font_slot_t msdf_slots[MAX_MSDF_FONTS];

static msdf_font_slot_t *
msdf_get_slot(unsigned id)
{
	if (id == 0 || id > MAX_MSDF_FONTS) return NULL;
	msdf_font_slot_t *s = &msdf_slots[id - 1];
	return s->used ? s : NULL;
}

static const msdf_glyph_t *
msdf_find_glyph(const msdf_font_slot_t *s, uint32_t cp)
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

static float
msdf_find_kern(const msdf_font_slot_t *s, uint32_t left, uint32_t right)
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
 * Vertex format: 3 attributes, 8 floats = 32 bytes.
 *   a_pos   (vec2)  — screen-space position
 *   a_uv    (vec2)  — atlas texture coordinate
 *   a_color (vec4)  — RGBA
 */

#define MSDF_FLOATS_PER_VERT  8
#define MSDF_VERTS_PER_QUAD   6
#define MSDF_MAX_QUADS        512

static struct {
	int initialized;
	lud_shader_t shader;
	GLuint vbo;
	float verts[MSDF_MAX_QUADS * MSDF_VERTS_PER_QUAD * MSDF_FLOATS_PER_VERT];
	int quad_count;
	float proj[16];
	unsigned current_font_id;
} msdf_batch;

static void
msdf_init(void)
{
	if (msdf_batch.initialized) return;

	msdf_batch.shader = lud_make_shader(&(lud_shader_desc_t){
		.vert_src = msdf_vert,
		.frag_src = msdf_frag,
		.attrs = { "a_pos", "a_uv", "a_color" },
		.num_attrs = 3,
	});

	glGenBuffers(1, &msdf_batch.vbo);
	msdf_batch.initialized = 1;
}

static void
msdf_flush(void)
{
	if (msdf_batch.quad_count == 0) return;

	msdf_font_slot_t *s = msdf_get_slot(msdf_batch.current_font_id);
	if (!s) { msdf_batch.quad_count = 0; return; }

	int vert_count = msdf_batch.quad_count * MSDF_VERTS_PER_QUAD;
	int stride = MSDF_FLOATS_PER_VERT * (int)sizeof(float);

	lud_apply_shader(msdf_batch.shader);
	lud_uniform_mat4(msdf_batch.shader, "u_proj", msdf_batch.proj);
	lud_uniform_int(msdf_batch.shader, "u_atlas", 0);

	lud_bind_texture(s->atlas_tex, 0);

	glBindBuffer(GL_ARRAY_BUFFER, msdf_batch.vbo);
	glBufferData(GL_ARRAY_BUFFER,
	             (GLsizeiptr)(vert_count * MSDF_FLOATS_PER_VERT * (int)sizeof(float)),
	             msdf_batch.verts, GL_STREAM_DRAW);

	int off = 0;
	/* a_pos (vec2) */
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void *)(intptr_t)(off));
	off += 2 * (int)sizeof(float);
	/* a_uv (vec2) */
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void *)(intptr_t)(off));
	off += 2 * (int)sizeof(float);
	/* a_color (vec4) */
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void *)(intptr_t)(off));

	glDrawArrays(GL_TRIANGLES, 0, vert_count);

	for (int i = 0; i < 3; i++)
		glDisableVertexAttribArray(i);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	msdf_batch.quad_count = 0;
}

static void
msdf_push_quad(float x0, float y0, float x1, float y1,
               float u0, float v0, float u1, float v1,
               float r, float g, float b, float a)
{
	if (msdf_batch.quad_count >= MSDF_MAX_QUADS)
		msdf_flush();

	/* TL(0), TR(1), BL(2), BR(3) */
	struct { float px, py, u, v; } corners[4] = {
		{ x0, y0, u0, v0 },  /* TL */
		{ x1, y0, u1, v0 },  /* TR */
		{ x0, y1, u0, v1 },  /* BL */
		{ x1, y1, u1, v1 },  /* BR */
	};
	int tri_idx[6] = { 0, 1, 2, 1, 3, 2 };

	float *vp = &msdf_batch.verts[msdf_batch.quad_count *
	                               MSDF_VERTS_PER_QUAD *
	                               MSDF_FLOATS_PER_VERT];

	for (int i = 0; i < 6; i++) {
		int ci = tri_idx[i];
		float *dst = vp + i * MSDF_FLOATS_PER_VERT;
		dst[0] = corners[ci].px;
		dst[1] = corners[ci].py;
		dst[2] = corners[ci].u;
		dst[3] = corners[ci].v;
		dst[4] = r;
		dst[5] = g;
		dst[6] = b;
		dst[7] = a;
	}

	msdf_batch.quad_count++;
}

/* UTF-8 decode — shared with slug.c pattern */
static uint32_t
msdf_utf8_next(const unsigned char **pp)
{
	const unsigned char *p = *pp;
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
	*pp = p;
	return cp;
}

/* ------------------------------------------------------------------------ */
/* Public API (called by vfont.c dispatch)                                   */
/* ------------------------------------------------------------------------ */

unsigned
lud_msdf_load(const char *path)
{
	int idx = -1;
	for (int i = 0; i < MAX_MSDF_FONTS; i++) {
		if (!msdf_slots[i].used) { idx = i; break; }
	}
	if (idx < 0) {
		lud_err("MSDF font pool exhausted");
		return 0;
	}

	FILE *f = fopen(path, "rb");
	if (!f) {
		lud_err("Cannot open '%s'", path);
		return 0;
	}

	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET);

	unsigned char *data = malloc((size_t)fsize);
	if ((long)fread(data, 1, (size_t)fsize, f) != fsize) {
		lud_err("Cannot read '%s'", path);
		fclose(f);
		free(data);
		return 0;
	}
	fclose(f);

	if (fsize < (long)sizeof(msdf_file_header_t)) {
		lud_err("'%s': file too small", path);
		free(data);
		return 0;
	}

	msdf_file_header_t hdr;
	memcpy(&hdr, data, sizeof(hdr));

	if (memcmp(hdr.magic, MSDF_MAGIC, 4) != 0) {
		lud_err("'%s': bad magic", path);
		free(data);
		return 0;
	}
	if (hdr.version != MSDF_VERSION) {
		lud_err("'%s': unsupported version %d", path, hdr.version);
		free(data);
		return 0;
	}
	if (hdr.atlas_format != MSDF_ATLAS_RAW) {
		lud_err("'%s': unsupported atlas format %d", path, hdr.atlas_format);
		free(data);
		return 0;
	}

	msdf_font_slot_t *s = &msdf_slots[idx];
	memset(s, 0, sizeof(*s));
	s->hdr = hdr;

	s->glyphs = malloc((size_t)hdr.glyph_count * sizeof(msdf_glyph_t));
	memcpy(s->glyphs, data + hdr.glyph_table_off,
	       (size_t)hdr.glyph_count * sizeof(msdf_glyph_t));

	if (hdr.kern_table_off && hdr.kern_count > 0) {
		s->kerns = malloc((size_t)hdr.kern_count * sizeof(slug_kern_pair_t));
		memcpy(s->kerns, data + hdr.kern_table_off,
		       (size_t)hdr.kern_count * sizeof(slug_kern_pair_t));
	}

	int multichannel = (hdr.flags & MSDF_FLAG_MULTICHANNEL) != 0;
	enum lud_pixel_format fmt = multichannel ? LUD_PIXFMT_RGB8 : LUD_PIXFMT_R8;

	s->atlas_tex = lud_make_texture(&(lud_texture_desc_t){
		.width = hdr.atlas_w,
		.height = hdr.atlas_h,
		.format = fmt,
		.min_filter = LUD_FILTER_LINEAR,
		.mag_filter = LUD_FILTER_LINEAR,
		.data = data + hdr.atlas_data_off,
	});

	free(data);

	s->used = 1;
	return (unsigned)(idx + 1);
}

void
lud_msdf_destroy(unsigned id)
{
	msdf_font_slot_t *s = msdf_get_slot(id);
	if (!s) return;
	lud_destroy_texture(s->atlas_tex);
	free(s->glyphs);
	free(s->kerns);
	memset(s, 0, sizeof(*s));
}

void
lud_msdf_begin(float vx, float vy, float vw, float vh)
{
	msdf_init();
	if (!msdf_batch.initialized) return;

	memset(msdf_batch.proj, 0, sizeof(msdf_batch.proj));
	msdf_batch.proj[0]  =  2.0f / vw;
	msdf_batch.proj[5]  = -2.0f / vh;
	msdf_batch.proj[10] = -1.0f;
	msdf_batch.proj[12] = -(2.0f * vx / vw + 1.0f);
	msdf_batch.proj[13] =  (2.0f * vy / vh + 1.0f);
	msdf_batch.proj[15] =  1.0f;

	msdf_batch.quad_count = 0;
	msdf_batch.current_font_id = 0;

	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void
lud_msdf_draw(unsigned id, float x, float y,
              float size, float r, float g, float b, float a,
              const char *text)
{
	msdf_font_slot_t *s = msdf_get_slot(id);
	if (!s || !text || !msdf_batch.initialized) return;

	float line_height = s->hdr.ascender - s->hdr.descender;
	float em_scale = size / line_height;

	if (msdf_batch.current_font_id != id) {
		msdf_flush();
		msdf_batch.current_font_id = id;
	}

	/* Premultiply alpha for GL_ONE blend */
	float pr = r * a, pg = g * a, pb = b * a;

	float inv_atlas_w = 1.0f / (float)s->hdr.atlas_w;
	float inv_atlas_h = 1.0f / (float)s->hdr.atlas_h;

	float cursor_x = x;
	uint32_t prev_cp = 0;

	for (const unsigned char *p = (const unsigned char *)text; *p; ) {
		uint32_t cp = msdf_utf8_next(&p);

		const msdf_glyph_t *gl = msdf_find_glyph(s, cp);
		if (!gl) {
			gl = msdf_find_glyph(s, ' ');
			if (!gl) continue;
		}

		if (prev_cp) {
			float kern = msdf_find_kern(s, prev_cp, cp);
			cursor_x += kern * em_scale;
		}

		if (gl->atlas_w > 0 && gl->atlas_h > 0) {
			float sx0 = cursor_x + gl->bbox_x0 * em_scale;
			float sy0 = y - gl->bbox_y1 * em_scale;
			float sx1 = cursor_x + gl->bbox_x1 * em_scale;
			float sy1 = y - gl->bbox_y0 * em_scale;

			float u0 = (float)gl->atlas_x * inv_atlas_w;
			float v0 = (float)gl->atlas_y * inv_atlas_h;
			float u1 = (float)(gl->atlas_x + gl->atlas_w) * inv_atlas_w;
			float v1 = (float)(gl->atlas_y + gl->atlas_h) * inv_atlas_h;

			msdf_push_quad(sx0, sy0, sx1, sy1,
			               u0, v0, u1, v1,
			               pr, pg, pb, a);
		}

		cursor_x += gl->advance * em_scale;
		prev_cp = cp;
	}
}

float
lud_msdf_text_width(unsigned id, float size, const char *text)
{
	msdf_font_slot_t *s = msdf_get_slot(id);
	if (!s || !text) return 0.0f;

	float line_height = s->hdr.ascender - s->hdr.descender;
	float em_scale = size / line_height;
	float width = 0.0f;
	uint32_t prev_cp = 0;

	for (const unsigned char *p = (const unsigned char *)text; *p; ) {
		uint32_t cp = msdf_utf8_next(&p);

		const msdf_glyph_t *gl = msdf_find_glyph(s, cp);
		if (!gl) {
			gl = msdf_find_glyph(s, ' ');
			if (!gl) continue;
		}

		if (prev_cp) {
			float kern = msdf_find_kern(s, prev_cp, cp);
			width += kern * em_scale;
		}

		width += gl->advance * em_scale;
		prev_cp = cp;
	}

	return width;
}

void
lud_msdf_end(void)
{
	if (!msdf_batch.initialized) return;
	msdf_flush();
}
