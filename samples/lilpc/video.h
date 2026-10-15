/* video.h - CGA, Hercules, and ATI Graphics Solution display emulation */
#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>
#include <stdbool.h>

struct lilpc;

/* adapter types */
typedef enum {
	VIDEO_CGA,
	VIDEO_HERCULES,
	VIDEO_ATI		/* CGA + MDA + Hercules + Plantronics + ATI */
} video_adapter_t;

/* CGA display dimensions */
#define CGA_TEXT_COLS	80
#define CGA_TEXT_ROWS	25
#define CGA_GFX_W	640
#define CGA_GFX_H	200
#define CGA_VRAM_BASE	0xB8000
#define CGA_VRAM_SIZE	0x4000	/* 16KB */

/*
 * CGA-256: hypothetical packed-pixel extension.
 * 160x100, one byte per pixel, 256 colors through a RAMDAC CLUT.
 * 160*100 = 16000 bytes, so the whole frame fits in CGA's 16KB VRAM
 * with 384 bytes to spare.  Enabled by mode-control bit 6 (0x40).
 */
#define CGA256_W	160
#define CGA256_H	100

/* Hercules display dimensions */
#define HERC_TEXT_COLS	80
#define HERC_TEXT_ROWS	25
#define HERC_GFX_W	720
#define HERC_GFX_H	348
#define HERC_VRAM_BASE	0xB0000
#define HERC_VRAM_SIZE	0x10000	/* 64KB (2 pages) */

/* Plantronics / ATI VRAM sizes */
#define PLANTRONICS_VRAM_SIZE	0x8000	/* 32KB: B8000-BFFFF */
#define ATI_VRAM_BASE		0xB0000
#define ATI_VRAM_SIZE		0x10000	/* 64KB: B0000-BFFFF */

/* render buffer: max resolution is 1056x350 (132-col text) */
#define VIDEO_MAX_W	1056
#define VIDEO_MAX_H	350

/* MC6845 CRTC registers (shared between CGA and Hercules) */
typedef struct {
	uint8_t reg[18];	/* R0-R17 */
	uint8_t index;		/* currently selected register */
} crtc_t;

typedef struct video {
	/* which adapter is active */
	video_adapter_t adapter;

	/* MC6845 CRTC */
	crtc_t crtc;

	/* CGA-specific registers */
	uint8_t mode_ctrl;	/* port 3D8h */
	uint8_t color_sel;	/* port 3D9h */
	uint8_t status;		/* port 3DAh (read) */

	/* Hercules-specific registers */
	uint8_t herc_mode;	/* port 3B8h */
	uint8_t herc_config;	/* port 3BFh */

	/* ATI / Plantronics registers */
	uint8_t plantronics;	/* port 3DDh: bits 4-6 Plantronics, bit 7 ATI */
	uint8_t ati_ext;	/* port 3DFh / 3BAh(w): ATI extended mode */
	bool ati_mono_active;	/* true when in MDA/Hercules mode (Level 3) */

	/* retrace timing */
	int scanline;		/* current scanline for status register */
	uint64_t last_tick;

	/* CGA snow emulation: bitmap of character cells corrupted by CPU
	 * writes to VRAM during active display.  Each byte is non-zero if
	 * the cell was touched.  Cleared at start of each render. */
	uint8_t snow[80 * 25];
	uint64_t *cpu_cycles_ptr;	/* points to cpu.cycles for timing */
	uint16_t snow_seed;		/* PRNG state for snow corruption */

	/* pixel render buffer (palette indices) */
	uint8_t pixels[VIDEO_MAX_W * VIDEO_MAX_H];
	int render_w, render_h;

	/* border color (CGA only): palette index from color_sel bits 3:0 */
	uint8_t border_color;

	/* composite CGA mode (GPU shader NTSC decode) */
	int composite;

	/*
	 * CGA-256 extension state.
	 *
	 * packed256 is set by video_render() when the 160x100x256 packed
	 * mode is active, so the display side knows to use the full RAMDAC
	 * palette (and to disable composite NTSC decode, which models an
	 * RGBI signal that this mode does not produce).
	 *
	 * The RAMDAC CLUT is programmed VGA-style through ports 3C6h-3C9h.
	 * dac[] holds 6-bit R,G,B per entry (0-63), as written by the CPU;
	 * video_get_palette() expands them to 8-bit RGBA on readout.
	 */
	int packed256;
	uint8_t dac[256][3];	/* RAMDAC CLUT: 6-bit R,G,B per index */
	uint8_t dac_mask;	/* PEL mask register (port 3C6h) */
	uint8_t dac_wr_idx;	/* PEL write index (port 3C8h) */
	uint8_t dac_rd_idx;	/* PEL read index (port 3C7h) */
	uint8_t dac_sub;	/* sub-index within an entry: 0=R, 1=G, 2=B */
} video_t;

void video_init(video_t *vid, struct lilpc *pc, video_adapter_t adapter);
void video_render(video_t *vid, struct lilpc *pc);
void video_tick(video_t *vid, struct lilpc *pc, uint64_t cpu_cycles);

/* get current display dimensions */
void video_get_size(video_t *vid, int *w, int *h);

/*
 * Fill out[256] with the active display palette as RGBA
 * (0xAABBGGRR, little-endian).  Standard CGA fills 0-15, Hercules
 * fills 0-1, and CGA-256 packed mode fills all 256 from the RAMDAC.
 * Entries beyond the active count are opaque black.
 */
void video_get_palette(video_t *vid, uint32_t out[256]);

/* CGA RGBI palette (RGBA, little-endian = 0xAABBGGRR) */
extern const uint32_t cga_palette[16];

/* Hercules monochrome palette */
extern const uint32_t herc_palette[2];

#endif
