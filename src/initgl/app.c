#include "initgl_internal.h"
#include <string.h>

/* Global state */
initgl__state_t initgl__state;

static void
apply_defaults(initgl_desc_t *desc)
{
	if (desc->width <= 0)
		desc->width = 640;
	if (desc->height <= 0)
		desc->height = 480;
	if (!desc->app_name)
		desc->app_name = "initgl";
}

static void
dispatch_events(void)
{
	initgl_event_t ev;
	while (initgl__event_poll(&ev)) {
		/* Update polled input state regardless of whether user consumes it */
		initgl__input_update(&ev);

		/* Dispatch to user callback */
		if (initgl__state.desc.event) {
			initgl__state.desc.event(&ev);
		}

		/* Handle window resize */
		if (ev.type == INITGL_EV_RESIZED) {
			initgl__state.win_width = ev.resize.width;
			initgl__state.win_height = ev.resize.height;
		}
	}
}

static void
frame_tick(void)
{
	unsigned long long now = initgl__clock_now();
	double dt = initgl__clock_diff(now, initgl__state.time_prev);
	initgl__state.time_prev = now;
	initgl__state.frame_dt = (float)dt;

	dispatch_events();

	if (initgl__state.desc.frame) {
		initgl__state.desc.frame(initgl__state.frame_dt);
	}

	initgl__platform_swap();
}

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>

static void
em_frame(void)
{
	if (initgl__state.quit_requested) {
		emscripten_cancel_main_loop();
		if (initgl__state.desc.cleanup)
			initgl__state.desc.cleanup();
		initgl__platform_shutdown();
		return;
	}
	frame_tick();
}
#endif

int
initgl_run(const initgl_desc_t *desc)
{
	memset(&initgl__state, 0, sizeof(initgl__state));
	initgl__state.desc = *desc;
	apply_defaults(&initgl__state.desc);

	initgl__event_init();
	initgl__input_init();

	if (initgl__platform_init(&initgl__state.desc) != INITGL_OK) {
		return 1;
	}

	initgl__state.win_width = initgl__state.desc.width;
	initgl__state.win_height = initgl__state.desc.height;
	initgl__state.time_init = initgl__clock_now();
	initgl__state.time_prev = initgl__state.time_init;

	if (initgl__state.desc.init) {
		initgl__state.desc.init();
	}

#if defined(__EMSCRIPTEN__)
	int fps = initgl__state.desc.target_fps > 0 ? initgl__state.desc.target_fps : 0;
	emscripten_set_main_loop(em_frame, fps, 1);
#else
	while (!initgl__state.quit_requested) {
		initgl__platform_poll_events();
		frame_tick();
	}

	if (initgl__state.desc.cleanup) {
		initgl__state.desc.cleanup();
	}
	initgl__platform_shutdown();
#endif

	return 0;
}

/* Public API callable from callbacks */

void
initgl_quit(void)
{
	initgl__state.quit_requested = 1;
}

void *
initgl_userdata(void)
{
	return initgl__state.desc.user_data;
}

int
initgl_width(void)
{
	return initgl__state.win_width;
}

int
initgl_height(void)
{
	return initgl__state.win_height;
}

double
initgl_time(void)
{
	return initgl__clock_diff(initgl__clock_now(), initgl__state.time_init);
}

float
initgl_frame_time(void)
{
	return initgl__state.frame_dt;
}
