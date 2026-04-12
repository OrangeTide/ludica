#ifndef INITGL_H_
#define INITGL_H_

#include "initgl_input.h"

#define INITGL_OK (0)
#define INITGL_ERR (-1)

/* Application descriptor.
 * Fill this struct and pass it to initgl_run().
 * Zero-initialized fields use sensible defaults. */
typedef struct initgl_desc {
	const char *app_name;
	int width, height;      /* initial window size (default: 640x480) */

	/* Callbacks */
	void (*init)(void);             /* called once after window+GL are ready */
	void (*frame)(float dt);        /* called each frame; dt = seconds since last frame */
	void (*cleanup)(void);          /* called before shutdown */
	int  (*event)(const initgl_event_t *ev); /* return non-zero = event consumed */

	/* User data pointer, retrievable via initgl_userdata() */
	void *user_data;

	/* Hints (zero = sensible default) */
	int target_fps;         /* 0 = vsync (default) */
	int fullscreen;         /* non-zero = start fullscreen */
	int resizable;          /* non-zero = allow resize */
} initgl_desc_t;

/* Entry point. Owns the main loop. Returns exit code.
 * On WASM this never returns. */
int initgl_run(const initgl_desc_t *desc);

/* Callable from callbacks */
void   initgl_quit(void);
void  *initgl_userdata(void);
int    initgl_width(void);
int    initgl_height(void);
double initgl_time(void);        /* seconds since init */
float  initgl_frame_time(void);  /* dt of last frame */

/* Logging */
void initgl_log(const char *msg, ...);
void initgl_err(const char *msg, ...);

#endif /* INITGL_H_ */
