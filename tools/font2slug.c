/*
 * font2slug.c — Convert TTF/OTF fonts to .slugfont binary format.
 *
 * Uses stb_truetype to extract glyph outlines (quadratic Bezier curves),
 * builds horizontal and vertical band structures, packs curve and band
 * data into GPU texture layouts, and writes a .slugfont file.
 *
 * Usage: font2slug input.ttf [-o output.slugfont] [--range 32-255] [--bands 8]
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "../src/ludica/slug_format.h"

/* ------------------------------------------------------------------------ */
/* Constants                                                                 */
/* ------------------------------------------------------------------------ */

#define BAND_TEX_W       4096   /* must match LOG_BAND_TEX_W=12 in shader */
#define CURVE_TEX_W      2048   /* max texels per row (1024 curve pairs) */
#define MAX_GLYPHS       4096
#define MAX_CURVES_GLYPH 2048
#define MAX_BAND_REFS    65536  /* total band→curve references per glyph */

/* Default codepoint range: ASCII printable + Latin-1 Supplement */
#define DEFAULT_RANGE_LO  32
#define DEFAULT_RANGE_HI  255
#define DEFAULT_BANDS     8

/* ------------------------------------------------------------------------ */
/* Float16 conversion                                                        */
/* ------------------------------------------------------------------------ */

static uint16_t
float_to_half(float f)
{
	uint32_t x;
	uint32_t sign, mant;
	int exp;

	memcpy(&x, &f, 4);
	sign = (x >> 16) & 0x8000u;
	exp = (int)((x >> 23) & 0xFFu) - 127 + 15;
	mant = (x >> 13) & 0x3FFu;

	if (exp <= 0)
		return (uint16_t)sign; /* underflow → ±0 */
	if (exp >= 31)
		return (uint16_t)(sign | 0x7C00u); /* overflow → ±inf */
	return (uint16_t)(sign | ((unsigned)exp << 10) | mant);
}

/* ------------------------------------------------------------------------ */
/* Quadratic Bezier curve                                                    */
/* ------------------------------------------------------------------------ */

typedef struct {
	float p1x, p1y;  /* start */
	float p2x, p2y;  /* control */
	float p3x, p3y;  /* end */
} curve_t;

/* ------------------------------------------------------------------------ */
/* Cubic → quadratic conversion                                              */
/* ------------------------------------------------------------------------ */

/*
 * Approximate a cubic Bezier (c0,c1,c2,c3) as quadratic segments.
 * Uses recursive midpoint subdivision. At each level, approximates the
 * cubic with a single quadratic whose control point is the average of
 * the two cubic interior control points. If the error exceeds tolerance,
 * subdivides at t=0.5 via De Casteljau.
 */

static float
cubic_approx_error(float c1x, float c1y, float c2x, float c2y)
{
	/* Error metric: max distance between cubic interior control points
	 * and the single quadratic control point (their average). */
	float mx = (c1x + c2x) * 0.5f;
	float my = (c1y + c2y) * 0.5f;
	float d1x = c1x - mx, d1y = c1y - my;
	float d2x = c2x - mx, d2y = c2y - my;
	float e1 = d1x * d1x + d1y * d1y;
	float e2 = d2x * d2x + d2y * d2y;
	return e1 > e2 ? e1 : e2;
}

static void
cubic_to_quads(float c0x, float c0y, float c1x, float c1y,
               float c2x, float c2y, float c3x, float c3y,
               curve_t *out, int *count, int max_count,
               int max_depth, float tol_sq)
{
	if (max_depth <= 0 || cubic_approx_error(c1x, c1y, c2x, c2y) < tol_sq) {
		/* Approximate as single quadratic */
		if (*count >= max_count) return;
		curve_t *c = &out[*count];
		c->p1x = c0x; c->p1y = c0y;
		c->p2x = (c1x + c2x) * 0.5f;
		c->p2y = (c1y + c2y) * 0.5f;
		c->p3x = c3x; c->p3y = c3y;
		(*count)++;
		return;
	}

	/* De Casteljau split at t=0.5 */
	float m01x = (c0x + c1x) * 0.5f, m01y = (c0y + c1y) * 0.5f;
	float m12x = (c1x + c2x) * 0.5f, m12y = (c1y + c2y) * 0.5f;
	float m23x = (c2x + c3x) * 0.5f, m23y = (c2y + c3y) * 0.5f;
	float m012x = (m01x + m12x) * 0.5f, m012y = (m01y + m12y) * 0.5f;
	float m123x = (m12x + m23x) * 0.5f, m123y = (m12y + m23y) * 0.5f;
	float mx = (m012x + m123x) * 0.5f, my = (m012y + m123y) * 0.5f;

	cubic_to_quads(c0x, c0y, m01x, m01y, m012x, m012y, mx, my,
	               out, count, max_count, max_depth - 1, tol_sq);
	cubic_to_quads(mx, my, m123x, m123y, m23x, m23y, c3x, c3y,
	               out, count, max_count, max_depth - 1, tol_sq);
}

/* ------------------------------------------------------------------------ */
/* Band structures                                                           */
/* ------------------------------------------------------------------------ */

typedef struct {
	int *indices;     /* indices into glyph's curve array */
	int count;
	int capacity;
} band_list_t;

static void
band_list_add(band_list_t *b, int idx)
{
	if (b->count >= b->capacity) {
		b->capacity = b->capacity ? b->capacity * 2 : 16;
		b->indices = realloc(b->indices, (size_t)b->capacity * sizeof(int));
	}
	b->indices[b->count++] = idx;
}

/* ------------------------------------------------------------------------ */
/* Glyph data                                                                */
/* ------------------------------------------------------------------------ */

typedef struct {
	uint32_t codepoint;
	int      glyph_index;
	float    advance;
	float    lsb;
	float    bbox_x0, bbox_y0, bbox_x1, bbox_y1;
	curve_t *curves;
	int      num_curves;
	int      hband_count;
	int      vband_count;
	band_list_t *hbands;  /* [hband_count] */
	band_list_t *vbands;  /* [vband_count] */

	/* Packed locations (filled during texture packing) */
	int      curve_tex_x, curve_tex_y;
	int      band_tex_x, band_tex_y;
} glyph_data_t;

/* ------------------------------------------------------------------------ */
/* Sorting comparators for curves within bands                               */
/* ------------------------------------------------------------------------ */

/* For horizontal bands: sort by descending max x coordinate */
static curve_t *g_sort_curves;  /* set before qsort */

static int
cmp_hband_curve(const void *a, const void *b)
{
	int ia = *(const int *)a, ib = *(const int *)b;
	const curve_t *ca = &g_sort_curves[ia];
	const curve_t *cb = &g_sort_curves[ib];
	float ma = ca->p1x;
	if (ca->p2x > ma) ma = ca->p2x;
	if (ca->p3x > ma) ma = ca->p3x;
	float mb = cb->p1x;
	if (cb->p2x > mb) mb = cb->p2x;
	if (cb->p3x > mb) mb = cb->p3x;
	/* descending */
	if (ma > mb) return -1;
	if (ma < mb) return 1;
	return 0;
}

/* For vertical bands: sort by descending max y coordinate */
static int
cmp_vband_curve(const void *a, const void *b)
{
	int ia = *(const int *)a, ib = *(const int *)b;
	const curve_t *ca = &g_sort_curves[ia];
	const curve_t *cb = &g_sort_curves[ib];
	float ma = ca->p1y;
	if (ca->p2y > ma) ma = ca->p2y;
	if (ca->p3y > ma) ma = ca->p3y;
	float mb = cb->p1y;
	if (cb->p2y > mb) mb = cb->p2y;
	if (cb->p3y > mb) mb = cb->p3y;
	if (ma > mb) return -1;
	if (ma < mb) return 1;
	return 0;
}

/* ------------------------------------------------------------------------ */
/* Process one glyph                                                         */
/* ------------------------------------------------------------------------ */

static int
process_glyph(const stbtt_fontinfo *font, glyph_data_t *g, int nbands,
              float em_scale)
{
	stbtt_vertex *verts = NULL;
	int nv, i;

	nv = stbtt_GetGlyphShape(font, g->glyph_index, &verts);
	if (nv <= 0) {
		/* empty glyph (space, etc.) */
		g->curves = NULL;
		g->num_curves = 0;
		g->hband_count = 0;
		g->vband_count = 0;
		g->hbands = NULL;
		g->vbands = NULL;
		return 0;
	}

	/* Extract horizontal metrics */
	{
		int adv, lsb_raw;
		stbtt_GetGlyphHMetrics(font, g->glyph_index, &adv, &lsb_raw);
		g->advance = (float)adv * em_scale;
		g->lsb = (float)lsb_raw * em_scale;
	}

	/* Extract bounding box */
	{
		int x0, y0, x1, y1;
		stbtt_GetGlyphBox(font, g->glyph_index, &x0, &y0, &x1, &y1);
		g->bbox_x0 = (float)x0 * em_scale;
		g->bbox_y0 = (float)y0 * em_scale;
		g->bbox_x1 = (float)x1 * em_scale;
		g->bbox_y1 = (float)y1 * em_scale;
	}

	/* Allocate curves (generous upper bound: cubics can split up to 2^4=16) */
	int max_curves = nv * 16;
	curve_t *curves = calloc((size_t)max_curves, sizeof(curve_t));
	int nc = 0;
	float last_x = 0, last_y = 0;

	/* Tolerance for cubic→quadratic subdivision (in em-space, squared) */
	float tol_sq = (0.001f) * (0.001f); /* ~0.1% of 1 em */

	for (i = 0; i < nv; i++) {
		float x = (float)verts[i].x * em_scale;
		float y = (float)verts[i].y * em_scale;

		switch (verts[i].type) {
		case STBTT_vmove:
			last_x = x; last_y = y;
			break;
		case STBTT_vline:
			/* Degenerate quadratic: control point at midpoint */
			if (nc < max_curves) {
				curves[nc].p1x = last_x;
				curves[nc].p1y = last_y;
				curves[nc].p2x = (last_x + x) * 0.5f;
				curves[nc].p2y = (last_y + y) * 0.5f;
				curves[nc].p3x = x;
				curves[nc].p3y = y;
				nc++;
			}
			last_x = x; last_y = y;
			break;
		case STBTT_vcurve: {
			float cx = (float)verts[i].cx * em_scale;
			float cy = (float)verts[i].cy * em_scale;
			if (nc < max_curves) {
				curves[nc].p1x = last_x;
				curves[nc].p1y = last_y;
				curves[nc].p2x = cx;
				curves[nc].p2y = cy;
				curves[nc].p3x = x;
				curves[nc].p3y = y;
				nc++;
			}
			last_x = x; last_y = y;
			break;
		}
		case STBTT_vcubic: {
			float cx1 = (float)verts[i].cx  * em_scale;
			float cy1 = (float)verts[i].cy  * em_scale;
			float cx2 = (float)verts[i].cx1 * em_scale;
			float cy2 = (float)verts[i].cy1 * em_scale;
			cubic_to_quads(last_x, last_y, cx1, cy1, cx2, cy2, x, y,
			               curves, &nc, max_curves, 4, tol_sq);
			last_x = x; last_y = y;
			break;
		}
		}
	}

	stbtt_FreeShape(font, verts);

	/* Trim allocation */
	{
		curve_t *trimmed = realloc(curves, (size_t)nc * sizeof(curve_t));
		g->curves = trimmed ? trimmed : curves;
	}
	g->num_curves = nc;

	/* Recompute bbox from actual quadratic curves (cubic→quadratic
	 * conversion can produce control points outside the original bbox) */
	if (nc > 0) {
		float bx0 = 1e30f, by0 = 1e30f, bx1 = -1e30f, by1 = -1e30f;
		for (int ci = 0; ci < nc; ci++) {
			curve_t *c = &g->curves[ci];
			float pts[6] = { c->p1x, c->p1y, c->p2x, c->p2y, c->p3x, c->p3y };
			for (int j = 0; j < 6; j += 2) {
				if (pts[j]   < bx0) bx0 = pts[j];
				if (pts[j]   > bx1) bx1 = pts[j];
				if (pts[j+1] < by0) by0 = pts[j+1];
				if (pts[j+1] > by1) by1 = pts[j+1];
			}
		}
		g->bbox_x0 = bx0;
		g->bbox_y0 = by0;
		g->bbox_x1 = bx1;
		g->bbox_y1 = by1;
	}

	if (nc == 0) {
		g->hband_count = 0;
		g->vband_count = 0;
		g->hbands = NULL;
		g->vbands = NULL;
		return 0;
	}

	/* Build bands */
	float bw = g->bbox_x1 - g->bbox_x0;
	float bh = g->bbox_y1 - g->bbox_y0;

	if (bw < 1e-6f) bw = 1e-6f;
	if (bh < 1e-6f) bh = 1e-6f;

	int hbc = nbands, vbc = nbands;

	/* Don't allocate more bands than make sense for small glyphs */
	if (hbc > nc) hbc = nc;
	if (vbc > nc) vbc = nc;
	if (hbc < 1) hbc = 1;
	if (vbc < 1) vbc = 1;

	g->hband_count = hbc;
	g->vband_count = vbc;
	g->hbands = calloc((size_t)hbc, sizeof(band_list_t));
	g->vbands = calloc((size_t)vbc, sizeof(band_list_t));

	float hband_size = bh / (float)hbc;
	float vband_size = bw / (float)vbc;

	/* Assign curves to bands */
	for (i = 0; i < nc; i++) {
		const curve_t *c = &g->curves[i];

		/* Curve Y range → horizontal bands */
		float cy_min = c->p1y;
		if (c->p2y < cy_min) cy_min = c->p2y;
		if (c->p3y < cy_min) cy_min = c->p3y;
		float cy_max = c->p1y;
		if (c->p2y > cy_max) cy_max = c->p2y;
		if (c->p3y > cy_max) cy_max = c->p3y;

		int hb_lo = (int)floorf((cy_min - g->bbox_y0) / hband_size);
		int hb_hi = (int)floorf((cy_max - g->bbox_y0) / hband_size);
		if (hb_lo < 0) hb_lo = 0;
		if (hb_hi >= hbc) hb_hi = hbc - 1;

		for (int b = hb_lo; b <= hb_hi; b++)
			band_list_add(&g->hbands[b], i);

		/* Curve X range → vertical bands */
		float cx_min = c->p1x;
		if (c->p2x < cx_min) cx_min = c->p2x;
		if (c->p3x < cx_min) cx_min = c->p3x;
		float cx_max = c->p1x;
		if (c->p2x > cx_max) cx_max = c->p2x;
		if (c->p3x > cx_max) cx_max = c->p3x;

		int vb_lo = (int)floorf((cx_min - g->bbox_x0) / vband_size);
		int vb_hi = (int)floorf((cx_max - g->bbox_x0) / vband_size);
		if (vb_lo < 0) vb_lo = 0;
		if (vb_hi >= vbc) vb_hi = vbc - 1;

		for (int b = vb_lo; b <= vb_hi; b++)
			band_list_add(&g->vbands[b], i);
	}

	/* Sort curves within each band by descending max coordinate */
	g_sort_curves = g->curves;

	for (i = 0; i < hbc; i++) {
		if (g->hbands[i].count > 1)
			qsort(g->hbands[i].indices, (size_t)g->hbands[i].count,
			      sizeof(int), cmp_hband_curve);
	}
	for (i = 0; i < vbc; i++) {
		if (g->vbands[i].count > 1)
			qsort(g->vbands[i].indices, (size_t)g->vbands[i].count,
			      sizeof(int), cmp_vband_curve);
	}

	return 0;
}

/* ------------------------------------------------------------------------ */
/* Texture packing                                                           */
/* ------------------------------------------------------------------------ */

/*
 * Curve texture: RGBA16F, each curve = 2 consecutive texels in same row.
 * Texel 0: (p1.x, p1.y, p2.x, p2.y)
 * Texel 1: (p3.x, p3.y, 0, 0)
 *
 * Band texture: RG16UI, layout per glyph:
 * [hband headers] [vband headers] [curve ref lists...]
 * Each header: (curve_count, offset_to_curve_list)
 * Each curve ref: (curve_tex_x, curve_tex_y)
 */

typedef struct {
	uint16_t *data;  /* RGBA16F: 4 components per texel */
	int w, h;
	int cursor_x, cursor_y;  /* next free texel */
} curve_tex_t;

typedef struct {
	uint16_t *data;  /* RG16UI: 2 components per texel */
	int w, h;
	int cursor;  /* linear offset from start (wraps at w) */
} band_tex_t;

static void
curve_tex_init(curve_tex_t *t, int max_curves)
{
	t->w = CURVE_TEX_W;
	/* each curve = 2 texels, estimate rows needed */
	int pairs = max_curves;
	int pairs_per_row = t->w / 2;
	t->h = (pairs + pairs_per_row - 1) / pairs_per_row;
	if (t->h < 1) t->h = 1;
	/* round to power of 2 for GPU friendliness */
	int h = 1;
	while (h < t->h) h <<= 1;
	t->h = h;
	t->data = calloc((size_t)t->w * (size_t)t->h * 4, sizeof(uint16_t));
	t->cursor_x = 0;
	t->cursor_y = 0;
}

/* Returns texel (x,y) of the first texel of the curve pair */
static void
curve_tex_add(curve_tex_t *t, const curve_t *c, int *out_x, int *out_y)
{
	/* Need 2 consecutive texels in same row */
	if (t->cursor_x + 2 > t->w) {
		t->cursor_x = 0;
		t->cursor_y++;
	}
	if (t->cursor_y >= t->h) {
		/* Grow texture height */
		int new_h = t->h * 2;
		t->data = realloc(t->data, (size_t)t->w * (size_t)new_h * 4 * sizeof(uint16_t));
		memset(t->data + (size_t)t->w * (size_t)t->h * 4, 0,
		       (size_t)t->w * (size_t)(new_h - t->h) * 4 * sizeof(uint16_t));
		t->h = new_h;
	}

	int x = t->cursor_x, y = t->cursor_y;
	size_t off0 = ((size_t)y * (size_t)t->w + (size_t)x) * 4;
	size_t off1 = off0 + 4;

	/* Texel 0: p1.x, p1.y, p2.x, p2.y */
	t->data[off0 + 0] = float_to_half(c->p1x);
	t->data[off0 + 1] = float_to_half(c->p1y);
	t->data[off0 + 2] = float_to_half(c->p2x);
	t->data[off0 + 3] = float_to_half(c->p2y);

	/* Texel 1: p3.x, p3.y, 0, 0 */
	t->data[off1 + 0] = float_to_half(c->p3x);
	t->data[off1 + 1] = float_to_half(c->p3y);
	t->data[off1 + 2] = 0;
	t->data[off1 + 3] = 0;

	*out_x = x;
	*out_y = y;
	t->cursor_x = x + 2;
}

static void
band_tex_init(band_tex_t *t, int total_texels)
{
	t->w = BAND_TEX_W;
	t->h = (total_texels + t->w - 1) / t->w;
	if (t->h < 1) t->h = 1;
	int h = 1;
	while (h < t->h) h <<= 1;
	t->h = h;
	t->data = calloc((size_t)t->w * (size_t)t->h * 2, sizeof(uint16_t));
	t->cursor = 0;
}

static void
band_tex_set(band_tex_t *t, int linear_off, uint16_t r, uint16_t g)
{
	int x = linear_off % t->w;
	int y = linear_off / t->w;
	if (y >= t->h) {
		int new_h = t->h * 2;
		while (y >= new_h) new_h *= 2;
		t->data = realloc(t->data, (size_t)t->w * (size_t)new_h * 2 * sizeof(uint16_t));
		memset(t->data + (size_t)t->w * (size_t)t->h * 2, 0,
		       (size_t)t->w * (size_t)(new_h - t->h) * 2 * sizeof(uint16_t));
		t->h = new_h;
	}
	size_t off = ((size_t)y * (size_t)t->w + (size_t)x) * 2;
	t->data[off + 0] = r;
	t->data[off + 1] = g;
}

/* Allocate a contiguous block of texels in the band texture.
 * Returns the linear offset of the first texel. */
static int
band_tex_alloc(band_tex_t *t, int count)
{
	int off = t->cursor;
	t->cursor += count;
	return off;
}

/* ------------------------------------------------------------------------ */
/* Kerning                                                                   */
/* ------------------------------------------------------------------------ */

typedef struct {
	uint32_t left_cp;
	uint32_t right_cp;
	float    adjust;
} kern_entry_t;

static int
cmp_kern(const void *a, const void *b)
{
	const kern_entry_t *ka = a, *kb = b;
	uint64_t va = ((uint64_t)ka->left_cp << 32) | ka->right_cp;
	uint64_t vb = ((uint64_t)kb->left_cp << 32) | kb->right_cp;
	if (va < vb) return -1;
	if (va > vb) return 1;
	return 0;
}

/* ------------------------------------------------------------------------ */
/* Glyph codepoint comparator                                                */
/* ------------------------------------------------------------------------ */

static int
cmp_glyph_cp(const void *a, const void *b)
{
	const glyph_data_t *ga = a, *gb = b;
	if (ga->codepoint < gb->codepoint) return -1;
	if (ga->codepoint > gb->codepoint) return 1;
	return 0;
}

/* ------------------------------------------------------------------------ */
/* Usage                                                                     */
/* ------------------------------------------------------------------------ */

static void
usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s input.ttf [-o output.slugfont] [--range LO-HI] [--bands N]\n"
		"\n"
		"Options:\n"
		"  -o PATH      Output file (default: input with .slugfont extension)\n"
		"  --range LO-HI  Codepoint range (default: %d-%d)\n"
		"  --bands N    Band subdivision count (default: %d)\n"
		"  -v           Verbose output\n",
		prog, DEFAULT_RANGE_LO, DEFAULT_RANGE_HI, DEFAULT_BANDS);
}

/* ------------------------------------------------------------------------ */
/* Main                                                                      */
/* ------------------------------------------------------------------------ */

int
main(int argc, char **argv)
{
	const char *input_path = NULL;
	const char *output_path = NULL;
	int range_lo = DEFAULT_RANGE_LO;
	int range_hi = DEFAULT_RANGE_HI;
	int nbands = DEFAULT_BANDS;
	int verbose = 0;

	/* Parse arguments */
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
			output_path = argv[++i];
		} else if (strcmp(argv[i], "--range") == 0 && i + 1 < argc) {
			if (sscanf(argv[++i], "%d-%d", &range_lo, &range_hi) != 2) {
				fprintf(stderr, "Error: bad range format (expected LO-HI)\n");
				return 1;
			}
		} else if (strcmp(argv[i], "--bands") == 0 && i + 1 < argc) {
			nbands = atoi(argv[++i]);
			if (nbands < 1 || nbands > 64) {
				fprintf(stderr, "Error: bands must be 1-64\n");
				return 1;
			}
		} else if (strcmp(argv[i], "-v") == 0) {
			verbose = 1;
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			usage(argv[0]);
			return 0;
		} else if (argv[i][0] != '-') {
			input_path = argv[i];
		} else {
			fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
			usage(argv[0]);
			return 1;
		}
	}

	if (!input_path) {
		fprintf(stderr, "Error: no input file\n");
		usage(argv[0]);
		return 1;
	}

	/* Default output path */
	char output_buf[1024];
	if (!output_path) {
		const char *dot = strrchr(input_path, '.');
		size_t base_len = dot ? (size_t)(dot - input_path) : strlen(input_path);
		if (base_len + 10 >= sizeof(output_buf)) {
			fprintf(stderr, "Error: path too long\n");
			return 1;
		}
		memcpy(output_buf, input_path, base_len);
		memcpy(output_buf + base_len, ".slugfont", 10);
		output_path = output_buf;
	}

	/* Load font file */
	FILE *fin = fopen(input_path, "rb");
	if (!fin) {
		fprintf(stderr, "Error: cannot open '%s'\n", input_path);
		return 1;
	}
	fseek(fin, 0, SEEK_END);
	long fsize = ftell(fin);
	fseek(fin, 0, SEEK_SET);
	unsigned char *font_data = malloc((size_t)fsize);
	if ((long)fread(font_data, 1, (size_t)fsize, fin) != fsize) {
		fprintf(stderr, "Error: cannot read '%s'\n", input_path);
		fclose(fin);
		free(font_data);
		return 1;
	}
	fclose(fin);

	/* Init stb_truetype */
	stbtt_fontinfo font;
	int font_off = stbtt_GetFontOffsetForIndex(font_data, 0);
	if (font_off < 0 || !stbtt_InitFont(&font, font_data, font_off)) {
		fprintf(stderr, "Error: not a valid TTF/OTF font\n");
		free(font_data);
		return 1;
	}

	/* Extract font metrics */
	int units_per_em_raw = *(uint16_t *)(font.data + font.head + 18);
	/* head.unitsPerEm is big-endian in the font file */
	units_per_em_raw = ((units_per_em_raw & 0xFF) << 8) | ((units_per_em_raw >> 8) & 0xFF);
	float units_per_em = (float)units_per_em_raw;
	float em_scale = 1.0f / units_per_em;

	int raw_ascent, raw_descent, raw_line_gap;
	stbtt_GetFontVMetrics(&font, &raw_ascent, &raw_descent, &raw_line_gap);
	float ascender  = (float)raw_ascent  * em_scale;
	float descender = (float)raw_descent * em_scale;
	float line_gap  = (float)raw_line_gap * em_scale;

	/* Cap height: try to measure from 'H' glyph bbox */
	float cap_height = 0.0f;
	{
		int gi = stbtt_FindGlyphIndex(&font, 'H');
		if (gi > 0) {
			int x0, y0, x1, y1;
			if (stbtt_GetGlyphBox(&font, gi, &x0, &y0, &x1, &y1))
				cap_height = (float)y1 * em_scale;
		}
	}

	if (verbose) {
		fprintf(stderr, "Font: %s\n", input_path);
		fprintf(stderr, "  units/em: %d\n", units_per_em_raw);
		fprintf(stderr, "  ascender: %.4f  descender: %.4f  line_gap: %.4f\n",
		        ascender, descender, line_gap);
		fprintf(stderr, "  cap_height: %.4f\n", cap_height);
		fprintf(stderr, "  range: U+%04X - U+%04X  bands: %d\n",
		        range_lo, range_hi, nbands);
	}

	/* Enumerate glyphs */
	glyph_data_t *glyphs = calloc(MAX_GLYPHS, sizeof(glyph_data_t));
	int glyph_count = 0;

	for (int cp = range_lo; cp <= range_hi && glyph_count < MAX_GLYPHS; cp++) {
		int gi = stbtt_FindGlyphIndex(&font, cp);
		if (gi <= 0) continue;

		glyph_data_t *g = &glyphs[glyph_count];
		g->codepoint = (uint32_t)cp;
		g->glyph_index = gi;

		process_glyph(&font, g, nbands, em_scale);

		/* Even empty glyphs (space) need advance */
		if (g->num_curves == 0) {
			int adv, lsb_raw;
			stbtt_GetGlyphHMetrics(&font, gi, &adv, &lsb_raw);
			g->advance = (float)adv * em_scale;
			g->lsb = (float)lsb_raw * em_scale;
		}

		glyph_count++;
	}

	/* Sort glyphs by codepoint */
	qsort(glyphs, (size_t)glyph_count, sizeof(glyph_data_t), cmp_glyph_cp);

	if (verbose)
		fprintf(stderr, "  glyphs: %d\n", glyph_count);

	/* Estimate total curves and band texels */
	int total_curves = 0;
	int total_band_texels = 0;

	for (int i = 0; i < glyph_count; i++) {
		glyph_data_t *g = &glyphs[i];
		total_curves += g->num_curves;

		/* Band texels: headers + curve references */
		int headers = g->hband_count + g->vband_count;
		int refs = 0;
		for (int b = 0; b < g->hband_count; b++)
			refs += g->hbands[b].count;
		for (int b = 0; b < g->vband_count; b++)
			refs += g->vbands[b].count;
		total_band_texels += headers + refs;
	}

	if (verbose) {
		fprintf(stderr, "  total curves: %d\n", total_curves);
		fprintf(stderr, "  total band texels: %d\n", total_band_texels);
	}

	/* Pack curve texture */
	curve_tex_t ctx;
	curve_tex_init(&ctx, total_curves);

	/* For each glyph, pack its curves and remember curve texture locations */
	/* We need a map from (glyph_index, curve_index) → (tex_x, tex_y) */
	typedef struct { int x, y; } texloc_t;
	texloc_t **curve_locs = calloc((size_t)glyph_count, sizeof(texloc_t *));

	for (int i = 0; i < glyph_count; i++) {
		glyph_data_t *g = &glyphs[i];
		if (g->num_curves == 0) continue;

		curve_locs[i] = calloc((size_t)g->num_curves, sizeof(texloc_t));

		/* Record glyph's first curve location */
		g->curve_tex_x = ctx.cursor_x;
		g->curve_tex_y = ctx.cursor_y;

		for (int j = 0; j < g->num_curves; j++) {
			int tx, ty;
			curve_tex_add(&ctx, &g->curves[j], &tx, &ty);
			curve_locs[i][j].x = tx;
			curve_locs[i][j].y = ty;
		}
	}

	/* Pack band texture */
	band_tex_t btx;
	band_tex_init(&btx, total_band_texels);

	for (int i = 0; i < glyph_count; i++) {
		glyph_data_t *g = &glyphs[i];
		int hbc = g->hband_count;
		int vbc = g->vband_count;
		if (hbc + vbc == 0) continue;

		int num_headers = hbc + vbc;
		int num_refs = 0;
		for (int b = 0; b < hbc; b++) num_refs += g->hbands[b].count;
		for (int b = 0; b < vbc; b++) num_refs += g->vbands[b].count;

		int base = band_tex_alloc(&btx, num_headers + num_refs);
		g->band_tex_x = base % BAND_TEX_W;
		g->band_tex_y = base / BAND_TEX_W;

		/* Write band headers and curve reference lists */
		int ref_off = num_headers; /* offset from base to first ref */

		/* Horizontal bands */
		for (int b = 0; b < hbc; b++) {
			uint16_t count = (uint16_t)g->hbands[b].count;
			uint16_t offset = (uint16_t)ref_off;
			band_tex_set(&btx, base + b, count, offset);

			/* Write curve references */
			for (int ci = 0; ci < g->hbands[b].count; ci++) {
				int cidx = g->hbands[b].indices[ci];
				uint16_t cx = (uint16_t)curve_locs[i][cidx].x;
				uint16_t cy = (uint16_t)curve_locs[i][cidx].y;
				band_tex_set(&btx, base + ref_off + ci, cx, cy);
			}
			ref_off += g->hbands[b].count;
		}

		/* Vertical bands */
		for (int b = 0; b < vbc; b++) {
			uint16_t count = (uint16_t)g->vbands[b].count;
			uint16_t offset = (uint16_t)ref_off;
			band_tex_set(&btx, base + hbc + b, count, offset);

			for (int ci = 0; ci < g->vbands[b].count; ci++) {
				int cidx = g->vbands[b].indices[ci];
				uint16_t cx = (uint16_t)curve_locs[i][cidx].x;
				uint16_t cy = (uint16_t)curve_locs[i][cidx].y;
				band_tex_set(&btx, base + ref_off + ci, cx, cy);
			}
			ref_off += g->vbands[b].count;
		}
	}

	/* Extract kerning pairs */
	kern_entry_t *kerns = NULL;
	int kern_count = 0;
	{
		int kt_len = stbtt_GetKerningTableLength(&font);
		if (kt_len > 0) {
			stbtt_kerningentry *raw = calloc((size_t)kt_len, sizeof(stbtt_kerningentry));
			kt_len = stbtt_GetKerningTable(&font, raw, kt_len);

			/* Filter to only include codepoints in our range.
			 * stbtt kerning entries use glyph indices, so we need to map
			 * back to codepoints. Build a reverse map. */
			/* For simplicity, iterate our glyphs and check kerning pairs
			 * between all pairs in our set. */
			kerns = calloc((size_t)kt_len, sizeof(kern_entry_t));

			for (int k = 0; k < kt_len; k++) {
				/* The kerning table uses glyph indices.
				 * We need to find which of our codepoints these map to. */
				int found_left = -1, found_right = -1;
				for (int g = 0; g < glyph_count; g++) {
					if (glyphs[g].glyph_index == raw[k].glyph1)
						found_left = (int)glyphs[g].codepoint;
					if (glyphs[g].glyph_index == raw[k].glyph2)
						found_right = (int)glyphs[g].codepoint;
					if (found_left >= 0 && found_right >= 0) break;
				}
				if (found_left >= 0 && found_right >= 0 && raw[k].advance != 0) {
					kerns[kern_count].left_cp = (uint32_t)found_left;
					kerns[kern_count].right_cp = (uint32_t)found_right;
					kerns[kern_count].adjust = (float)raw[k].advance * em_scale;
					kern_count++;
				}
			}

			free(raw);
			qsort(kerns, (size_t)kern_count, sizeof(kern_entry_t), cmp_kern);
		}
	}

	if (verbose)
		fprintf(stderr, "  kern pairs: %d\n", kern_count);

	/* Compute actual texture heights used */
	int curve_tex_h_used = ctx.cursor_y + (ctx.cursor_x > 0 ? 1 : 0);
	if (curve_tex_h_used < 1) curve_tex_h_used = 1;
	/* Round up to power of 2 */
	int ct_h = 1;
	while (ct_h < curve_tex_h_used) ct_h <<= 1;

	int band_tex_h_used = (btx.cursor + BAND_TEX_W - 1) / BAND_TEX_W;
	if (band_tex_h_used < 1) band_tex_h_used = 1;
	int bt_h = 1;
	while (bt_h < band_tex_h_used) bt_h <<= 1;

	if (verbose) {
		fprintf(stderr, "  curve texture: %d x %d (used %d rows)\n",
		        ctx.w, ct_h, curve_tex_h_used);
		fprintf(stderr, "  band texture: %d x %d (used %d rows)\n",
		        btx.w, bt_h, band_tex_h_used);
	}

	/* Write .slugfont file */
	FILE *fout = fopen(output_path, "wb");
	if (!fout) {
		fprintf(stderr, "Error: cannot create '%s'\n", output_path);
		return 1;
	}

	/* Calculate offsets */
	uint32_t glyph_off  = (uint32_t)sizeof(slug_file_header_t);
	uint32_t kern_off   = glyph_off + (uint32_t)(glyph_count * (int)sizeof(slug_glyph_t));
	uint32_t curve_off  = kern_off + (uint32_t)(kern_count * (int)sizeof(slug_kern_pair_t));
	uint32_t band_off   = curve_off + (uint32_t)((size_t)ctx.w * (size_t)ct_h * 8);

	/* Write header */
	slug_file_header_t hdr;
	memset(&hdr, 0, sizeof(hdr));
	memcpy(hdr.magic, SLUG_MAGIC, 4);
	hdr.version = SLUG_VERSION;
	hdr.flags = 0;
	hdr.glyph_count = (uint16_t)glyph_count;
	hdr.kern_count = (uint16_t)kern_count;
	hdr.units_per_em = units_per_em;
	hdr.ascender = ascender;
	hdr.descender = descender;
	hdr.line_gap = line_gap;
	hdr.cap_height = cap_height;
	hdr.curve_tex_w = (uint16_t)ctx.w;
	hdr.curve_tex_h = (uint16_t)ct_h;
	hdr.band_tex_w = (uint16_t)btx.w;
	hdr.band_tex_h = (uint16_t)bt_h;
	hdr.glyph_table_off = glyph_off;
	hdr.kern_table_off = kern_count > 0 ? kern_off : 0;
	hdr.curve_data_off = curve_off;
	hdr.band_data_off = band_off;
	fwrite(&hdr, sizeof(hdr), 1, fout);

	/* Write glyph table */
	for (int i = 0; i < glyph_count; i++) {
		glyph_data_t *g = &glyphs[i];
		slug_glyph_t sg;
		memset(&sg, 0, sizeof(sg));
		sg.codepoint = g->codepoint;
		sg.advance = g->advance;
		sg.lsb = g->lsb;
		sg.bbox_x0 = g->bbox_x0;
		sg.bbox_y0 = g->bbox_y0;
		sg.bbox_x1 = g->bbox_x1;
		sg.bbox_y1 = g->bbox_y1;
		sg.curve_x = (uint16_t)g->curve_tex_x;
		sg.curve_y = (uint16_t)g->curve_tex_y;
		sg.band_x = (uint16_t)g->band_tex_x;
		sg.band_y = (uint16_t)g->band_tex_y;
		sg.hband_count = (uint8_t)g->hband_count;
		sg.vband_count = (uint8_t)g->vband_count;
		sg.curve_count = (uint16_t)g->num_curves;
		fwrite(&sg, sizeof(sg), 1, fout);
	}

	/* Write kerning table */
	for (int i = 0; i < kern_count; i++) {
		slug_kern_pair_t kp;
		kp.left = kerns[i].left_cp;
		kp.right = kerns[i].right_cp;
		kp.adjust = kerns[i].adjust;
		fwrite(&kp, sizeof(kp), 1, fout);
	}

	/* Write curve texture data (RGBA16F = 4 × uint16 per texel = 8 bytes) */
	fwrite(ctx.data, 2, (size_t)ctx.w * (size_t)ct_h * 4, fout);

	/* Write band texture data (RG16UI = 2 × uint16 per texel = 4 bytes) */
	fwrite(btx.data, 2, (size_t)btx.w * (size_t)bt_h * 2, fout);

	fclose(fout);

	if (verbose) {
		long out_size = (long)band_off + (long)btx.w * (long)bt_h * 4;
		fprintf(stderr, "  output: %s (%ld bytes)\n", output_path, out_size);
	} else {
		fprintf(stderr, "%s: %d glyphs, %d curves, %d kerns -> %s\n",
		        input_path, glyph_count, total_curves, kern_count, output_path);
	}

	/* Cleanup */
	for (int i = 0; i < glyph_count; i++) {
		free(glyphs[i].curves);
		for (int b = 0; b < glyphs[i].hband_count; b++)
			free(glyphs[i].hbands[b].indices);
		for (int b = 0; b < glyphs[i].vband_count; b++)
			free(glyphs[i].vbands[b].indices);
		free(glyphs[i].hbands);
		free(glyphs[i].vbands);
		free(curve_locs[i]);
	}
	free(curve_locs);
	free(glyphs);
	free(kerns);
	free(ctx.data);
	free(btx.data);
	free(font_data);

	return 0;
}
