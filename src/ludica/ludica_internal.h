#ifndef LUDICA_INTERNAL_H_
#define LUDICA_INTERNAL_H_

#include "ludica.h"

/* Limits */
#define LUD_GAMEPAD_MAX 4
#define LUD_GAMEPAD_AXIS_MAX 8
#define LUD_GAMEPAD_BUTTON_MAX 64

/* Internal state */
typedef struct lud__state {
	lud_desc_t desc;
	int quit_requested;
	int win_width, win_height;
	unsigned long long time_init;   /* nanoseconds at init */
	unsigned long long time_prev;   /* nanoseconds at previous frame */
	float frame_dt;                 /* seconds */
	int gles_version;               /* 2 or 3, resolved at init */
	int is_fullscreen;
	int saved_x, saved_y;           /* windowed geometry before fullscreen */
	int saved_w, saved_h;
} lud__state_t;

extern lud__state_t lud__state;

/* event.c */
void lud__event_init(void);
void lud__event_push(const lud_event_t *ev);
int  lud__event_poll(lud_event_t *ev);

/* input.c */
void lud__input_init(void);
void lud__input_update(const lud_event_t *ev);

/* action.c */
void lud__action_update(void);  /* call once per frame before frame callback */
void lud__action_reset(void);   /* call on shutdown */

/* timing.c */
unsigned long long lud__clock_now(void);
double lud__clock_diff(unsigned long long t1, unsigned long long t0);

/* Platform backend interface.
 * Each platform_*.c implements these. */
int  lud__platform_init(const lud_desc_t *desc);
void lud__platform_shutdown(void);
void lud__platform_poll_events(void); /* push events via lud__event_push() */
void lud__platform_swap(void);
void lud__platform_set_fullscreen(int fullscreen);

#endif /* LUDICA_INTERNAL_H_ */
