/*
 * font2msdf.c — Convert TTF/OTF fonts to .msdffont binary format.
 *
 * Uses stb_truetype's built-in SDF rasterizer (stbtt_GetGlyphSDF) to
 * produce single-channel signed distance field bitmaps, packs them into
 * an atlas, and writes a .msdffont file.
 *
 * Usage: font2msdf input.ttf [-o output.msdffont] [--range 32-255]
 *                             [--size 48] [--pxrange 4] [-v]
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

#include "../src/ludica/msdf_format.h"
#include "../src/ludica/slug_format.h" /* slug_kern_pair_t */

/* ------------------------------------------------------------------------ */
/* Constants                                                                 */
/* ------------------------------------------------------------------------ */

#define MAX_GLYPHS        4096
#define MAX_ATLAS_DIM     4096

#define DEFAULT_RANGE_LO  32
#define DEFAULT_RANGE_HI  255
#define DEFAULT_SIZE      48
#define DEFAULT_PXRANGE   4

/* ------------------------------------------------------------------------ */
/* Glyph data                                                                */
/* ------------------------------------------------------------------------ */

typedef struct {
	uint32_t codepoint;
	int      glyph_index;
	float    advance;      /* em-space */
	float    lsb;          /* em-space */
	float    bbox_x0, bbox_y0, bbox_x1, bbox_y1; /* em-space */

	/* SDF bitmap from stbtt_GetGlyphSDF */
	unsigned char *sdf_pixels;
	int sdf_w, sdf_h;
	int sdf_xoff, sdf_yoff;

	/* Packed atlas position */
	int atlas_x, atlas_y;
} glyph_data_t;

/* ------------------------------------------------------------------------ */
/* Comparators                                                               */
/* ------------------------------------------------------------------------ */

static int
cmp_glyph_cp(const void *a, const void *b)
{
	const glyph_data_t *ga = a, *gb = b;
	if (ga->codepoint < gb->codepoint) return -1;
	if (ga->codepoint > gb->codepoint) return 1;
	return 0;
}

/* Sort by descending height for shelf packing */
static int
cmp_glyph_height_desc(const void *a, const void *b)
{
	const glyph_data_t *ga = a, *gb = b;
	if (ga->sdf_h > gb->sdf_h) return -1;
	if (ga->sdf_h < gb->sdf_h) return 1;
	return 0;
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
/* Atlas packing (shelf algorithm)                                           */
/* ------------------------------------------------------------------------ */

static int
pack_atlas(glyph_data_t *glyphs, int count, int *out_w, int *out_h)
{
	/* Estimate total area */
	int total_area = 0;
	int max_h = 0;
	for (int i = 0; i < count; i++) {
		total_area += glyphs[i].sdf_w * glyphs[i].sdf_h;
		if (glyphs[i].sdf_h > max_h) max_h = glyphs[i].sdf_h;
	}

	/* Start with a square-ish atlas, round up to power of 2 */
	int side = (int)ceilf(sqrtf((float)total_area));
	int w = 64;
	while (w < side) w <<= 1;
	if (w > MAX_ATLAS_DIM) w = MAX_ATLAS_DIM;
	int h = 64;
	while (h < max_h) h <<= 1;

	/* Sort by height for better shelf utilization */
	qsort(glyphs, (size_t)count, sizeof(glyph_data_t), cmp_glyph_height_desc);

	/* Try packing, grow height as needed */
	for (;;) {
		int cx = 0, cy = 0, shelf_h = 0;
		int ok = 1;

		for (int i = 0; i < count; i++) {
			if (glyphs[i].sdf_w == 0 || glyphs[i].sdf_h == 0) {
				glyphs[i].atlas_x = 0;
				glyphs[i].atlas_y = 0;
				continue;
			}
			if (cx + glyphs[i].sdf_w > w) {
				cx = 0;
				cy += shelf_h;
				shelf_h = 0;
			}
			if (cy + glyphs[i].sdf_h > h) {
				ok = 0;
				break;
			}
			glyphs[i].atlas_x = cx;
			glyphs[i].atlas_y = cy;
			cx += glyphs[i].sdf_w;
			if (glyphs[i].sdf_h > shelf_h)
				shelf_h = glyphs[i].sdf_h;
		}

		if (ok) break;

		if (h < w)
			h <<= 1;
		else
			w <<= 1;

		if (w > MAX_ATLAS_DIM && h > MAX_ATLAS_DIM) {
			fprintf(stderr, "Error: atlas exceeds %dx%d\n",
			        MAX_ATLAS_DIM, MAX_ATLAS_DIM);
			return -1;
		}
	}

	*out_w = w;
	*out_h = h;
	return 0;
}

/* ------------------------------------------------------------------------ */
/* Usage                                                                     */
/* ------------------------------------------------------------------------ */

static void
usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s input.ttf [-o output.msdffont] [--range LO-HI]\n"
		"                     [--size N] [--pxrange N] [-v]\n"
		"\n"
		"Options:\n"
		"  -o PATH        Output file (default: input with .msdffont extension)\n"
		"  --range LO-HI  Codepoint range (default: %d-%d)\n"
		"  --size N        SDF rasterize size in pixels (default: %d)\n"
		"  --pxrange N     SDF pixel range / padding (default: %d)\n"
		"  -v              Verbose output\n",
		prog, DEFAULT_RANGE_LO, DEFAULT_RANGE_HI, DEFAULT_SIZE,
		DEFAULT_PXRANGE);
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
	int sdf_size = DEFAULT_SIZE;
	int pxrange = DEFAULT_PXRANGE;
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
		} else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
			sdf_size = atoi(argv[++i]);
			if (sdf_size < 8 || sdf_size > 512) {
				fprintf(stderr, "Error: size must be 8-512\n");
				return 1;
			}
		} else if (strcmp(argv[i], "--pxrange") == 0 && i + 1 < argc) {
			pxrange = atoi(argv[++i]);
			if (pxrange < 1 || pxrange > 32) {
				fprintf(stderr, "Error: pxrange must be 1-32\n");
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
		if (base_len + 11 >= sizeof(output_buf)) {
			fprintf(stderr, "Error: path too long\n");
			return 1;
		}
		memcpy(output_buf, input_path, base_len);
		memcpy(output_buf + base_len, ".msdffont", 10);
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
	/* head.unitsPerEm is big-endian */
	units_per_em_raw = ((units_per_em_raw & 0xFF) << 8) |
	                   ((units_per_em_raw >> 8) & 0xFF);
	float units_per_em = (float)units_per_em_raw;
	float em_scale = 1.0f / units_per_em;

	int raw_ascent, raw_descent, raw_line_gap;
	stbtt_GetFontVMetrics(&font, &raw_ascent, &raw_descent, &raw_line_gap);
	float ascender  = (float)raw_ascent  * em_scale;
	float descender = (float)raw_descent * em_scale;
	float line_gap  = (float)raw_line_gap * em_scale;

	float cap_height = 0.0f;
	{
		int gi = stbtt_FindGlyphIndex(&font, 'H');
		if (gi > 0) {
			int x0, y0, x1, y1;
			if (stbtt_GetGlyphBox(&font, gi, &x0, &y0, &x1, &y1))
				cap_height = (float)y1 * em_scale;
		}
	}

	float scale = stbtt_ScaleForPixelHeight(&font, (float)sdf_size);

	if (verbose) {
		fprintf(stderr, "Font: %s\n", input_path);
		fprintf(stderr, "  units/em: %d\n", units_per_em_raw);
		fprintf(stderr, "  ascender: %.4f  descender: %.4f  line_gap: %.4f\n",
		        ascender, descender, line_gap);
		fprintf(stderr, "  cap_height: %.4f\n", cap_height);
		fprintf(stderr, "  range: U+%04X - U+%04X\n", range_lo, range_hi);
		fprintf(stderr, "  sdf size: %d px  pxrange: %d\n", sdf_size, pxrange);
	}

	/* Enumerate glyphs and generate SDF bitmaps */
	glyph_data_t *glyphs = calloc(MAX_GLYPHS, sizeof(glyph_data_t));
	int glyph_count = 0;

	unsigned char onedge_value = 128;
	float pixel_dist_scale = (float)onedge_value / (float)pxrange;

	for (int cp = range_lo; cp <= range_hi && glyph_count < MAX_GLYPHS; cp++) {
		int gi = stbtt_FindGlyphIndex(&font, cp);
		if (gi <= 0) continue;

		glyph_data_t *g = &glyphs[glyph_count];
		g->codepoint = (uint32_t)cp;
		g->glyph_index = gi;

		/* Horizontal metrics (em-space) */
		int adv, lsb_raw;
		stbtt_GetGlyphHMetrics(&font, gi, &adv, &lsb_raw);
		g->advance = (float)adv * em_scale;
		g->lsb = (float)lsb_raw * em_scale;

		/* Bounding box (em-space) */
		int x0, y0, x1, y1;
		if (stbtt_GetGlyphBox(&font, gi, &x0, &y0, &x1, &y1)) {
			g->bbox_x0 = (float)x0 * em_scale;
			g->bbox_y0 = (float)y0 * em_scale;
			g->bbox_x1 = (float)x1 * em_scale;
			g->bbox_y1 = (float)y1 * em_scale;
		}

		/* Generate SDF bitmap */
		int sw = 0, sh = 0, sxoff = 0, syoff = 0;
		unsigned char *sdf = stbtt_GetGlyphSDF(&font, scale, gi,
		                                        pxrange, onedge_value,
		                                        pixel_dist_scale,
		                                        &sw, &sh, &sxoff, &syoff);
		g->sdf_pixels = sdf;
		g->sdf_w = sw;
		g->sdf_h = sh;
		g->sdf_xoff = sxoff;
		g->sdf_yoff = syoff;

		glyph_count++;
	}

	if (verbose)
		fprintf(stderr, "  glyphs: %d\n", glyph_count);

	/* Pack atlas */
	int atlas_w = 0, atlas_h = 0;
	if (pack_atlas(glyphs, glyph_count, &atlas_w, &atlas_h) < 0) {
		free(glyphs);
		free(font_data);
		return 1;
	}

	/* Re-sort by codepoint (pack_atlas sorted by height) */
	qsort(glyphs, (size_t)glyph_count, sizeof(glyph_data_t), cmp_glyph_cp);

	if (verbose)
		fprintf(stderr, "  atlas: %d x %d\n", atlas_w, atlas_h);

	/* Blit SDF bitmaps into atlas */
	size_t atlas_size = (size_t)atlas_w * (size_t)atlas_h;
	unsigned char *atlas = calloc(atlas_size, 1);

	for (int i = 0; i < glyph_count; i++) {
		glyph_data_t *g = &glyphs[i];
		if (!g->sdf_pixels || g->sdf_w == 0 || g->sdf_h == 0)
			continue;

		for (int row = 0; row < g->sdf_h; row++) {
			memcpy(atlas + (size_t)(g->atlas_y + row) * (size_t)atlas_w + (size_t)g->atlas_x,
			       g->sdf_pixels + (size_t)row * (size_t)g->sdf_w,
			       (size_t)g->sdf_w);
		}
	}

	/* Extract kerning pairs (same pattern as font2slug) */
	kern_entry_t *kerns = NULL;
	int kern_count = 0;
	{
		int kt_len = stbtt_GetKerningTableLength(&font);
		if (kt_len > 0) {
			stbtt_kerningentry *raw = calloc((size_t)kt_len,
			                                  sizeof(stbtt_kerningentry));
			kt_len = stbtt_GetKerningTable(&font, raw, kt_len);
			kerns = calloc((size_t)kt_len, sizeof(kern_entry_t));

			for (int k = 0; k < kt_len; k++) {
				int found_left = -1, found_right = -1;
				for (int gi = 0; gi < glyph_count; gi++) {
					if (glyphs[gi].glyph_index == raw[k].glyph1)
						found_left = (int)glyphs[gi].codepoint;
					if (glyphs[gi].glyph_index == raw[k].glyph2)
						found_right = (int)glyphs[gi].codepoint;
					if (found_left >= 0 && found_right >= 0) break;
				}
				if (found_left >= 0 && found_right >= 0 &&
				    raw[k].advance != 0) {
					kerns[kern_count].left_cp = (uint32_t)found_left;
					kerns[kern_count].right_cp = (uint32_t)found_right;
					kerns[kern_count].adjust =
						(float)raw[k].advance * em_scale;
					kern_count++;
				}
			}
			free(raw);
			qsort(kerns, (size_t)kern_count, sizeof(kern_entry_t),
			      cmp_kern);
		}
	}

	if (verbose)
		fprintf(stderr, "  kern pairs: %d\n", kern_count);

	/* Write .msdffont file */
	FILE *fout = fopen(output_path, "wb");
	if (!fout) {
		fprintf(stderr, "Error: cannot create '%s'\n", output_path);
		free(atlas);
		free(glyphs);
		free(kerns);
		free(font_data);
		return 1;
	}

	/* Calculate offsets */
	uint32_t glyph_off = (uint32_t)sizeof(msdf_file_header_t);
	uint32_t kern_off  = glyph_off +
		(uint32_t)(glyph_count * (int)sizeof(msdf_glyph_t));
	uint32_t atlas_off = kern_off +
		(uint32_t)(kern_count * (int)sizeof(slug_kern_pair_t));

	/* Write header */
	msdf_file_header_t hdr;
	memset(&hdr, 0, sizeof(hdr));
	memcpy(hdr.magic, MSDF_MAGIC, 4);
	hdr.version = MSDF_VERSION;
	hdr.flags = 0; /* single-channel SDF */
	hdr.glyph_count = (uint16_t)glyph_count;
	hdr.kern_count = (uint16_t)kern_count;
	hdr.units_per_em = units_per_em;
	hdr.ascender = ascender;
	hdr.descender = descender;
	hdr.line_gap = line_gap;
	hdr.cap_height = cap_height;
	hdr.atlas_w = (uint16_t)atlas_w;
	hdr.atlas_h = (uint16_t)atlas_h;
	hdr.px_range = (float)pxrange;
	hdr.atlas_format = MSDF_ATLAS_RAW;
	hdr.glyph_table_off = glyph_off;
	hdr.kern_table_off = kern_count > 0 ? kern_off : 0;
	hdr.atlas_data_off = atlas_off;
	hdr.atlas_data_size = (uint32_t)atlas_size;
	fwrite(&hdr, sizeof(hdr), 1, fout);

	/* Write glyph table */
	for (int i = 0; i < glyph_count; i++) {
		glyph_data_t *g = &glyphs[i];
		msdf_glyph_t mg;
		memset(&mg, 0, sizeof(mg));
		mg.codepoint = g->codepoint;
		mg.advance = g->advance;
		mg.lsb = g->lsb;
		mg.bbox_x0 = g->bbox_x0;
		mg.bbox_y0 = g->bbox_y0;
		mg.bbox_x1 = g->bbox_x1;
		mg.bbox_y1 = g->bbox_y1;
		mg.atlas_x = (uint16_t)g->atlas_x;
		mg.atlas_y = (uint16_t)g->atlas_y;
		mg.atlas_w = (uint16_t)g->sdf_w;
		mg.atlas_h = (uint16_t)g->sdf_h;
		fwrite(&mg, sizeof(mg), 1, fout);
	}

	/* Write kerning table */
	for (int i = 0; i < kern_count; i++) {
		slug_kern_pair_t kp;
		kp.left = kerns[i].left_cp;
		kp.right = kerns[i].right_cp;
		kp.adjust = kerns[i].adjust;
		fwrite(&kp, sizeof(kp), 1, fout);
	}

	/* Write atlas data (single-channel R8) */
	fwrite(atlas, 1, atlas_size, fout);

	fclose(fout);

	if (verbose) {
		long out_size = (long)atlas_off + (long)atlas_size;
		fprintf(stderr, "  output: %s (%ld bytes)\n", output_path, out_size);
	} else {
		fprintf(stderr, "%s: %d glyphs, %d kerns, %dx%d atlas -> %s\n",
		        input_path, glyph_count, kern_count,
		        atlas_w, atlas_h, output_path);
	}

	/* Cleanup */
	for (int i = 0; i < glyph_count; i++)
		stbtt_FreeSDF(glyphs[i].sdf_pixels, NULL);
	free(atlas);
	free(glyphs);
	free(kerns);
	free(font_data);

	return 0;
}
