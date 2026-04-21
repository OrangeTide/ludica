/*
 * msdf_format.h — Binary format for .msdffont files.
 *
 * Layout:
 *   msdf_file_header
 *   msdf_glyph[glyph_count]       (sorted by codepoint)
 *   slug_kern_pair_t[kern_count]   (sorted by left<<16|right, same as slug)
 *   atlas texture data             [atlas_w * atlas_h * channels bytes]
 *
 * All values are little-endian.
 *
 * Channel count depends on flags:
 *   MSDF_FLAG_MULTICHANNEL set:   3 channels (RGB8)
 *   MSDF_FLAG_MULTICHANNEL clear: 1 channel (R8) — deprecated
 *
 * Atlas format field controls compression:
 *   MSDF_ATLAS_RAW (0): uncompressed pixel data
 *   Future values reserved for PNG embedding, etc.
 */

#ifndef MSDF_FORMAT_H_
#define MSDF_FORMAT_H_

#include <stdint.h>

#define MSDF_MAGIC   "MSDF"
#define MSDF_VERSION 1

#define MSDF_FLAG_MULTICHANNEL 0x0001  /* RGB8 atlas (MSDF); clear = R8 (SDF) */

#define MSDF_ATLAS_RAW 0

#pragma pack(push, 1)

typedef struct {
	char     magic[4];          /* "MSDF" */
	uint16_t version;           /* MSDF_VERSION */
	uint16_t flags;             /* MSDF_FLAG_* */
	uint16_t glyph_count;
	uint16_t kern_count;
	float    units_per_em;
	float    ascender;          /* em-space, positive up */
	float    descender;         /* em-space, negative down */
	float    line_gap;          /* em-space */
	float    cap_height;        /* em-space, 0 if unknown */
	uint16_t atlas_w;
	uint16_t atlas_h;
	float    px_range;          /* SDF pixel range (e.g. 4.0) */
	uint8_t  atlas_format;      /* MSDF_ATLAS_* */
	uint8_t  reserved[3];
	uint32_t glyph_table_off;  /* byte offset from file start */
	uint32_t kern_table_off;   /* byte offset, 0 = no kerning */
	uint32_t atlas_data_off;   /* byte offset to atlas pixels */
	uint32_t atlas_data_size;  /* byte count of atlas data */
} msdf_file_header_t;

typedef struct {
	uint32_t codepoint;
	float    advance;           /* em-space horizontal advance */
	float    lsb;               /* em-space left side bearing */
	float    bbox_x0, bbox_y0;  /* em-space bounding box min */
	float    bbox_x1, bbox_y1;  /* em-space bounding box max */
	uint16_t atlas_x, atlas_y;  /* texel position in atlas */
	uint16_t atlas_w, atlas_h;  /* texel size in atlas */
} msdf_glyph_t;

#pragma pack(pop)

#endif /* MSDF_FORMAT_H_ */
