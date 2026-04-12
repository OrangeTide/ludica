/* input.c */
#include "input.h"
#include "state.h"
#include "gui.h"
#include <initgl.h>

#define INPUT_SOURCE_KEYBOARD (1U)
#define INPUT_SOURCE_GAMEPAD(n) (2U << (n))

static void
push_action(enum input_action act, int down, unsigned char source)
{
	unsigned char orig = state.input.action_down[act];

	if (down) {
		if (!(orig & source)) {
			state.input.fresh_keys = true;
			// printf("PRESS act=%d\n", act);
		}
		state.input.action_down[act] |= source;
	} else {
		if (orig & source) {
			state.input.fresh_keys = true;
			// printf("RELEASE act=%d\n", act);
		}
		state.input.action_down[act] &= ~source;
	}
}

void
input_event(const sapp_event *ev)
{
	if (gui_event(ev)) {
		return;
	}
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
			if (!ev->key_repeat) {
				push_action(state.input.translate[ev->key_code], ev->type == SAPP_EVENTTYPE_KEY_DOWN, INPUT_SOURCE_KEYBOARD);
			}
		}
		break;
	default:
		; // ignored
	}
}


/* process gamepad inputs outside of the input_event() callback */
void
input_gamepad(void)
{
	if (!sgamepad_get_max_supported_gamepads()) {
		return;
	}

	sgamepad_record_state();
	sgamepad_gamepad_state pad_a = {0};
	sgamepad_get_gamepad_state(0, &pad_a);

	push_action(INPUT_ACTION_UP, pad_a.digital_inputs & SGAMEPAD_GAMEPAD_DPAD_UP, INPUT_SOURCE_GAMEPAD(0));
	push_action(INPUT_ACTION_DOWN, pad_a.digital_inputs & SGAMEPAD_GAMEPAD_DPAD_DOWN, INPUT_SOURCE_GAMEPAD(0));
	push_action(INPUT_ACTION_LEFT, pad_a.digital_inputs & SGAMEPAD_GAMEPAD_DPAD_LEFT, INPUT_SOURCE_GAMEPAD(0));
	push_action(INPUT_ACTION_RIGHT, pad_a.digital_inputs & SGAMEPAD_GAMEPAD_DPAD_RIGHT, INPUT_SOURCE_GAMEPAD(0));
}
