/* deadzone_test.c : verify the gamepad analog dead zone (input.c) */
/* Made by a machine. PUBLIC DOMAIN (CC0-1.0) */

/* Pure-logic test: inject raw normalized axis values through the internal
 * input-update path, then read them back via lud_gamepad_axis and check
 * the dead-zone filtering and rescaling. No window or GL context. */

#include "ludica_internal.h"
#include "ludica_input.h"
#include <math.h>
#include <stdio.h>

static int checks;
static int failures;

static void
expect(const char *what, int cond)
{
	checks++;
	if (cond) {
		printf("ok   %s\n", what);
	} else {
		failures++;
		printf("FAIL %s\n", what);
	}
}

static int
near(float a, float b)
{
	return fabsf(a - b) < 0.001f;
}

/* Feed one raw axis value into pad 0, axis 0. */
static void
set_raw(float v)
{
	lud_event_t ev = {0};
	ev.type = LUD_EV_GAMEPAD_AXIS;
	ev.gamepad_axis.id = 0;
	ev.gamepad_axis.axis = 0;
	ev.gamepad_axis.value = v;
	lud__input_update(&ev);
}

static float
axis(void)
{
	return lud_gamepad_axis(0, 0);
}

int
main(void)
{
	lud__input_init();
	expect("default dead zone is 0.15", near(lud_gamepad_deadzone(), 0.15f));

	/* Inside the dead zone reads as exactly zero. */
	set_raw(0.10f);
	expect("inside dead zone reads 0", axis() == 0.0f);
	set_raw(0.15f);
	expect("at the dead-zone edge reads 0", axis() == 0.0f);

	/* Past the edge, output is rescaled so (dz,1] maps to (0,1]. */
	set_raw(0.50f);
	expect("0.50 rescaled past 0.15 dead zone",
	       near(axis(), (0.50f - 0.15f) / (1.0f - 0.15f)));
	set_raw(1.0f);
	expect("full deflection still reaches 1.0", near(axis(), 1.0f));

	/* Sign is preserved. */
	set_raw(-0.50f);
	expect("negative axis keeps its sign",
	       near(axis(), -(0.50f - 0.15f) / (1.0f - 0.15f)));
	set_raw(-1.0f);
	expect("negative full deflection reaches -1.0", near(axis(), -1.0f));

	/* Dead zone of 0 passes raw values straight through. */
	lud_gamepad_set_deadzone(0.0f);
	set_raw(0.10f);
	expect("zero dead zone passes small values", near(axis(), 0.10f));

	/* A larger dead zone gates and rescales accordingly. */
	lud_gamepad_set_deadzone(0.50f);
	set_raw(0.40f);
	expect("0.40 inside a 0.50 dead zone reads 0", axis() == 0.0f);
	set_raw(0.75f);
	expect("0.75 rescaled past 0.50 dead zone",
	       near(axis(), (0.75f - 0.50f) / (1.0f - 0.50f)));

	/* Setter clamps to [0, 0.95]. */
	lud_gamepad_set_deadzone(2.0f);
	expect("dead zone clamps high to 0.95", near(lud_gamepad_deadzone(), 0.95f));
	lud_gamepad_set_deadzone(-1.0f);
	expect("dead zone clamps low to 0", near(lud_gamepad_deadzone(), 0.0f));

	/* Out-of-range queries are still safe. */
	expect("invalid pad id reads 0", lud_gamepad_axis(-1, 0) == 0.0f);
	expect("invalid axis reads 0", lud_gamepad_axis(0, 999) == 0.0f);

	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
