#ifndef LITHOS_H_
#define LITHOS_H_

#include "lithos_input.h"

#define LITHOS_OK (0)
#define LITHOS_ERR (-1)

/* Application descriptor.
 * Fill this struct and pass it to lithos_run().
 * Zero-initialized fields use sensible defaults. */
typedef struct lithos_desc {
	const char *app_name;
	int width, height;      /* initial window size (default: 640x480) */

	/* Callbacks */
	void (*init)(void);             /* called once after window+GL are ready */
	void (*frame)(float dt);        /* called each frame; dt = seconds since last frame */
	void (*cleanup)(void);          /* called before shutdown */
	int  (*event)(const lithos_event_t *ev); /* return non-zero = event consumed */

	/* User data pointer, retrievable via lithos_userdata() */
	void *user_data;

	/* Hints (zero = sensible default) */
	int target_fps;         /* 0 = vsync (default) */
	int fullscreen;         /* non-zero = start fullscreen */
	int resizable;          /* non-zero = allow resize */
} lithos_desc_t;

/* Entry point. Owns the main loop. Returns exit code.
 * On WASM this never returns. */
int lithos_run(const lithos_desc_t *desc);

/* Callable from callbacks */
void   lithos_quit(void);
void  *lithos_userdata(void);
int    lithos_width(void);
int    lithos_height(void);
double lithos_time(void);        /* seconds since init */
float  lithos_frame_time(void);  /* dt of last frame */

/* Logging */
void lithos_log(const char *msg, ...);
void lithos_err(const char *msg, ...);

#endif /* LITHOS_H_ */
