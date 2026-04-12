#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include <initgl.h>
#include "HandmadeMath.h"
#include "cube-sapp.glsl.h"

/***********************************************************************
 * Prototypes
 ***********************************************************************/
static void game_tick(void);

static void gfx_init(void);
static void gfx_done(void);
static void gfx_draw(void);

/***********************************************************************
 * State
 ***********************************************************************/

#define TRANSLATE_KEYS_MAX 512			// maximum number of key codes, see enum sapp_keycode
#define INPUT_ACTION_MAX 16			// number of input actions, see enum input_action
#define TICK_TOLERANCE_NS 1000000		// per-frame tolerance in nanoseconds
#define TICK_DURATION_NS (100000000 / 60)	// resolution of tick-based events (virtual frame time)
#define TIMER_DISABLED 0xfffffffful		// magic tick value for a disabled timer

enum input_action {
	INPUT_ACTION_NONE,
	INPUT_ACTION_UP,
	INPUT_ACTION_DOWN,
	INPUT_ACTION_LEFT,
	INPUT_ACTION_RIGHT,
};

enum gamestate {
	GAMESTATE_PLAYING,
};

struct state {
	enum gamestate gamestate;
	struct input_state {
		/** globally enables/disables action input */
		bool enabled;
		/** indicates new keys since last read */
		bool fresh_keys;
		/** true if an action is active (key is held down) */
		bool action_down[INPUT_ACTION_MAX];
		/** Translates SAPP_KEYCODE_x to one of the action_down[] states */
		unsigned char translate[TRANSLATE_KEYS_MAX];
	} input;

	struct timing {
		unsigned tick;
		int accumulator;
	} timing;

	struct game {
		unsigned timer_started;
		float rx, ry;
	} game;

	struct gfx {
		sg_pass_action pass_action;
		sg_pipeline pip;
		sg_bindings bind;
		int vertex_count; /* for debugging */
		int index_count; /* for drawing */
	} gfx;

};

static struct state state = {
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
};

static void
timer_stop(unsigned *t)
{
	*t = TIMER_DISABLED;
}

static void
timer_start(unsigned *t)
{
	*t = state.timing.tick;
}

static bool
timer_now(unsigned t)
{
	return state.timing.tick == t;
}

/***********************************************************************
 * Base
 ***********************************************************************/

static void
init(void)
{
	gfx_init();
}

static void
done(void)
{
	gfx_done();
}

static void
frame_update(void)
{
	unsigned frame_time_ns = (unsigned)(sapp_frame_duration() * 1000000000.0);

	if (frame_time_ns > 33333333) {
		frame_time_ns = 33333333;
	}

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
}

static void
input(const sapp_event *ev)
{
	if (!state.input.enabled)
		return;
	switch (ev->type) {
	case SAPP_EVENTTYPE_KEY_DOWN:
		if (ev->key_code == SAPP_KEYCODE_ESCAPE) {
			sapp_request_quit();
			break;
		}
		// fallthru
	case SAPP_EVENTTYPE_KEY_UP:
		if (ev->key_code > SAPP_KEYCODE_INVALID
		    && ev->key_code < TRANSLATE_KEYS_MAX) {
			state.input.action_down[state.input.translate[ev->key_code]]
				= ev->type == SAPP_EVENTTYPE_KEY_DOWN;
			state.input.fresh_keys = true;
		}
		break;
	default:
		; // ignored
	}
}

sapp_desc
sokol_main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;

	sapp_desc desc = {
		.window_title = "demo1",
		.init_cb = init,
		.cleanup_cb = done,
		.frame_cb = frame_update,
		.event_cb = input,
		.width = 640,
		.height = 480,
		.icon.sokol_default = true,
#if defined __EMSCRIPTEN__
		.html5_canvas_name = "canvas",
#endif
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
 * Graphics
 ***********************************************************************/

static void
gfx_init(void)
{
	const sg_desc desc = {
		/*
		.buffer_pool_size = 2,
		.image_pool_size = 3,
		.shader_pool_size = 2,
		.pipeline_pool_size = 2,
		.pass_pool_size = 1,
		*/
		.context = sapp_sgcontext()
	};
	sg_setup(&desc);

	float vertices[] = {
		// positions            // colors
		-1.0, -1.0, -1.0,	1.0, 0.0, 0.0, 1.0,
		 1.0, -1.0, -1.0,	1.0, 0.0, 0.0, 1.0,
		 1.0,  1.0, -1.0,	1.0, 0.0, 0.0, 1.0,
		-1.0,  1.0, -1.0,	1.0, 0.0, 0.0, 1.0,

		-1.0, -1.0,  1.0,	0.0, 1.0, 0.0, 1.0,
		 1.0, -1.0,  1.0,	0.0, 1.0, 0.0, 1.0,
		 1.0,  1.0,  1.0,	0.0, 1.0, 0.0, 1.0,
		-1.0,  1.0,  1.0,	0.0, 1.0, 0.0, 1.0,

		-1.0, -1.0, -1.0,	0.0, 0.0, 1.0, 1.0,
		-1.0,  1.0, -1.0,	0.0, 0.0, 1.0, 1.0,
		-1.0,  1.0,  1.0,	0.0, 0.0, 1.0, 1.0,
		-1.0, -1.0,  1.0,	0.0, 0.0, 1.0, 1.0,

		 1.0, -1.0, -1.0,	1.0, 0.5, 0.0, 1.0,
		 1.0,  1.0, -1.0,	1.0, 0.5, 0.0, 1.0,
		 1.0,  1.0,  1.0,	1.0, 0.5, 0.0, 1.0,
		 1.0, -1.0,  1.0,	1.0, 0.5, 0.0, 1.0,

		-1.0, -1.0, -1.0,	0.0, 0.5, 1.0, 1.0,
		-1.0, -1.0,  1.0,	0.0, 0.5, 1.0, 1.0,
		 1.0, -1.0,  1.0,	0.0, 0.5, 1.0, 1.0,
		 1.0, -1.0, -1.0,	0.0, 0.5, 1.0, 1.0,

		-1.0,  1.0, -1.0,	1.0, 0.0, 0.5, 1.0,
		-1.0,  1.0,  1.0,	1.0, 0.0, 0.5, 1.0,
		 1.0,  1.0,  1.0,	1.0, 0.0, 0.5, 1.0,
		 1.0,  1.0, -1.0,	1.0, 0.0, 0.5, 1.0,
	};

	sg_buffer_desc vbuffer = {
		.data = SG_RANGE(vertices),
		.type = SG_BUFFERTYPE_VERTEXBUFFER,
		.usage = SG_USAGE_IMMUTABLE,
		.label = "cube-vertices",
	};
	state.gfx.bind.vertex_buffers[0] = sg_make_buffer(&vbuffer);
	state.gfx.vertex_count = sizeof(vertices) / sizeof(*vertices) / (3 + 4);
	// printf("vertex count = %d\n", state.gfx.vertex_count);

	uint16_t indices[] = {
		0, 1, 2,  0, 2, 3,
		6, 5, 4,  7, 6, 4,
		8, 9, 10,  8, 10, 11,
		14, 13, 12,  15, 14, 12,
		16, 17, 18,  16, 18, 19,
		22, 21, 20,  23, 22, 20
	};

	sg_buffer_desc ibuffer = {
		.type = SG_BUFFERTYPE_INDEXBUFFER,
		.data = SG_RANGE(indices),
		.usage = SG_USAGE_IMMUTABLE,
		.label = "cube-indices"
	};
	sg_buffer ibuf = sg_make_buffer(&ibuffer);
	state.gfx.bind.index_buffer = ibuf;
	state.gfx.index_count = sizeof(indices) / sizeof(*indices);
	// printf("index count = %d\n", state.gfx.index_count);

	sg_shader shd = sg_make_shader(demo1_cube_shader_desc(sg_query_backend()));

	sg_pipeline_desc pipeline_desc = {
		.shader = shd,
		.layout = {
			.attrs = {
				[ATTR_demo1_vs_position].format = SG_VERTEXFORMAT_FLOAT3,
				[ATTR_demo1_vs_color0].format = SG_VERTEXFORMAT_FLOAT4
			}
		},
		.index_type = SG_INDEXTYPE_UINT16,
		.cull_mode = SG_CULLMODE_BACK,
		.depth = {
			.write_enabled = true,
			.compare = SG_COMPAREFUNC_LESS_EQUAL,
		},
		.label = "cube-pipeline",
	};

	state.gfx.pip = sg_make_pipeline(&pipeline_desc);
}

static void
gfx_done(void)
{
	sg_shutdown();
}

static void
gfx_draw(void)
{
	demo1_vs_params_t vs_params;
	const int canvas_width = sapp_width();
	const int canvas_height = sapp_height();

	hmm_mat4 proj = HMM_Perspective(60.0f, canvas_width / (float)canvas_height, 0.01f, 10.0f);
	hmm_mat4 view = HMM_LookAt(HMM_Vec3(0.0f, 1.5f, 6.0f), HMM_Vec3(0.0f, 0.0f, 0.0f), HMM_Vec3(0.0f, 1.0f, 0.0f));
	hmm_mat4 view_proj = HMM_MultiplyMat4(proj, view);

	// hmm_mat4 model = HMM_Mat4d(1.0f);
	hmm_mat4 rxm = HMM_Rotate(state.game.rx, HMM_Vec3(1.0f, 0.0f, 0.0f));
	hmm_mat4 rym = HMM_Rotate(state.game.ry, HMM_Vec3(0.0f, 1.0f, 0.0f));
	hmm_mat4 model = HMM_MultiplyMat4(rxm, rym);

	vs_params.mvp = HMM_MultiplyMat4(view_proj, model);

	sg_begin_default_pass(&state.gfx.pass_action, canvas_width, canvas_height);
	sg_apply_pipeline(state.gfx.pip);
	sg_apply_bindings(&state.gfx.bind);
	sg_apply_uniforms(SG_SHADERSTAGE_VS, SLOT_demo1_vs_params, &SG_RANGE(vs_params));
	sg_draw(0, state.gfx.index_count, 1);
	sg_end_pass();
	sg_commit();
}
