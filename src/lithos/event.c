#include "lithos_internal.h"
#include <string.h>

#define EVENT_QUEUE_SIZE 256

static lithos_event_t queue[EVENT_QUEUE_SIZE];
static unsigned head;
static unsigned count;

void
lithos__event_init(void)
{
	head = 0;
	count = 0;
}

void
lithos__event_push(const lithos_event_t *ev)
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
lithos__event_poll(lithos_event_t *ev)
{
	if (count == 0) {
		return 0;
	}
	*ev = queue[head];
	head = (head + 1) % EVENT_QUEUE_SIZE;
	count--;
	return 1;
}
