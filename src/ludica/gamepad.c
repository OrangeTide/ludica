#include "gamepad.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#if defined(__linux__)
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
		fprintf(stderr, "pad%u: buttons=%08X%08X\n", i,
				gamepad_state[i].button[0],
				gamepad_state[i].button[1]);
		for (j = 0; j < gamepad_info[i].num_axis; j++ ) {
			fprintf(stderr, "        Axis #%u : %g\n", j, gamepad_axis(i, j));
		}
	}
}
