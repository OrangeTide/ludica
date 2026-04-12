/* hellodungeon.c */
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include <initgl.h>
#include "graphics.h"
#include "gui.h"
#include "input.h"
#include "state.h"
#include "timer.h"

/***********************************************************************
 * Prototypes
 ***********************************************************************/
static void game_tick(void);

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
	gfx_init();
	gui_init();
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
		.window_title = "HelloDungeon",
		.init_cb = init,
		.cleanup_cb = done,
		.frame_cb = frame_update,
		.event_cb = input_event,
		.width = 1024,
		.height = 768,
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
