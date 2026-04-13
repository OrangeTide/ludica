#ifndef LUDICA_H_
#define LUDICA_H_

#include "ludica_input.h"
#include "ludica_anim.h"
#include "ludica_audio.h"
#include "ludica_auto.h"

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
	int gles_version;       /* 2 or 3; 0 defaults to 2 */

	/* Command-line arguments (optional).
	 * Set argc/argv to enable --flag parsing in lud_run(). */
	int argc;
	const char *const *argv;
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
int    lud_gles_version(void); /* 2 or 3 */

/* Fullscreen */
void lud_set_fullscreen(int fullscreen); /* 0 = windowed, non-zero = fullscreen */
int  lud_is_fullscreen(void);

/* Logging */
void lud_log(const char *msg, ...);
void lud_err(const char *msg, ...);

#endif /* LUDICA_H_ */
