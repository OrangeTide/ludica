#include "lithos_internal.h"
#include <string.h>

/* Global state */
lithos__state_t lithos__state;

static void
apply_defaults(lithos_desc_t *desc)
{
	if (desc->width <= 0)
		desc->width = 640;
	if (desc->height <= 0)
		desc->height = 480;
	if (!desc->app_name)
		desc->app_name = "lithos";
}

static void
dispatch_events(void)
{
	lithos_event_t ev;
	while (lithos__event_poll(&ev)) {
		/* Update polled input state regardless of whether user consumes it */
		lithos__input_update(&ev);

		/* Dispatch to user callback */
		if (lithos__state.desc.event) {
			lithos__state.desc.event(&ev);
		}

		/* Handle window resize */
		if (ev.type == LITHOS_EV_RESIZED) {
			lithos__state.win_width = ev.resize.width;
			lithos__state.win_height = ev.resize.height;
		}
	}
}

static void
frame_tick(void)
{
	unsigned long long now = lithos__clock_now();
	double dt = lithos__clock_diff(now, lithos__state.time_prev);
	lithos__state.time_prev = now;
	lithos__state.frame_dt = (float)dt;

	dispatch_events();

	if (lithos__state.desc.frame) {
		lithos__state.desc.frame(lithos__state.frame_dt);
	}

	lithos__platform_swap();
}

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>

static void
em_frame(void)
{
	if (lithos__state.quit_requested) {
		emscripten_cancel_main_loop();
		if (lithos__state.desc.cleanup)
			lithos__state.desc.cleanup();
		lithos__platform_shutdown();
		return;
	}
	frame_tick();
}
#endif

int
lithos_run(const lithos_desc_t *desc)
{
	memset(&lithos__state, 0, sizeof(lithos__state));
	lithos__state.desc = *desc;
	apply_defaults(&lithos__state.desc);

	lithos__event_init();
	lithos__input_init();

	if (lithos__platform_init(&lithos__state.desc) != LITHOS_OK) {
		return 1;
	}

	lithos__state.win_width = lithos__state.desc.width;
	lithos__state.win_height = lithos__state.desc.height;
	lithos__state.time_init = lithos__clock_now();
	lithos__state.time_prev = lithos__state.time_init;

	if (lithos__state.desc.init) {
		lithos__state.desc.init();
	}

#if defined(__EMSCRIPTEN__)
	int fps = lithos__state.desc.target_fps > 0 ? lithos__state.desc.target_fps : 0;
	emscripten_set_main_loop(em_frame, fps, 1);
#else
	while (!lithos__state.quit_requested) {
		lithos__platform_poll_events();
		frame_tick();
	}

	if (lithos__state.desc.cleanup) {
		lithos__state.desc.cleanup();
	}
	lithos__platform_shutdown();
#endif

	return 0;
}

/* Public API callable from callbacks */

void
lithos_quit(void)
{
	lithos__state.quit_requested = 1;
}

void *
lithos_userdata(void)
{
	return lithos__state.desc.user_data;
}

int
lithos_width(void)
{
	return lithos__state.win_width;
}

int
lithos_height(void)
{
	return lithos__state.win_height;
}

double
lithos_time(void)
{
	return lithos__clock_diff(lithos__clock_now(), lithos__state.time_init);
}

float
lithos_frame_time(void)
{
	return lithos__state.frame_dt;
}
