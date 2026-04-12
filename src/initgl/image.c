/*
 * image.c — Load textures from image files via stb_image.
 */

#include "initgl_internal.h"
#include "initgl_gfx.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

initgl_texture_t
initgl_load_texture(const char *path,
                    enum initgl_filter min_filter,
                    enum initgl_filter mag_filter)
{
	initgl_texture_t out = {0};
	int w, h, channels;
	unsigned char *data;
	enum initgl_pixel_format fmt;

	data = stbi_load(path, &w, &h, &channels, 0);
	if (!data) {
		initgl_err("failed to load image: %s", path);
		return out;
	}

	switch (channels) {
	case 1: fmt = INITGL_PIXFMT_R8;    break;
	case 3: fmt = INITGL_PIXFMT_RGB8;  break;
	case 4: fmt = INITGL_PIXFMT_RGBA8; break;
	default:
		initgl_err("unsupported channel count %d: %s", channels, path);
		stbi_image_free(data);
		return out;
	}

	out = initgl_make_texture(&(initgl_texture_desc_t){
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
