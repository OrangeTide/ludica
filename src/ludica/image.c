/*
 * image.c — Load textures from image files via stb_image.
 */

#include "ludica_internal.h"
#include "ludica_gfx.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static lud_texture_t
load_image(const char *path, enum lud_filter min_filter,
           enum lud_filter mag_filter, int srgb)
{
	lud_texture_t out = {0};
	int w, h, channels;
	unsigned char *data;
	enum lud_pixel_format fmt;

	data = stbi_load(path, &w, &h, &channels, 0);
	if (!data) {
		lud_err("failed to load image: %s", path);
		return out;
	}

	if (srgb) {
		switch (channels) {
		case 3: fmt = LUD_PIXFMT_SRGB8;  break;
		case 4: fmt = LUD_PIXFMT_SRGBA8; break;
		default:
			/* sRGB only meaningful for color images */
			srgb = 0;
			break;
		}
	}

	if (!srgb) {
		switch (channels) {
		case 1: fmt = LUD_PIXFMT_R8;    break;
		case 3: fmt = LUD_PIXFMT_RGB8;  break;
		case 4: fmt = LUD_PIXFMT_RGBA8; break;
		default:
			lud_err("unsupported channel count %d: %s", channels, path);
			stbi_image_free(data);
			return out;
		}
	}

	out = lud_make_texture(&(lud_texture_desc_t){
		.width = w,
		.height = h,
		.format = fmt,
		.min_filter = min_filter,
		.mag_filter = mag_filter,
		.data = data,
	});

	stbi_image_free(data);
	return out;
}

lud_texture_t
lud_load_texture(const char *path,
                    enum lud_filter min_filter,
                    enum lud_filter mag_filter)
{
	return load_image(path, min_filter, mag_filter, 0);
}

lud_texture_t
lud_load_texture_srgb(const char *path,
                         enum lud_filter min_filter,
                         enum lud_filter mag_filter)
{
	return load_image(path, min_filter, mag_filter, 1);
}
