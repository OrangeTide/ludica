#ifndef LUDICA_H_
#define LUDICA_H_

#include "ludica_input.h"

#ifndef LUD_VERSION
#define LUD_VERSION "unknown"
#endif

#define LUD_OK (0)
#define LUD_ERR (-1)

/* Application descriptor.
 * Fill this struct and pass it to lud_run().
 * Zero-initialized fields use sensible defaults. */
typedef struct lud_desc {
	const char *app_name;
	int width, height;      /* initial window size (default: 640x480) */

	/* Callbacks */
	void (*init)(void);             /* called once after window+GL are ready */
	void (*frame)(float dt);        /* called each frame; dt = seconds since last frame */
	void (*cleanup)(void);          /* called before shutdown */
	int  (*event)(const lud_event_t *ev); /* return non-zero = event consumed */

	/* User data pointer, retrievable via lud_userdata() */
	void *user_data;

	/* Hints (zero = sensible default) */
	int target_fps;         /* 0 = vsync (default) */
	int fullscreen;         /* non-zero = start fullscreen */
	int resizable;          /* non-zero = allow resize */
} lud_desc_t;

/* Entry point. Owns the main loop. Returns exit code.
 * On WASM this never returns. */
int lud_run(const lud_desc_t *desc);

/* Callable from callbacks */
void   lud_quit(void);
void  *lud_userdata(void);
int    lud_width(void);
int    lud_height(void);
double lud_time(void);        /* seconds since init */
float  lud_frame_time(void);  /* dt of last frame */

/* Logging */
void lud_log(const char *msg, ...);
void lud_err(const char *msg, ...);

#endif /* LUDICA_H_ */
