/*
 * demo01_retrocrt — palette-indexed framebuffer with CRT post-process.
 * Opens a window, renders an animated XOR pattern through the framebuffer
 * API with scanlines + barrel distortion, prints input events to stderr,
 * quits on Escape or window close.
 */

#include "ludica.h"
#include "ludica_gfx.h"
#include <GLES2/gl2.h>
#include <stdio.h>


#define SCREEN_W 320
#define SCREEN_H 240

static lud_framebuffer_t fb;
static unsigned int palette[256];
static unsigned tick;

static void
palette_init(void)
{
	unsigned i;
	unsigned char r, g, b;

	/* Colors 0-15: traditional TTL RGB */
	i = 0;
	palette[i++] = 0xff000000; /* black */
	palette[i++] = 0xff000080; /* red */
	palette[i++] = 0xff008000; /* green */
	palette[i++] = 0xff008080; /* brown */
	palette[i++] = 0xff800000; /* blue */
	palette[i++] = 0xff800080; /* magenta */
	palette[i++] = 0xff808000; /* cyan */
	palette[i++] = 0xffc0c0c0; /* white */
	palette[i++] = 0xff808080; /* dark grey */
	palette[i++] = 0xff0000ff; /* bright red */
	palette[i++] = 0xff00ff00; /* bright green */
	palette[i++] = 0xff00ffff; /* yellow */
	palette[i++] = 0xffff0000; /* bright blue */
	palette[i++] = 0xffff00ff; /* bright magenta */
	palette[i++] = 0xffffff00; /* bright cyan */
	palette[i++] = 0xffffffff; /* bright white */

	/* Colors 16-231: 6x6x6 RGB cube */
	for (; i < 232; i++) {
		unsigned n = i - 16;
		b = (n % 6) * 255 / 5;
		g = (n / 6 % 6) * 255 / 5;
		r = (n / 36 % 6) * 255 / 5;
		palette[i] = 0xff000000u | ((unsigned)b << 16) | ((unsigned)g << 8) | r;
	}

	/* Colors 232-255: greyscale ramp */
	for (; i < 256; i++) {
		unsigned c = 8 + (i - 232) * 230 / 23;
		palette[i] = 0xff000000u | (c << 16) | (c << 8) | c;
	}
}

static void
init(void)
{
	fb = lud_make_framebuffer(&(lud_framebuffer_desc_t){
		.width = SCREEN_W,
		.height = SCREEN_H,
		.crt = LUD_CRT_SCANLINES,
	});

	palette_init();
	lud_framebuffer_palette(fb, palette);
}

static void
animate(void)
{
	unsigned char *pixels;
	int x, y;

	tick++;
	pixels = lud_framebuffer_lock(fb);
	if (!pixels) return;

	for (y = 0; y < SCREEN_H; y++) {
		unsigned char *row = pixels + y * SCREEN_W;
		for (x = 0; x < SCREEN_W; x++) {
			row[x] = (unsigned char)(((x + (y ^ 12) + tick) % 16) + 232);
		}
	}

	lud_framebuffer_unlock(fb);
}

static int
on_event(const lud_event_t *ev)
{
	switch (ev->type) {
	case LUD_EV_KEY_DOWN:
		fprintf(stderr, "key down: %d%s\n", ev->key.keycode,
			ev->key.repeat ? " (repeat)" : "");
		if (ev->key.keycode == LUD_KEY_ESCAPE)
			lud_quit();
		return 1;
	case LUD_EV_KEY_UP:
		fprintf(stderr, "key up: %d\n", ev->key.keycode);
		return 1;
	case LUD_EV_MOUSE_MOVE:
		fprintf(stderr, "mouse: %d,%d\n", ev->mouse_move.x, ev->mouse_move.y);
		return 1;
	case LUD_EV_MOUSE_DOWN:
		fprintf(stderr, "mouse down: btn=%d at %d,%d\n",
			ev->mouse_button.button, ev->mouse_button.x, ev->mouse_button.y);
		return 1;
	case LUD_EV_MOUSE_UP:
		fprintf(stderr, "mouse up: btn=%d at %d,%d\n",
			ev->mouse_button.button, ev->mouse_button.x, ev->mouse_button.y);
		return 1;
	case LUD_EV_MOUSE_SCROLL:
		fprintf(stderr, "scroll: dx=%.1f dy=%.1f\n", ev->scroll.dx, ev->scroll.dy);
		return 1;
	case LUD_EV_RESIZED:
		fprintf(stderr, "resize: %dx%d\n", ev->resize.width, ev->resize.height);
		return 1;
	case LUD_EV_FOCUS:
		fprintf(stderr, "focus\n");
		return 1;
	case LUD_EV_UNFOCUS:
		fprintf(stderr, "unfocus\n");
		return 1;
	default:
		return 0;
	}
}

static void
frame(float dt)
{
	(void)dt;

	animate();

	lud_viewport(0, 0, lud_width(), lud_height());
	lud_clear(0.0f, 0.0f, 0.0f, 1.0f);
	lud_framebuffer_blit(fb);

	glFlush();
}

static void
cleanup(void)
{
	lud_destroy_framebuffer(fb);
}

int
main(int argc, char **argv)
{
	return lud_run(&(lud_desc_t){
		.app_name = "ludica gfx test",
		.width = 640,
		.height = 480,
		.resizable = 1,
		.argc = argc,
		.argv = (const char *const *)argv,
		.init = init,
		.frame = frame,
		.cleanup = cleanup,
		.event = on_event,
	});
}
