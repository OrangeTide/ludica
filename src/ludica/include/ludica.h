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

	/* Command-line arguments (desktop only, ignored on WASM).
	 * Set argc/argv so lud_run() can populate the config store.
	 * Use lud_get_config() in callbacks to read options. */
	int argc;
	char **argv;
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

/* Configuration store (platform-agnostic, works on desktop and WASM).
 * On desktop, populated from argv by lud_run().
 * On WASM, populated from JavaScript via lud_set_config() before init.
 * lud_get_config() returns the value string, or NULL if key not set.
 * Boolean flags (--key with no =value) are stored as "" (empty, non-NULL). */
const char *lud_get_config(const char *key);
int         lud_set_config(const char *key, const char *value);

/* Fullscreen */
void lud_set_fullscreen(int fullscreen); /* 0 = windowed, non-zero = fullscreen */
int  lud_is_fullscreen(void);

/* Logging.
 *
 * Two APIs:
 *   lud_log / lud_err : printf-style; formatted result goes into the
 *                       JSON "msg" field of a {"t":..,"lvl":..,"msg":..}\n line.
 *   lud_logj          : structured varargs; caller supplies typed key/value
 *                       pairs terminated by NULL.  Emits one JSON object per
 *                       call, one line, to stderr.
 *
 * Example:
 *   lud_logj(LUD_LOG_INFO, "loaded texture",
 *            "tex",   LUD_STR(name),
 *            "bytes", LUD_INT(sz),
 *            (const char *)0);
 */
typedef enum {
	LUD_LOG_DEBUG = 0,
	LUD_LOG_INFO  = 1,
	LUD_LOG_WARN  = 2,
	LUD_LOG_ERROR = 3,
} lud_log_level_t;

/* Value tags used by lud_logj varargs.  Prefer the LUD_STR / LUD_INT / ...
 * helper macros below so the call site reads as key/value pairs. */
enum {
	LUD_LOGV_END  = 0,
	LUD_LOGV_STR  = 1,   /* const char * */
	LUD_LOGV_INT  = 2,   /* long long */
	LUD_LOGV_UINT = 3,   /* unsigned long long */
	LUD_LOGV_HEX  = 4,   /* unsigned long long (rendered 0x..) */
	LUD_LOGV_FLT  = 5,   /* double */
	LUD_LOGV_BOOL = 6,   /* int (0 = false, nonzero = true) */
};

#define LUD_STR(x)  LUD_LOGV_STR,  (const char *)(x)
#define LUD_INT(x)  LUD_LOGV_INT,  (long long)(x)
#define LUD_UINT(x) LUD_LOGV_UINT, (unsigned long long)(x)
#define LUD_HEX(x)  LUD_LOGV_HEX,  (unsigned long long)(x)
#define LUD_FLT(x)  LUD_LOGV_FLT,  (double)(x)
#define LUD_BOOL(x) LUD_LOGV_BOOL, (int)((x) ? 1 : 0)

void lud_log(const char *msg, ...);
void lud_err(const char *msg, ...);
void lud_logj(lud_log_level_t lvl, const char *msg, ...);

#endif /* LUDICA_H_ */
