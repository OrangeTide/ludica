#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_audio.h"
#include "sokol_glue.h"
#include "HandmadeMath.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "palette.glsl.h"

/***********************************************************************
 * Prototypes
 ***********************************************************************/
static void game_tick(void);

static void gfx_init(void);
static void gfx_done(void);
static void gfx_draw(void);

static void palette_init(uint32_t palette[]);

void screen_init(int width, int height);
void screen_done(void);
void screen_fill(uint8_t v);
void screen_random(void);
void screen_update(void);
uint8_t *screen_pixels(unsigned *width, unsigned *height, unsigned *rowbytes);
void screen_pattern_herringbone(void);

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

	struct screen {
		unsigned width;
		unsigned height;
		sg_range pixels;
		uint32_t palette[256];
	} screen;
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
	screen_init(160, 100);
	palette_init(state.screen.palette);
	gfx_init();
	// screen_update();
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
		.window_title = "demo2",
		.init_cb = init,
		.cleanup_cb = done,
		.frame_cb = frame_update,
		.event_cb = input,
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

	float quad_data[] = {
		-1.0,  1.0,  1.0,
		 1.0,  1.0,  1.0,
		-1.0, -1.0,  1.0,
		 1.0, -1.0,  1.0,
	};

	sg_buffer_desc quad_vbuffer = {
		.data = SG_RANGE(quad_data),
		.type = SG_BUFFERTYPE_VERTEXBUFFER,
		.usage = SG_USAGE_IMMUTABLE,
		.label = "quad-vertices",
	};
	state.gfx.bind.vertex_buffers[0] = sg_make_buffer(&quad_vbuffer);
	state.gfx.vertex_count = sizeof(quad_data) / sizeof(*quad_data) / (3);

	sg_image_desc screen_img = {
		.width = state.screen.width,
		.height = state.screen.height,
		.pixel_format = SG_PIXELFORMAT_R8,
		.usage = SG_USAGE_STREAM,
		.min_filter = SG_FILTER_LINEAR,
		.mag_filter = SG_FILTER_NEAREST,
		.wrap_u = SG_WRAP_CLAMP_TO_EDGE,
		.wrap_v = SG_WRAP_CLAMP_TO_EDGE,
		.label = "screen-texture",
	};
	sg_image img = sg_make_image(&screen_img);
	state.gfx.bind.fs_images[SLOT_demo2_screentexture] = img;

	sg_image_desc palette_desc = {
		.width = 256,
		.height = 1,
		.data.subimage[0][0] = SG_RANGE(state.screen.palette),
		.label = "palette-tex",
	};
	sg_image palette = sg_make_image(&palette_desc);
	state.gfx.bind.fs_images[SLOT_demo2_palette] = palette;

	sg_shader shd = sg_make_shader(demo2_palette_shader_desc(sg_query_backend()));

	sg_pipeline_desc pipeline_desc = {
		.shader = shd,
		.layout = {
			.attrs = {
				[ATTR_demo2_vs_position].format = SG_VERTEXFORMAT_FLOAT3,
			}
		},
		.cull_mode = SG_CULLMODE_BACK,
		.primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP,
		.label = "quad-pipeline",
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
	const int canvas_width = sapp_width();
	const int canvas_height = sapp_height();

#if 0
	demo2_vs_params_t vs_params;
	hmm_mat4 proj = HMM_Perspective(60.0f, canvas_width / (float)canvas_height, 0.01f, 10.0f);
	hmm_mat4 view = HMM_LookAt(HMM_Vec3(0.0f, 1.5f, 6.0f), HMM_Vec3(0.0f, 0.0f, 0.0f), HMM_Vec3(0.0f, 1.0f, 0.0f));
	demo2_vs_params_t vs_params;
	hmm_mat4 view_proj = HMM_MultiplyMat4(proj, view);

	// hmm_mat4 model = HMM_Mat4d(1.0f);
	hmm_mat4 rxm = HMM_Rotate(state.game.rx, HMM_Vec3(1.0f, 0.0f, 0.0f));
	hmm_mat4 rym = HMM_Rotate(state.game.ry, HMM_Vec3(0.0f, 1.0f, 0.0f));
	hmm_mat4 model = HMM_MultiplyMat4(rxm, rym);

	vs_params.mvp = HMM_MultiplyMat4(view_proj, model);
#endif

	screen_pattern_herringbone();
	screen_update();

	sg_begin_default_pass(&state.gfx.pass_action, canvas_width, canvas_height);
	sg_apply_pipeline(state.gfx.pip);
	sg_apply_bindings(&state.gfx.bind);
	// sg_apply_uniforms(SG_SHADERSTAGE_VS, SLOT_demo2_vs_params, &SG_RANGE(vs_params));
	sg_draw(0, state.gfx.vertex_count, 1);
	sg_end_pass();
	sg_commit();
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

/***********************************************************************
 * Screen
 ***********************************************************************/

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
	sg_update_image(state.gfx.bind.fs_images[SLOT_demo2_screentexture], &data);
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
