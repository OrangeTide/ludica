#include "ludica_internal.h"
#include <string.h>

/* Global state */
lud__state_t lud__state;

static void
apply_defaults(lud_desc_t *desc)
{
	if (desc->width <= 0)
		desc->width = 640;
	if (desc->height <= 0)
		desc->height = 480;
	if (!desc->app_name)
		desc->app_name = "ludica";
	if (desc->gles_version == 0)
		desc->gles_version = 2;
}

static void
dispatch_events(void)
{
	lud_event_t ev;
	while (lud__event_poll(&ev)) {
		/* Update polled input state regardless of whether user consumes it */
		lud__input_update(&ev);

		/* Dispatch to user callback */
		if (lud__state.desc.event) {
			lud__state.desc.event(&ev);
		}

		/* Handle window resize */
		if (ev.type == LUD_EV_RESIZED) {
			lud__state.win_width = ev.resize.width;
			lud__state.win_height = ev.resize.height;
		}
	}
}

static void
frame_tick(void)
{
	unsigned long long now = lud__clock_now();
	double dt = lud__clock_diff(now, lud__state.time_prev);
	lud__state.time_prev = now;

	if (lud__state.fixed_dt)
		lud__state.frame_dt = 1.0f / 60.0f;
	else
		lud__state.frame_dt = (float)dt;

	dispatch_events();
	lud__action_update();

	if (lud__state.desc.frame) {
		lud__state.desc.frame(lud__state.frame_dt);
	}

	lud__platform_swap();
	lud__state.frame_count++;
}

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>

static void
em_frame(void)
{
	if (lud__state.quit_requested) {
		emscripten_cancel_main_loop();
		if (lud__state.desc.cleanup)
			lud__state.desc.cleanup();
		lud__platform_shutdown();
		lud__config_cleanup();
		return;
	}
	frame_tick();
}
#endif

int
lud_run(const lud_desc_t *desc)
{
	memset(&lud__state, 0, sizeof(lud__state));
	lud__state.desc = *desc;
	apply_defaults(&lud__state.desc);
	lud__parse_args(&lud__state.desc);

	lud__event_init();
	lud__input_init();

	if (lud__platform_init(&lud__state.desc) != LUD_OK) {
		return 1;
	}

	lud__state.win_width = lud__state.desc.width;
	lud__state.win_height = lud__state.desc.height;
	lud__state.gles_version = lud__state.desc.gles_version;
	lud__state.time_init = lud__clock_now();
	lud__state.time_prev = lud__state.time_init;

	lud__auto_init();

	if (lud__state.desc.fullscreen) {
		lud__platform_set_fullscreen(1);
		lud__state.is_fullscreen = 1;
	}

	if (lud__state.desc.init) {
		lud__state.desc.init();
	}

#if defined(__EMSCRIPTEN__)
	int fps = lud__state.desc.target_fps > 0 ? lud__state.desc.target_fps : 0;
	emscripten_set_main_loop(em_frame, fps, 1);
#else
	while (!lud__state.quit_requested) {
		lud__platform_poll_events();
		lud__auto_poll();
		if (lud__auto_frame_allowed()) {
			frame_tick();
			lud__auto_post_frame();
		}
	}

	if (lud__state.desc.cleanup) {
		lud__state.desc.cleanup();
	}
	lud__auto_shutdown();
	lud__action_reset();
	lud__platform_shutdown();
	lud__config_cleanup();
#endif

	return 0;
}

/* Public API callable from callbacks */

void
lud_quit(void)
{
	lud__state.quit_requested = 1;
}

void *
lud_userdata(void)
{
	return lud__state.desc.user_data;
}

int
lud_width(void)
{
	return lud__state.win_width;
}

int
lud_height(void)
{
	return lud__state.win_height;
}

double
lud_time(void)
{
	return lud__clock_diff(lud__clock_now(), lud__state.time_init);
}

float
lud_frame_time(void)
{
	return lud__state.frame_dt;
}

int
lud_gles_version(void)
{
	return lud__state.gles_version;
}

void
lud_set_fullscreen(int fullscreen)
{
	int want = !!fullscreen;
	if (want == lud__state.is_fullscreen)
		return;
	lud__platform_set_fullscreen(want);
	lud__state.is_fullscreen = want;
}

int
lud_is_fullscreen(void)
{
	return lud__state.is_fullscreen;
}

