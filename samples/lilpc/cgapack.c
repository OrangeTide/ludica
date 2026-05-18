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

/* NTSC YIQ matrix for composite artifact color computation */
static void rgb_to_yiq(double r, double g, double b, double *y, double *i, double *q)
{
    *y = 0.299 * r + 0.587 * g + 0.114 * b;
    *i = 0.5957 * r - 0.2745 * g - 0.3213 * b;
    *q = 0.2115 * r - 0.5226 * g + 0.3112 * b;
}

static void yiq_to_rgb(double y, double i, double q, int *r, int *g, int *b)
{
    double rr = y + 0.9563 * i + 0.6210 * q;
    double gg = y - 0.2721 * i - 0.6474 * q;
    double bb = y - 1.1070 * i + 1.7046 * q;
    *r = (int)(fmin(fmax(rr, 0.0), 255.0) + 0.5);
    *g = (int)(fmin(fmax(gg, 0.0), 255.0) + 0.5);
    *b = (int)(fmin(fmax(bb, 0.0), 255.0) + 0.5);
}

/* CGA RGBI palette for generating composite signal */
static const int cga_rgbi[16][3] = {
    {  0,   0,   0}, {  0,   0, 170}, {  0, 170,   0}, {  0, 170, 170},
    {170,   0,   0}, {170,   0, 170}, {170,  85,   0}, {170, 170, 170},
    { 85,  85,  85}, { 85,  85, 255}, { 85, 255,  85}, { 85, 255, 255},
    {255,  85,  85}, {255,  85, 255}, {255, 255,  85}, {255, 255, 255},
};

static int artifact_colors[16][3];

static void compute_artifact_colors(void)
{
    /* In mode 6 (640x200 1bpp), the CGA generates a monochrome signal
     * using RGBI color 15 (white) for on-pixels and 0 (black) for off.
     * Each group of 4 pixels at the colorburst frequency produces a
     * composite color. The 4-bit pattern indexes into 16 artifact colors. */
    double pi = 3.14159265358979323846;

    for (int pat = 0; pat < 16; pat++) {
        double y_acc = 0, i_acc = 0, q_acc = 0;
        /* simulate 4 pixels of the pattern, repeated over an 8-pixel window */
        for (int tap = -3; tap <= 4; tap++) {
            int bit = (pat >> (3 - (tap & 3))) & 1;
            double luma = bit ? 255.0 : 0.0;
            double phase = tap * pi * 0.5;
            double signal = luma;
            /* Y: narrow filter (1 NTSC cycle = 4 pixels at hires) */
            if (tap >= -1 && tap <= 2)
                y_acc += signal;
            /* I/Q: full 8-tap window */
            i_acc += signal * cos(phase);
            q_acc += signal * sin(phase);
        }
        y_acc /= 4.0;
        i_acc /= 4.0;
        q_acc /= 4.0;
        yiq_to_rgb(y_acc, i_acc, q_acc,
                    &artifact_colors[pat][0],
                    &artifact_colors[pat][1],
                    &artifact_colors[pat][2]);
    }
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

static int nearest_artifact(int r, int g, int b)
{
    int best = 0;
    int best_dist = INT_MAX;
    for (int i = 0; i < 16; i++) {
        int dr = r - artifact_colors[i][0];
        int dg = g - artifact_colors[i][1];
        int db = b - artifact_colors[i][2];
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
    /* Floyd-Steinberg dithering to 16 artifact colors.
     * Source is 320 wide; each source pixel becomes a 4-bit pattern
     * in the 640-wide framebuffer. */
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

            int ci = nearest_artifact(cr, cg, cb);

            /* write 4 bits of the pattern into the 640-wide framebuffer */
            for (int bit = 0; bit < 4; bit++) {
                int pixel_val = (ci >> (3 - bit)) & 1;
                vram_put(vram, x * 4 + bit, y, 1, pixel_val);
            }

            float er = r - artifact_colors[ci][0];
            float eg = g - artifact_colors[ci][1];
            float eb = b - artifact_colors[ci][2];

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
        compute_artifact_colors();
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
