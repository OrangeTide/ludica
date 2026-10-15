/* cga256pack.c - convert a PNG to a CGA-256 packed-mode VRAM dump.
 *
 * Output layout (one blob the viewer256.asm stub loads directly):
 *   bytes 0..767     RAMDAC palette, 256 entries of R,G,B (6-bit each)
 *   bytes 768..16767 160x100 pixel indices, one byte per pixel, linear
 *
 * The image is area-resampled to 160x100, reduced to 256 colors with a
 * median-cut quantizer, and mapped with optional Floyd-Steinberg
 * dithering.  See CGA256.md for the hardware model this feeds.
 *
 *   cga256pack [--nodither] input.png output.raw
 */

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DST_W     160
#define DST_H     100
#define NPIX      (DST_W * DST_H)
#define NCOLORS   256
#define PAL_BYTES (NCOLORS * 3)

typedef struct { uint8_t r, g, b; } rgb_t;

/* --- area resample to 160x100 ------------------------------------------ */

static void resample(const uint8_t *src, int sw, int sh, rgb_t *dst)
{
	for (int dy = 0; dy < DST_H; dy++) {
		int sy0 = dy * sh / DST_H;
		int sy1 = (dy + 1) * sh / DST_H;
		if (sy1 <= sy0)
			sy1 = sy0 + 1;
		for (int dx = 0; dx < DST_W; dx++) {
			int sx0 = dx * sw / DST_W;
			int sx1 = (dx + 1) * sw / DST_W;
			if (sx1 <= sx0)
				sx1 = sx0 + 1;

			uint32_t r = 0, g = 0, b = 0, n = 0;
			for (int y = sy0; y < sy1; y++) {
				for (int x = sx0; x < sx1; x++) {
					const uint8_t *p = src + (y * sw + x) * 3;
					r += p[0];
					g += p[1];
					b += p[2];
					n++;
				}
			}
			dst[dy * DST_W + dx].r = (uint8_t)(r / n);
			dst[dy * DST_W + dx].g = (uint8_t)(g / n);
			dst[dy * DST_W + dx].b = (uint8_t)(b / n);
		}
	}
}

/* --- median-cut quantizer ---------------------------------------------- */

typedef struct { int start, len; } box_t;

static int g_channel;	/* channel qsort compares on */

static int cmp_channel(const void *a, const void *b)
{
	const uint8_t *pa = (const uint8_t *)a + g_channel;
	const uint8_t *pb = (const uint8_t *)b + g_channel;
	return (int)*pa - (int)*pb;
}

/* widest channel of a box, returns its range and sets *chan */
static int box_widest(const rgb_t *pix, box_t box, int *chan)
{
	uint8_t lo[3] = { 255, 255, 255 };
	uint8_t hi[3] = { 0, 0, 0 };
	for (int i = 0; i < box.len; i++) {
		const uint8_t *p = (const uint8_t *)&pix[box.start + i];
		for (int c = 0; c < 3; c++) {
			if (p[c] < lo[c]) lo[c] = p[c];
			if (p[c] > hi[c]) hi[c] = p[c];
		}
	}
	int best = 0, bestc = 0;
	for (int c = 0; c < 3; c++) {
		int range = hi[c] - lo[c];
		if (range > best) {
			best = range;
			bestc = c;
		}
	}
	*chan = bestc;
	return best;
}

/* build a 256-entry palette (8-bit) from the resized image */
static int quantize(rgb_t *pix, rgb_t *pal)
{
	box_t *boxes = calloc(NCOLORS, sizeof(*boxes));
	int nboxes = 1;
	boxes[0].start = 0;
	boxes[0].len = NPIX;

	while (nboxes < NCOLORS) {
		/* pick the box with the widest channel to split */
		int target = -1, target_range = 0, target_chan = 0;
		for (int i = 0; i < nboxes; i++) {
			if (boxes[i].len < 2)
				continue;
			int chan;
			int range = box_widest(pix, boxes[i], &chan);
			if (range > target_range) {
				target_range = range;
				target = i;
				target_chan = chan;
			}
		}
		if (target < 0)
			break;	/* every box is a single color */

		box_t b = boxes[target];
		g_channel = target_chan;
		qsort(&pix[b.start], b.len, sizeof(rgb_t), cmp_channel);

		int half = b.len / 2;
		boxes[target].len = half;
		boxes[nboxes].start = b.start + half;
		boxes[nboxes].len = b.len - half;
		nboxes++;
	}

	/* palette entry = average color of each box */
	for (int i = 0; i < nboxes; i++) {
		uint32_t r = 0, g = 0, b = 0;
		for (int k = 0; k < boxes[i].len; k++) {
			const rgb_t *p = &pix[boxes[i].start + k];
			r += p->r;
			g += p->g;
			b += p->b;
		}
		int n = boxes[i].len ? boxes[i].len : 1;
		pal[i].r = (uint8_t)(r / n);
		pal[i].g = (uint8_t)(g / n);
		pal[i].b = (uint8_t)(b / n);
	}
	for (int i = nboxes; i < NCOLORS; i++)
		pal[i] = (rgb_t){ 0, 0, 0 };

	free(boxes);
	return nboxes;
}

/* nearest palette entry (8-bit Euclidean) */
static int nearest(const rgb_t *pal, int npal, int r, int g, int b)
{
	int best = 0;
	long bestd = 1L << 30;
	for (int i = 0; i < npal; i++) {
		long dr = r - pal[i].r;
		long dg = g - pal[i].g;
		long db = b - pal[i].b;
		long d = dr * dr + dg * dg + db * db;
		if (d < bestd) {
			bestd = d;
			best = i;
		}
	}
	return best;
}

static int clampi(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

int main(int argc, char **argv)
{
	int dither = 1;
	const char *in = NULL, *out = NULL;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--nodither") == 0)
			dither = 0;
		else if (!in)
			in = argv[i];
		else
			out = argv[i];
	}
	if (!in || !out) {
		fprintf(stderr, "usage: %s [--nodither] input.png output.raw\n",
			argv[0]);
		return 1;
	}

	int sw, sh, comp;
	uint8_t *src = stbi_load(in, &sw, &sh, &comp, 3);
	if (!src) {
		fprintf(stderr, "cannot load %s: %s\n", in, stbi_failure_reason());
		return 1;
	}

	rgb_t *pix = malloc(NPIX * sizeof(rgb_t));
	resample(src, sw, sh, pix);
	stbi_image_free(src);

	/* quantize a copy (median-cut reorders its input) */
	rgb_t *work = malloc(NPIX * sizeof(rgb_t));
	memcpy(work, pix, NPIX * sizeof(rgb_t));
	rgb_t pal[NCOLORS];
	int npal = quantize(work, pal);
	free(work);

	/* map pixels to palette indices, with optional dithering */
	uint8_t *idx = malloc(NPIX);
	if (!dither) {
		for (int i = 0; i < NPIX; i++)
			idx[i] = (uint8_t)nearest(pal, npal,
				pix[i].r, pix[i].g, pix[i].b);
	} else {
		/* Floyd-Steinberg in RGB against the quantized palette */
		int *err = calloc(NPIX * 3, sizeof(int));
		for (int y = 0; y < DST_H; y++) {
			for (int x = 0; x < DST_W; x++) {
				int i = y * DST_W + x;
				int r = clampi(pix[i].r + err[i * 3 + 0]);
				int g = clampi(pix[i].g + err[i * 3 + 1]);
				int b = clampi(pix[i].b + err[i * 3 + 2]);
				int pi = nearest(pal, npal, r, g, b);
				idx[i] = (uint8_t)pi;
				int dr = r - pal[pi].r;
				int dg = g - pal[pi].g;
				int db = b - pal[pi].b;
				/* distribute 7/16, 3/16, 5/16, 1/16 */
				struct { int dx, dy, w; } k[4] = {
					{ 1, 0, 7 }, { -1, 1, 3 },
					{ 0, 1, 5 }, { 1, 1, 1 },
				};
				for (int j = 0; j < 4; j++) {
					int nx = x + k[j].dx, ny = y + k[j].dy;
					if (nx < 0 || nx >= DST_W || ny >= DST_H)
						continue;
					int ni = (ny * DST_W + nx) * 3;
					err[ni + 0] += dr * k[j].w / 16;
					err[ni + 1] += dg * k[j].w / 16;
					err[ni + 2] += db * k[j].w / 16;
				}
			}
		}
		free(err);
	}

	/* write palette (6-bit) then pixels */
	FILE *f = fopen(out, "wb");
	if (!f) {
		fprintf(stderr, "cannot write %s\n", out);
		return 1;
	}
	uint8_t palbytes[PAL_BYTES];
	for (int i = 0; i < NCOLORS; i++) {
		palbytes[i * 3 + 0] = pal[i].r >> 2;	/* 8-bit -> 6-bit DAC */
		palbytes[i * 3 + 1] = pal[i].g >> 2;
		palbytes[i * 3 + 2] = pal[i].b >> 2;
	}
	fwrite(palbytes, 1, PAL_BYTES, f);
	fwrite(idx, 1, NPIX, f);
	fclose(f);

	fprintf(stderr, "%s: %dx%d -> 160x100, %d colors%s\n",
		in, sw, sh, npal, dither ? ", dithered" : "");

	free(pix);
	free(idx);
	return 0;
}
