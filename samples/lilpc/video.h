/* video.h - CGA and Hercules display emulation for lilpc */
#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>
#include <stdbool.h>

struct lilpc;

/* CGA display dimensions */
#define CGA_TEXT_COLS	80
#define CGA_TEXT_ROWS	25
#define CGA_GFX_W	640
#define CGA_GFX_H	200
#define CGA_VRAM_BASE	0xB8000
#define CGA_VRAM_SIZE	0x4000	/* 16KB */

/* Hercules display dimensions */
#define HERC_TEXT_COLS	80
#define HERC_TEXT_ROWS	25
#define HERC_GFX_W	720
#define HERC_GFX_H	348
#define HERC_VRAM_BASE	0xB0000
#define HERC_VRAM_SIZE	0x10000	/* 64KB (2 pages) */

/* render buffer: max resolution is 720x350 (Hercules) */
#define VIDEO_MAX_W	720
#define VIDEO_MAX_H	350

/* MC6845 CRTC registers (shared between CGA and Hercules) */
typedef struct {
	uint8_t reg[18];	/* R0-R17 */
	uint8_t index;		/* currently selected register */
} crtc_t;

typedef struct video {
	/* which adapter is active */
	bool hercules;		/* true = Hercules, false = CGA */

	/* MC6845 CRTC */
	crtc_t crtc;

	/* CGA-specific registers */
	uint8_t mode_ctrl;	/* port 3D8h */
	uint8_t color_sel;	/* port 3D9h */
	uint8_t status;		/* port 3DAh (read) */

	/* Hercules-specific registers */
	uint8_t herc_mode;	/* port 3B8h */
	uint8_t herc_config;	/* port 3BFh */

	/* retrace timing */
	int scanline;		/* current scanline for status register */
	uint64_t last_tick;

	/* pixel render buffer (palette indices for CGA, RGBA for Hercules) */
	uint8_t pixels[VIDEO_MAX_W * VIDEO_MAX_H];
	int render_w, render_h;

	/* border color (CGA only): palette index from color_sel bits 3:0 */
	uint8_t border_color;
} video_t;

void video_init(video_t *vid, struct lilpc *pc, bool hercules);
void video_render(video_t *vid, struct lilpc *pc);
void video_tick(video_t *vid, struct lilpc *pc, uint64_t cpu_cycles);

/* get current display dimensions */
void video_get_size(video_t *vid, int *w, int *h);

/* CGA RGBI palette (RGBA, little-endian = 0xAABBGGRR) */
extern const uint32_t cga_palette[16];

/* Hercules monochrome palette */
extern const uint32_t herc_palette[2];

#endif
