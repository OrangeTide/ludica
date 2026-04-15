/*
 * slug_format.h — Binary format for .slugfont files.
 *
 * Layout:
 *   slug_file_header
 *   slug_glyph[glyph_count]       (sorted by codepoint)
 *   slug_kern_pair[kern_count]     (sorted by left<<16|right)
 *   curve texture data (RGBA16F)   [curve_tex_w * curve_tex_h * 8 bytes]
 *   band texture data (RG16UI)     [band_tex_w * band_tex_h * 4 bytes]
 *
 * All values are little-endian.
 *
 * Curve texture (RGBA16F): stores quadratic Bezier control points.
 * Two consecutive texels per curve segment:
 *   texel 0: (p1.x, p1.y, p2.x, p2.y)   — start point and control point
 *   texel 1: (p3.x, p3.y, 0, 0)          — end point
 * Coordinates are in em-space (1.0 = 1 em).
 *
 * Band texture (RG16UI): maps pixel bands to curve lists.
 * For each glyph, horizontal bands are stored first, then vertical bands.
 * Each texel = (curve_start, curve_count) as uint16 offsets into the
 * curve texture. Curves within each band are sorted by descending max
 * coordinate for early termination.
 */

#ifndef SLUG_FORMAT_H_
#define SLUG_FORMAT_H_

#include <stdint.h>

#define SLUG_MAGIC          "SLUG"
#define SLUG_VERSION        1

#pragma pack(push, 1)

typedef struct {
	char     magic[4];          /* "SLUG" */
	uint16_t version;           /* SLUG_VERSION */
	uint16_t flags;             /* reserved, must be 0 */
	uint16_t glyph_count;
	uint16_t kern_count;
	float    units_per_em;
	float    ascender;          /* em-space, positive up */
	float    descender;         /* em-space, negative down */
	float    line_gap;          /* em-space */
	float    cap_height;        /* em-space, 0 if unknown */
	uint16_t curve_tex_w;
	uint16_t curve_tex_h;
	uint16_t band_tex_w;
	uint16_t band_tex_h;
	uint32_t glyph_table_off;  /* byte offset from file start */
	uint32_t kern_table_off;   /* byte offset, 0 = no kerning */
	uint32_t curve_data_off;   /* byte offset to RGBA16F data */
	uint32_t band_data_off;    /* byte offset to RG16UI data */
} slug_file_header_t;

typedef struct {
	uint32_t codepoint;
	float    advance;           /* em-space horizontal advance */
	float    lsb;               /* em-space left side bearing */
	float    bbox_x0, bbox_y0;  /* em-space bounding box min */
	float    bbox_x1, bbox_y1;  /* em-space bounding box max */
	uint16_t curve_x;           /* texel column in curve texture */
	uint16_t curve_y;           /* texel row in curve texture */
	uint16_t band_x;            /* texel column in band texture */
	uint16_t band_y;            /* texel row in band texture */
	uint8_t  hband_count;       /* number of horizontal bands */
	uint8_t  vband_count;       /* number of vertical bands */
	uint16_t curve_count;       /* total curves for this glyph */
} slug_glyph_t;

typedef struct {
	uint32_t left;              /* left codepoint */
	uint32_t right;             /* right codepoint */
	float    adjust;            /* em-space kerning adjustment */
} slug_kern_pair_t;

#pragma pack(pop)

#endif /* SLUG_FORMAT_H_ */
