/* demo3.c */
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include <initgl.h>
#include "graphics.h"
#include "gui.h"
#include "input.h"
#include "screen.h"
#include "state.h"
#include "timer.h"

/***********************************************************************
 * Prototypes
 ***********************************************************************/
static void game_tick(void);

static void palette_init(uint32_t palette[]);

struct state state = {
	.gamestate = GAMESTATE_PLAYING,
	.input = {
		.enabled = true,
		.translate = {
			[SAPP_KEYCODE_UP] = INPUT_ACTION_UP,
			[SAPP_KEYCODE_W] = INPUT_ACTION_UP,
			[SAPP_KEYCODE_DOWN] = INPUT_ACTION_DOWN,
			[SAPP_KEYCODE_S] = INPUT_ACTION_DOWN,
			[SAPP_KEYCODE_LEFT] = INPUT_ACTION_LEFT,
			[SAPP_KEYCODE_A] = INPUT_ACTION_LEFT,
			[SAPP_KEYCODE_RIGHT] = INPUT_ACTION_RIGHT,
			[SAPP_KEYCODE_D] = INPUT_ACTION_RIGHT,
		},
	},
	.gfx.pass_action = {
		.colors[0] = {
			.action = SG_ACTION_CLEAR,
			.value = { 0.25f, 0.25f, 0.50f, 1.0f }
		}
	},
	.gui.pass_gui = (sg_pass_action) {
		.colors[0] = {
			.action = SG_ACTION_DONTCARE,
			.value = {0.2f, 0.3f, 0.3f, 1.0f },
		}
	},
};

/***********************************************************************
 * Base
 ***********************************************************************/

static void
init(void)
{
	sgamepad_init();
	screen_init(160, 100);
	palette_init(state.screen.palette);
	gfx_init();
	gui_init();
	// screen_update();
}

static void
done(void)
{
	gui_done();
	gfx_done();
}

static void
frame_update(void)
{
	unsigned frame_time_ns = (unsigned)(sapp_frame_duration() * 1000000000.0);

	if (frame_time_ns > 33333333) {
		frame_time_ns = 33333333;
	}

	input_gamepad();

	state.timing.accumulator += frame_time_ns;
	while(state.timing.accumulator > -TICK_TOLERANCE_NS) {
		if (timer_now(state.game.timer_started)) {
		    state.gamestate = GAMESTATE_PLAYING;
		}

		switch (state.gamestate) {
		case GAMESTATE_PLAYING:
			game_tick();
		}

		state.timing.tick++;
		state.timing.accumulator -= TICK_DURATION_NS;
	}

	gfx_draw();
	gui_frame();
}

sapp_desc
sokol_main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;

	sapp_desc desc = {
		.window_title = "demo3",
		.init_cb = init,
		.cleanup_cb = done,
		.frame_cb = frame_update,
		.event_cb = input_event,
		.width = 640,
		.height = 480,
		.icon.sokol_default = true,
	};

	return desc;
}

/***********************************************************************
 * Game
 ***********************************************************************/

static void
game_tick(void)
{
	state.game.rx += 0.1f;
	state.game.ry += 0.2f;
}

/***********************************************************************
 * Palette
 ***********************************************************************/

/* initialize 256 color palette */
static void
palette_init(uint32_t palette[])
{
	unsigned i;
	unsigned char r, g, b;
	uint32_t c;


	/* traditional TTL RGB : colors 0~15 */
	i = 0;
	palette[i++] = 0x000000; // 0 - black
	palette[i++] = 0x800000; // 1 - red
	palette[i++] = 0x008000; // 2 - green
	palette[i++] = 0x808000; // 3 - brown
	palette[i++] = 0x000080; // 4 - blue
	palette[i++] = 0x800080; // 5 - magenta
	palette[i++] = 0x008080; // 6 - cyan
	palette[i++] = 0xc0c0c0; // 7 - white
	/* high-intensity (bright mode) */
	palette[i++] = 0x808080; // 8 - dark grey
	palette[i++] = 0xff0000; // 9 - bright red
	palette[i++] = 0x00ff00; // 10 - bright green
	palette[i++] = 0xffff00; // 11 - yellow
	palette[i++] = 0x0000ff; // 12 - bright blue
	palette[i++] = 0xff00ff; // 13 - bright magenta
	palette[i++] = 0x00ffff; // 14 - bright cyan
	palette[i++] = 0xffffff; // 15 - bright white

	/* RGB 6x6x6 color cube : colors 16~231 */
	for (; i < 232; i++) {
		unsigned n = i - 16;

		b = n % 6;
		g = n / 6 % 6;
		r = n / 36 % 6;

		c = r * 255 / 5;
		c <<= 8;
		c |= g * 255 / 5;
		c <<= 8;
		c |= b * 255 / 5;

		palette[i] = c;
	}

	/* grey scale : colors 232~255 */
	for (; i < 256; i++) {
		unsigned n = i - 232;
		/* 08, 12, 1c, 26, ... d0, da, e4, ee */
		c = 8 + n * 230 / 23;
		palette[i] = c | (c << 8) | (c << 16);
	}
}
