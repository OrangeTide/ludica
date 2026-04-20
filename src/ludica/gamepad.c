#include "gamepad.h"
#include "ludica.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#if defined(__EMSCRIPTEN__)
/* Stubs -- HTML5 Gamepad API not yet wired up */
static bool gamepad_open_one(int id) { (void)id; return false; }
static void gamepad_close_one(int id) { (void)id; }
bool gamepad_poll(void) { return false; }
bool gamepad_wait(int msec) { (void)msec; return false; }
#elif defined(__linux__)
#include "linux/gamepad-linux.h"
#elif defined(_WIN32)
#include "win32/gamepad-windows.h"
#else
#error Unsupported platform
#endif

struct gamepad_state gamepad_state[GAMEPAD_MAX];
struct gamepad_info gamepad_info[GAMEPAD_MAX];

//////// Common

bool
gamepad_init(void)
{
	unsigned i;
	unsigned pads = 0;

	for (i = 0; i < GAMEPAD_MAX; i++) {
		if (gamepad_open_one(i) == 0) {
			pads++;
		}
	}

	if (!pads)
		return false; /* failure - unable to open any devices */

	return true;
}

void
gamepad_done(void)
{
	gamepad_close_one(0);
}

/* useful for debugging information */
void
gamepad_dump(void)
{
	unsigned i, j;

	for (i = 0; i < GAMEPAD_MAX; i++) {
		if (!gamepad_exists(i))
			continue;
		lud_logj(LUD_LOG_DEBUG, "gamepad state",
		         "pad", LUD_UINT(i),
		         "buttons_hi", LUD_HEX(gamepad_state[i].button[0]),
		         "buttons_lo", LUD_HEX(gamepad_state[i].button[1]),
		         (const char *)0);
		for (j = 0; j < gamepad_info[i].num_axis; j++ ) {
			lud_logj(LUD_LOG_DEBUG, "gamepad axis",
			         "pad", LUD_UINT(i),
			         "axis", LUD_UINT(j),
			         "value", LUD_FLT(gamepad_axis(i, j)),
			         (const char *)0);
		}
	}
}
