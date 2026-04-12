#ifndef INITGL_INTERNAL_H_
#define INITGL_INTERNAL_H_

#include "initgl.h"

/* Limits */
#define INITGL_GAMEPAD_MAX 4
#define INITGL_GAMEPAD_AXIS_MAX 8
#define INITGL_GAMEPAD_BUTTON_MAX 64

/* Internal state */
typedef struct initgl__state {
	initgl_desc_t desc;
	int quit_requested;
	int win_width, win_height;
	unsigned long long time_init;   /* nanoseconds at init */
	unsigned long long time_prev;   /* nanoseconds at previous frame */
	float frame_dt;                 /* seconds */
} initgl__state_t;

extern initgl__state_t initgl__state;

/* event.c */
void initgl__event_init(void);
void initgl__event_push(const initgl_event_t *ev);
int  initgl__event_poll(initgl_event_t *ev);

/* input.c */
void initgl__input_init(void);
void initgl__input_update(const initgl_event_t *ev);

/* timing.c */
unsigned long long initgl__clock_now(void);
double initgl__clock_diff(unsigned long long t1, unsigned long long t0);

/* Platform backend interface.
 * Each platform_*.c implements these. */
int  initgl__platform_init(const initgl_desc_t *desc);
void initgl__platform_shutdown(void);
void initgl__platform_poll_events(void); /* push events via initgl__event_push() */
void initgl__platform_swap(void);

#endif /* INITGL_INTERNAL_H_ */
