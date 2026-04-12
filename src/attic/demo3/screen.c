/* screen.c */
#include "screen.h"
#include "state.h"
#if defined(__ANDROID__)
#include "palette.glsl.android.h"
#elif defined(__EMSCRIPTEN__)
#include "palette.glsl.webgl.h"
#else
#include "palette.glsl.gl.h"
#endif

#include <stdlib.h>
#include <initgl.h>

void
screen_init(int width, int height)
{
	state.screen.width = width;
	state.screen.height = height;
	state.screen.pixels.size = width * height;
	state.screen.pixels.ptr = malloc(state.screen.pixels.size);
	if (!state.screen.pixels.ptr) {
		// SOKOL_LOG("alloc failed");
		abort(); // SOKOL_ABORT();
	}
	screen_fill(0);
}

void
screen_done(void)
{
	if (state.screen.pixels.ptr) {
		free((uint8_t*)state.screen.pixels.ptr);
		state.screen.pixels.ptr = NULL;
		state.screen.pixels.size = 0;
	}
}

void
screen_fill(uint8_t v)
{
	if (state.screen.pixels.ptr) {
		memset((uint8_t*)state.screen.pixels.ptr, v, state.screen.width * state.screen.height);
	}
}

void
screen_random(void)
{
	if (state.screen.pixels.ptr) {
		uint8_t *pixels = (uint8_t*)state.screen.pixels.ptr;
		unsigned x, y;
		for (y = 0; y < state.screen.height; y++) {
			uint8_t *row = pixels + y * state.screen.width;
			for (x = 0; x < state.screen.width; x++) {
				row[x] = rand() % 256;
			}
		}
	}
}

void
screen_update(void)
{
	sg_image_data data = { .subimage[0][0] = state.screen.pixels };
	sg_update_image(state.gfx.bind.fs_images[SLOT_demo3_screentexture], &data);
}

uint8_t *
screen_pixels(unsigned *width, unsigned *height, unsigned *rowbytes)
{
	if (!state.screen.pixels.ptr) {
		if (width)
			*width = 0;
		if (height)
			*height = 0;
		if (rowbytes)
			*rowbytes = 0;
		return NULL;
	}

	if (width)
		*width = state.screen.width;
	if (height)
		*height = state.screen.height;
	if (rowbytes)
		*rowbytes = state.screen.width;

	return (uint8_t*)state.screen.pixels.ptr;
}

/* a simple animated pattern - digital herringbone */
void
screen_pattern_herringbone(void)
{
	unsigned width, height;
	unsigned char *pixels = screen_pixels(&width, &height, NULL);
	unsigned x, y;
	unsigned char *row = pixels;
	for (y = 0; y < height; y++, row += width) {
		for (x = 0; x < width; x++) {
			row[x] = ((x + (y^12) + state.timing.tick / 30) % 16) + 232;
		}
	}
}
