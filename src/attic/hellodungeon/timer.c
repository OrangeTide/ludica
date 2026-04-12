/* timer.c */
#include "timer.h"
#include "state.h"

void
timer_stop(unsigned *t)
{
	*t = TIMER_DISABLED;
}

void
timer_start(unsigned *t)
{
	*t = state.timing.tick;
}

bool
timer_now(unsigned t)
{
	return state.timing.tick == t;
}
