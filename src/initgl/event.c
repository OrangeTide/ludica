#include "initgl_internal.h"
#include <string.h>

#define EVENT_QUEUE_SIZE 256

static initgl_event_t queue[EVENT_QUEUE_SIZE];
static unsigned head;
static unsigned count;

void
initgl__event_init(void)
{
	head = 0;
	count = 0;
}

void
initgl__event_push(const initgl_event_t *ev)
{
	if (count >= EVENT_QUEUE_SIZE) {
		/* queue full — drop oldest */
		head = (head + 1) % EVENT_QUEUE_SIZE;
		count--;
	}
	unsigned tail = (head + count) % EVENT_QUEUE_SIZE;
	queue[tail] = *ev;
	count++;
}

int
initgl__event_poll(initgl_event_t *ev)
{
	if (count == 0) {
		return 0;
	}
	*ev = queue[head];
	head = (head + 1) % EVENT_QUEUE_SIZE;
	count--;
	return 1;
}
