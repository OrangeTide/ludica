#ifndef LITHOS_INTERNAL_H_
#define LITHOS_INTERNAL_H_

#include "lithos.h"

/* Limits */
#define LITHOS_GAMEPAD_MAX 4
#define LITHOS_GAMEPAD_AXIS_MAX 8
#define LITHOS_GAMEPAD_BUTTON_MAX 64

/* Internal state */
typedef struct lithos__state {
	lithos_desc_t desc;
	int quit_requested;
	int win_width, win_height;
	unsigned long long time_init;   /* nanoseconds at init */
	unsigned long long time_prev;   /* nanoseconds at previous frame */
	float frame_dt;                 /* seconds */
} lithos__state_t;

extern lithos__state_t lithos__state;

/* event.c */
void lithos__event_init(void);
void lithos__event_push(const lithos_event_t *ev);
int  lithos__event_poll(lithos_event_t *ev);

/* input.c */
void lithos__input_init(void);
void lithos__input_update(const lithos_event_t *ev);

/* timing.c */
unsigned long long lithos__clock_now(void);
double lithos__clock_diff(unsigned long long t1, unsigned long long t0);

/* Platform backend interface.
 * Each platform_*.c implements these. */
int  lithos__platform_init(const lithos_desc_t *desc);
void lithos__platform_shutdown(void);
void lithos__platform_poll_events(void); /* push events via lithos__event_push() */
void lithos__platform_swap(void);

#endif /* LITHOS_INTERNAL_H_ */
