/* cgapack.c - convert PNG to CGA VRAM dump for mode 4 or mode 6
 * Made by a machine. PUBLIC DOMAIN (CC0-1.0) */

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CGA_W4    320
#define CGA_W6    640
#define CGA_H     200
#define VRAM_SIZE 16384
#define VRAM_HALF 8192

/* Mode 4 palette 1 high intensity: black, bright cyan, bright magenta, white */
static const int pal4[4][3] = {
    {  0,   0,   0},
    { 85, 255, 255},
    {255,  85, 255},
    {255, 255, 255},
};

/* NTSC composite decode of mode 6 monochrome bits, matching the display
 * shader (lilpc.c composite_decode): on-pixel = white (signal 1), off = black.
 * Luma is averaged over a 4-tap window so it stays sharp; I/Q over the full
 * 8-tap window so chroma is recovered. Phase advances PI/2 per hires pixel,
 * so cos/sin only ever take the values {1,0,-1,0} / {0,1,0,-1} by (pixel & 3).
 * Result is linear RGB in 0..1 (may fall slightly outside; caller clamps).
 *
 * Phase here is integer pixel phase. The display shader samples at texel
 * centres, so its decoded hues sit ~45 degrees rotated from this; that tint
 * is left as-is (shader u_hue == 0). Align both phases before touching it. */
static void decode_pixel(const uint8_t *bits, int n, int p,
                         double *r, double *g, double *b)
{
    static const double CO[4] = { 1.0, 0.0, -1.0, 0.0 };
    static const double SI[4] = { 0.0, 1.0, 0.0, -1.0 };
    double y = 0.0, i = 0.0, q = 0.0;

    for (int t = -3; t <= 4; t++) {
        int idx = p + t;
        int s = (idx >= 0 && idx < n) ? bits[idx] : 0;
        int m = idx & 3;
        if (t >= -1 && t <= 2)
            y += s;
        i += s * CO[m];
        q += s * SI[m];
    }
    y /= 4.0;
    i /= 4.0;
    q /= 4.0;

    *r = y + 0.956 * i + 0.621 * q;
    *g = y - 0.272 * i - 0.647 * q;
    *b = y - 1.106 * i + 1.703 * q;
}

static int nearest_pal4(int r, int g, int b)
{
    int best = 0;
    int best_dist = INT_MAX;
    for (int i = 0; i < 4; i++) {
        int dr = r - pal4[i][0];
        int dg = g - pal4[i][1];
        int db = b - pal4[i][2];
        int d = dr * dr + dg * dg + db * db;
        if (d < best_dist) {
            best_dist = d;
            best = i;
        }
    }
    return best;
}

static void vram_put(uint8_t *vram, int x, int y, int bpp, int val)
{
    int row_bytes = (bpp == 2) ? 80 : 80; /* 80 bytes/row for both modes */
    int base = (y & 1) ? VRAM_HALF : 0;
    int row = y / 2;
    int offset = base + row * row_bytes;

    if (bpp == 2) {
        /* mode 4: 4 pixels per byte, high bits first */
        int byte_idx = x / 4;
        int bit_pos = 6 - (x % 4) * 2;
        vram[offset + byte_idx] |= (val & 3) << bit_pos;
    } else {
        /* mode 6: 8 pixels per byte, high bit first */
        int byte_idx = x / 8;
        int bit_pos = 7 - (x % 8);
        if (val)
            vram[offset + byte_idx] |= 1 << bit_pos;
    }
}

static void convert_mode4(const uint8_t *img, int w, int h, uint8_t *vram)
{
    /* Floyd-Steinberg dithering to 4-color CGA palette */
    float *err = calloc((size_t)w * h * 3, sizeof(float));

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int idx = (y * w + x) * 3;
            float r = img[idx + 0] + err[idx + 0];
            float g = img[idx + 1] + err[idx + 1];
            float b = img[idx + 2] + err[idx + 2];
            int cr = (int)(fminf(fmaxf(r, 0), 255) + 0.5f);
            int cg = (int)(fminf(fmaxf(g, 0), 255) + 0.5f);
            int cb = (int)(fminf(fmaxf(b, 0), 255) + 0.5f);

            int ci = nearest_pal4(cr, cg, cb);
            vram_put(vram, x, y, 2, ci);

            float er = r - pal4[ci][0];
            float eg = g - pal4[ci][1];
            float eb = b - pal4[ci][2];

            /* distribute error: right 7/16, below-left 3/16, below 5/16, below-right 1/16 */
            if (x + 1 < w) {
                err[idx + 3] += er * 7.0f / 16.0f;
                err[idx + 4] += eg * 7.0f / 16.0f;
                err[idx + 5] += eb * 7.0f / 16.0f;
            }
            if (y + 1 < h) {
                int below = ((y + 1) * w + x) * 3;
                if (x > 0) {
                    err[below - 3] += er * 3.0f / 16.0f;
                    err[below - 2] += eg * 3.0f / 16.0f;
                    err[below - 1] += eb * 3.0f / 16.0f;
                }
                err[below + 0] += er * 5.0f / 16.0f;
                err[below + 1] += eg * 5.0f / 16.0f;
                err[below + 2] += eb * 5.0f / 16.0f;
                if (x + 1 < w) {
                    err[below + 3] += er * 1.0f / 16.0f;
                    err[below + 4] += eg * 1.0f / 16.0f;
                    err[below + 5] += eb * 1.0f / 16.0f;
                }
            }
        }
    }
    free(err);
}

static void convert_mode6(const uint8_t *img, int w, int h, uint8_t *vram)
{
    /* Mode 6 is 640x200, 1 bit per pixel. On a composite monitor each run of
     * pixels at the colorburst frequency blends into one of 16 artifact
     * colors: ~160 color cells horizontally, but full 640-wide luma detail.
     *
     * Rather than stamp fixed 4-bit patterns (which ignores how neighbouring
     * bits bleed together in the decoder, and biases the picture toward white),
     * choose the 640 on/off bits per scanline so that the same NTSC decode the
     * display performs reproduces the target color. Each bit is picked to
     * minimise the decoded error over the pixels its 8-tap window touches, and
     * the residual is error-diffused so average brightness and hue track the
     * source instead of washing out. */
    const int W = 640;

    float *rowerr = calloc((size_t)W * 3, sizeof(float));   /* error from above */
    float *nexterr = calloc((size_t)W * 3, sizeof(float));  /* error to below */
    uint8_t *bits = calloc((size_t)W, 1);
    double *tr = malloc(sizeof(double) * W);
    double *tg = malloc(sizeof(double) * W);
    double *tb = malloc(sizeof(double) * W);

    for (int y = 0; y < h; y++) {
        /* target row in 0..1, source upsampled 320->640, plus diffused error */
        for (int p = 0; p < W; p++) {
            int sx = p * w / W;
            int idx = (y * w + sx) * 3;
            tr[p] = img[idx + 0] / 255.0 + rowerr[p * 3 + 0];
            tg[p] = img[idx + 1] / 255.0 + rowerr[p * 3 + 1];
            tb[p] = img[idx + 2] / 255.0 + rowerr[p * 3 + 2];
        }

        /* seed from a luma threshold so the symmetric decode sees plausible
         * neighbours, then refine each bit against the real decode */
        for (int p = 0; p < W; p++) {
            double luma = 0.299 * tr[p] + 0.587 * tg[p] + 0.114 * tb[p];
            bits[p] = luma > 0.5 ? 1 : 0;
        }
        for (int pass = 0; pass < 2; pass++) {
            for (int p = 0; p < W; p++) {
                double best = 1e30;
                int bestv = 0;
                for (int v = 0; v <= 1; v++) {
                    bits[p] = (uint8_t)v;
                    double e = 0.0;
                    /* pixels whose 8-tap window includes p: p-4 .. p+3 */
                    for (int k = p - 4; k <= p + 3; k++) {
                        if (k < 0 || k >= W)
                            continue;
                        double r, g, b;
                        decode_pixel(bits, W, k, &r, &g, &b);
                        double dr = r - tr[k], dg = g - tg[k], db = b - tb[k];
                        e += dr * dr + dg * dg + db * db;
                    }
                    if (e < best) {
                        best = e;
                        bestv = v;
                    }
                }
                bits[p] = (uint8_t)bestv;
            }
        }

        /* commit to interleaved VRAM and diffuse the residual downward */
        memset(nexterr, 0, (size_t)W * 3 * sizeof(float));
        for (int p = 0; p < W; p++) {
            vram_put(vram, p, y, 1, bits[p]);

            double r, g, b;
            decode_pixel(bits, W, p, &r, &g, &b);
            float er = (float)(tr[p] - r);
            float eg = (float)(tg[p] - g);
            float eb = (float)(tb[p] - b);

            if (p > 0) {
                nexterr[(p - 1) * 3 + 0] += er * 3.0f / 16.0f;
                nexterr[(p - 1) * 3 + 1] += eg * 3.0f / 16.0f;
                nexterr[(p - 1) * 3 + 2] += eb * 3.0f / 16.0f;
            }
            nexterr[p * 3 + 0] += er * 5.0f / 16.0f;
            nexterr[p * 3 + 1] += eg * 5.0f / 16.0f;
            nexterr[p * 3 + 2] += eb * 5.0f / 16.0f;
            if (p + 1 < W) {
                nexterr[(p + 1) * 3 + 0] += er * 1.0f / 16.0f;
                nexterr[(p + 1) * 3 + 1] += eg * 1.0f / 16.0f;
                nexterr[(p + 1) * 3 + 2] += eb * 1.0f / 16.0f;
            }
        }

        float *tmp = rowerr;
        rowerr = nexterr;
        nexterr = tmp;
    }

    free(rowerr);
    free(nexterr);
    free(bits);
    free(tr);
    free(tg);
    free(tb);
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: cgapack --mode4|--mode6 input.png output.raw\n");
        return 1;
    }

    int mode6 = 0;
    if (strcmp(argv[1], "--mode6") == 0)
        mode6 = 1;
    else if (strcmp(argv[1], "--mode4") != 0) {
        fprintf(stderr, "error: first argument must be --mode4 or --mode6\n");
        return 1;
    }

    int w, h, ch;
    uint8_t *img = stbi_load(argv[2], &w, &h, &ch, 3);
    if (!img) {
        fprintf(stderr, "error: cannot load %s: %s\n", argv[2], stbi_failure_reason());
        return 1;
    }
    if (w != 320 || h != 200) {
        fprintf(stderr, "error: image must be 320x200, got %dx%d\n", w, h);
        stbi_image_free(img);
        return 1;
    }

    uint8_t vram[VRAM_SIZE];
    memset(vram, 0, sizeof(vram));

    if (mode6) {
        convert_mode6(img, w, h, vram);
    } else {
        convert_mode4(img, w, h, vram);
    }

    stbi_image_free(img);

    FILE *f = fopen(argv[3], "wb");
    if (!f) {
        perror(argv[3]);
        return 1;
    }
    fwrite(vram, 1, VRAM_SIZE, f);
    fclose(f);

    return 0;
}
