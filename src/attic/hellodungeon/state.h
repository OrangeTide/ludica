/* state.h */
#ifndef DEMO3_STATE_H_
#define DEMO3_STATE_H_

#include <stdbool.h>
#include <sokol_gfx.h>

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

	struct gui {
		bool show_another_window;
		sg_pass_action pass_gui;
	} gui;
};

extern struct state state;
#endif
