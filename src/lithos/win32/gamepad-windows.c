#include "gamepad.h"
#include "gamepad-windows.h"
#include <windows.h>
#include <xinput.h>
#include <stdlib.h>

static DWORD gamepad_packetnumber[GAMEPAD_MAX]; /* cache of XINPUT_STATE.dwPacketNumber */

#define GAMEPAD_DPAD_UP(id) (gamepad_state[id].button[0] & XINPUT_GAMEPAD_DPAD_UP)
#define GAMEPAD_DPAD_DOWN(id) (gamepad_state[id].button[0] & XINPUT_GAMEPAD_DPAD_DOWN)
#define GAMEPAD_DPAD_LEFT(id) (gamepad_state[id].button[0] & XINPUT_GAMEPAD_DPAD_LEFT)
#define GAMEPAD_DPAD_RIGHT(id) (gamepad_state[id].button[0] & XINPUT_GAMEPAD_DPAD_RIGHT)
#define GAMEPAD_BUTTON_START(id) (gamepad_state[id].button[0] & XINPUT_GAMEPAD_START)
#define GAMEPAD_BUTTON_BACK(id) (gamepad_state[id].button[0] & XINPUT_GAMEPAD_BACK)
#define GAMEPAD_BUTTON_LEFT_THUMB(id) (gamepad_state[id].button[0] & XINPUT_GAMEPAD_LEFT_THUMB)
#define GAMEPAD_BUTTON_RIGHT_THUMB(id) (gamepad_state[id].button[0] & XINPUT_GAMEPAD_RIGHT_THUMB)
#define GAMEPAD_BUTTON_LEFT_SHOULDER(id) (gamepad_state[id].button[0] & XINPUT_GAMEPAD_LEFT_SHOULDER)
#define GAMEPAD_BUTTON_RIGHT_SHOULDER(id) (gamepad_state[id].button[0] & XINPUT_GAMEPAD_RIGHT_SHOULDER)
#define GAMEPAD_BUTTON_LEFT_THUMB(id) (gamepad_state[id].button[0] & XINPUT_GAMEPAD_LEFT_THUMB)
#define GAMEPAD_BUTTON_A(id) (gamepad_state[id].button[0] & XINPUT_GAMEPAD_A)
#define GAMEPAD_BUTTON_B(id) (gamepad_state[id].button[0] & XINPUT_GAMEPAD_B)
#define GAMEPAD_BUTTON_X(id) (gamepad_state[id].button[0] & XINPUT_GAMEPAD_X)
#define GAMEPAD_BUTTON_Y(id) (gamepad_state[id].button[0] & XINPUT_GAMEPAD_Y)

void
gamepad_close_one(int id)
{
}

bool
gamepad_open_one(int id)
{
	struct gamepad_info *info = &gamepad_info[id];
	struct gamepad_state *gs = &gamepad_state[id];

	memset(gs, 0, sizeof(*gs));
	info->num_button = 14; /* XBox 360 controller */

	// TODO: implement this

	return true;
}

/* process an event from one gamepad.
 * return - true if events were processed. false is no pending events. */
static bool
gamepad_process_one(int id)
{
	unsigned i;
	XINPUT_STATE state;
	DWORD dwResult;

	ZeroMemory(&state, sizeof(XINPUT_STATE));
	dwResult = XInputGetState(id, &state);

	if (dwResult != ERROR_SUCCESS)
		return false; /* controller is not connected (ERROR_DEVICE_NOT_CONNECTED) */
	if (gamepad_packetnumber[id] == state.dwPacketNumber)
		return false; /* no change to state */

	struct gamepad_state *gs = &gamepad_state[id];

#define SHORT2FLOAT(s) ((s) < 0 ? (s) / 32768.0f : (s) / 32767.0f)
	// TODO: resolve these mappings so that Linux and Windows implementations match
	gs->axis[0] = SHORT2FLOAT(state.Gamepad.sThumbLX);
	gs->axis[1] = SHORT2FLOAT(state.Gamepad.sThumbLY);
	gs->axis[2] = state.Gamepad.bLeftTrigger / 255.0f;
	gs->axis[3] = SHORT2FLOAT(state.Gamepad.sThumbRX);
	gs->axis[4] = SHORT2FLOAT(state.Gamepad.sThumbRY);
	gs->axis[5] = state.Gamepad.bRightTrigger / 255.0f;
#undef SHORT2FLOAT

	return true;
}

/* return true if events have changed out state */
bool
gamepad_poll(void)
{
	unsigned i;

	for (i=0; i< XUSER_MAX_COUNT; i++ ) {
		gamepad_process_one(i);
	}
}

/* wait gamepads for updates, with a timeout.
 * msec = 0 : no timeout
 * msec < 0 : wait forever
 * msec > 0 : milliseconds to wait for events. returns after processing events.
 * return true if events have changed out state. false if no state change. */
bool
gamepad_wait(int msec)
{
	abort(); // TODO: implement
}


// TODO: XinputEnable() to call on WM_ACTIVEAPP changes
// TODO: vibration (low_speed_intensity, high_speed_intensity). XInputSetState()
