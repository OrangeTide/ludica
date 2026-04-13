/*
 * image.c — Load textures from image files via stb_image.
 */

#include "lithos_internal.h"
#include "lithos_gfx.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

lithos_texture_t
lithos_load_texture(const char *path,
                    enum lithos_filter min_filter,
                    enum lithos_filter mag_filter)
{
	lithos_texture_t out = {0};
	int w, h, channels;
	unsigned char *data;
	enum lithos_pixel_format fmt;

	data = stbi_load(path, &w, &h, &channels, 0);
	if (!data) {
		lithos_err("failed to load image: %s", path);
		return out;
	}

	switch (channels) {
	case 1: fmt = LITHOS_PIXFMT_R8;    break;
	case 3: fmt = LITHOS_PIXFMT_RGB8;  break;
	case 4: fmt = LITHOS_PIXFMT_RGBA8; break;
	default:
		lithos_err("unsupported channel count %d: %s", channels, path);
		stbi_image_free(data);
		return out;
	}

	out = lithos_make_texture(&(lithos_texture_desc_t){
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
