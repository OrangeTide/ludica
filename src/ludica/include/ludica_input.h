#ifndef LUDICA_INPUT_H_
#define LUDICA_INPUT_H_

#include <stddef.h> /* size_t */

/* Platform-independent keycodes */
enum lud_keycode {
	LUD_KEY_UNKNOWN = 0,

	/* Printable keys */
	LUD_KEY_SPACE = 32,
	LUD_KEY_APOSTROPHE = 39,
	LUD_KEY_COMMA = 44,
	LUD_KEY_MINUS = 45,
	LUD_KEY_PERIOD = 46,
	LUD_KEY_SLASH = 47,
	LUD_KEY_0 = 48,
	LUD_KEY_1, LUD_KEY_2, LUD_KEY_3, LUD_KEY_4,
	LUD_KEY_5, LUD_KEY_6, LUD_KEY_7, LUD_KEY_8, LUD_KEY_9,
	LUD_KEY_SEMICOLON = 59,
	LUD_KEY_EQUAL = 61,
	LUD_KEY_A = 65,
	LUD_KEY_B, LUD_KEY_C, LUD_KEY_D, LUD_KEY_E,
	LUD_KEY_F, LUD_KEY_G, LUD_KEY_H, LUD_KEY_I,
	LUD_KEY_J, LUD_KEY_K, LUD_KEY_L, LUD_KEY_M,
	LUD_KEY_N, LUD_KEY_O, LUD_KEY_P, LUD_KEY_Q,
	LUD_KEY_R, LUD_KEY_S, LUD_KEY_T, LUD_KEY_U,
	LUD_KEY_V, LUD_KEY_W, LUD_KEY_X, LUD_KEY_Y, LUD_KEY_Z,
	LUD_KEY_LEFT_BRACKET = 91,
	LUD_KEY_BACKSLASH = 92,
	LUD_KEY_RIGHT_BRACKET = 93,
	LUD_KEY_GRAVE_ACCENT = 96,

	/* Function keys */
	LUD_KEY_ESCAPE = 256,
	LUD_KEY_ENTER,
	LUD_KEY_TAB,
	LUD_KEY_BACKSPACE,
	LUD_KEY_INSERT,
	LUD_KEY_DELETE,
	LUD_KEY_RIGHT,
	LUD_KEY_LEFT,
	LUD_KEY_DOWN,
	LUD_KEY_UP,
	LUD_KEY_PAGE_UP,
	LUD_KEY_PAGE_DOWN,
	LUD_KEY_HOME,
	LUD_KEY_END,
	LUD_KEY_CAPS_LOCK = 280,
	LUD_KEY_SCROLL_LOCK,
	LUD_KEY_NUM_LOCK,
	LUD_KEY_PRINT_SCREEN,
	LUD_KEY_PAUSE,
	LUD_KEY_F1 = 290,
	LUD_KEY_F2, LUD_KEY_F3, LUD_KEY_F4, LUD_KEY_F5,
	LUD_KEY_F6, LUD_KEY_F7, LUD_KEY_F8, LUD_KEY_F9,
	LUD_KEY_F10, LUD_KEY_F11, LUD_KEY_F12,
	LUD_KEY_KP_0 = 320,
	LUD_KEY_KP_1, LUD_KEY_KP_2, LUD_KEY_KP_3, LUD_KEY_KP_4,
	LUD_KEY_KP_5, LUD_KEY_KP_6, LUD_KEY_KP_7, LUD_KEY_KP_8,
	LUD_KEY_KP_9,
	LUD_KEY_KP_DECIMAL,
	LUD_KEY_KP_DIVIDE,
	LUD_KEY_KP_MULTIPLY,
	LUD_KEY_KP_SUBTRACT,
	LUD_KEY_KP_ADD,
	LUD_KEY_KP_ENTER,
	LUD_KEY_KP_EQUAL,
	LUD_KEY_LEFT_SHIFT = 340,
	LUD_KEY_LEFT_CONTROL,
	LUD_KEY_LEFT_ALT,
	LUD_KEY_LEFT_SUPER,
	LUD_KEY_RIGHT_SHIFT,
	LUD_KEY_RIGHT_CONTROL,
	LUD_KEY_RIGHT_ALT,
	LUD_KEY_RIGHT_SUPER,
	LUD_KEY_MENU,

	LUD_KEY__COUNT
};

/* Mouse buttons */
enum lud_mouse_button {
	LUD_MOUSE_LEFT = 0,
	LUD_MOUSE_RIGHT = 1,
	LUD_MOUSE_MIDDLE = 2,
};

/* Event types */
enum lud_event_type {
	LUD_EV_NONE = 0,
	LUD_EV_KEY_DOWN,
	LUD_EV_KEY_UP,
	LUD_EV_CHAR,
	LUD_EV_MOUSE_MOVE,
	LUD_EV_MOUSE_DOWN,
	LUD_EV_MOUSE_UP,
	LUD_EV_MOUSE_SCROLL,
	LUD_EV_GAMEPAD_CONN,
	LUD_EV_GAMEPAD_DISCONN,
	LUD_EV_GAMEPAD_BUTTON,
	LUD_EV_GAMEPAD_AXIS,
	LUD_EV_RESIZED,
	LUD_EV_FOCUS,
	LUD_EV_UNFOCUS,
};

/* Modifier key bitmask */
enum lud_modifier {
	LUD_MOD_SHIFT = 1,
	LUD_MOD_CTRL = 2,
	LUD_MOD_ALT = 4,
	LUD_MOD_SUPER = 8,
};

typedef struct lud_event {
	enum lud_event_type type;
	unsigned modifiers; /* bitmask of lud_modifier */
	union {
		struct {
			enum lud_keycode keycode;
			int repeat;
		} key;
		struct {
			unsigned codepoint; /* Unicode codepoint */
		} ch;
		struct {
			int x, y;
			int dx, dy;
		} mouse_move;
		struct {
			int x, y;
			enum lud_mouse_button button;
		} mouse_button;
		struct {
			float dx, dy;
		} scroll;
		struct {
			int id;
			int button;
			int down;
		} gamepad_button;
		struct {
			int id;
			int axis;
			float value;
		} gamepad_axis;
		struct {
			int id;
		} gamepad_conn;
		struct {
			int width, height;
		} resize;
	};
} lud_event_t;

/* Polled input state queries */
int lud_key_down(enum lud_keycode key);
void lud_mouse_pos(int *x, int *y);
int lud_mouse_button_down(enum lud_mouse_button button);
float lud_gamepad_axis(int id, int axis);
int lud_gamepad_button_down(int id, int button);
int lud_gamepad_connected(int id);

/* Analog stick dead zone, as a fraction of full deflection (default 0.15).
 * Axis values within this radius read as 0; values past it are rescaled so
 * output still reaches 1.0. Clamped to [0, 0.95]. Applies to every pad. */
void  lud_gamepad_set_deadzone(float dz);
float lud_gamepad_deadzone(void);

/* Named key lookup (returns LUD_KEY_UNKNOWN on failure) */
enum lud_keycode lud_key_from_name(const char *name);

/* ---- Action bindings ---- */

/* Opaque action handle (id==0 is invalid) */
typedef struct { unsigned id; } lud_action_t;

/* Create or find a named action.  Returns existing handle if name matches. */
lud_action_t lud_make_action(const char *name);

/* Find an existing action by name.  Returns {0} if not found. */
lud_action_t lud_find_action(const char *name);

/* Bind a key or gamepad button to an action (multiple bindings allowed). */
void lud_bind_key(enum lud_keycode key, lud_action_t action);
void lud_bind_gamepad_button(int pad, int button, lud_action_t action);

/* Remove all bindings from an action (keeps the action itself). */
void lud_unbind_action(lud_action_t action);

/* Poll action state (updated once per frame before the frame callback). */
int lud_action_down(lud_action_t action);     /* held this frame */
int lud_action_pressed(lud_action_t action);   /* just went down */
int lud_action_released(lud_action_t action);  /* just went up */

/* ---- Clipboard ---- */

/* Format strings for the typed/async API below.  TEXT is plain UTF-8; PNG is
 * a PNG-encoded image; URI_LIST is a newline-separated list of file:// URIs
 * (the standard target for copying and dropping files). */
#define LUD_CLIPBOARD_TEXT     "text/plain;charset=utf-8"
#define LUD_CLIPBOARD_PNG      "image/png"
#define LUD_CLIPBOARD_URI_LIST "text/uri-list"

/* Synchronous text clipboard.
 *
 * lud_clipboard_get_text() returns a malloc'd, NUL-terminated UTF-8 string
 * that the caller must free().  It returns NULL when the clipboard is empty,
 * holds no text, or the owner does not respond within a short timeout.
 *
 * lud_clipboard_set_text() copies utf8 onto the clipboard.  It returns 0 on
 * success and non-zero on failure.  Ownership is held only while the app is
 * running; the contents are lost on exit unless a clipboard manager saves
 * them (standard X11 behavior). */
char *lud_clipboard_get_text(void);
int   lud_clipboard_set_text(const char *utf8);

/* Asynchronous, typed clipboard read.  Does not block.
 *
 * Requests `format` (e.g. LUD_CLIPBOARD_TEXT) and calls cb once the data
 * arrives, from inside event processing on this or a later frame.  On
 * failure or timeout cb is still invoked, with data==NULL and len==0.  The
 * data buffer is owned by ludica and freed after cb returns, so copy
 * anything you need to keep.  Only one request may be in flight at a time; a
 * request made while another is pending fails immediately (cb called with
 * data==NULL).
 *
 * The `format` axis exists so non-text targets (images, file lists) can be
 * added later without changing this signature. */
typedef void (*lud_clipboard_cb)(const char *format, void *data,
				 size_t len, void *user);
void lud_clipboard_get_async(const char *format, lud_clipboard_cb cb, void *user);

/* Synchronous typed clipboard for arbitrary binary data (images, and any
 * other target named by a format string).
 *
 * lud_clipboard_set_data() copies len bytes onto the clipboard under `format`,
 * replacing any previous contents.  It returns 0 on success, non-zero on
 * failure.  As with text, ownership is held only while the app runs.
 *
 * lud_clipboard_get_data() blocks briefly for the owner to answer and returns
 * a malloc'd buffer the caller must free(), writing its length to *len_out
 * (which may be NULL).  The buffer is NUL-terminated for convenience, but the
 * terminator is not counted in *len_out.  Returns NULL when the clipboard is
 * empty, holds no data in that format, or the owner does not respond in time.
 *
 * Large payloads transfer automatically through the INCR protocol on X11. */
int   lud_clipboard_set_data(const char *format, const void *data, size_t len);
void *lud_clipboard_get_data(const char *format, size_t *len_out);

/* File lists, a convenience layer over LUD_CLIPBOARD_URI_LIST.
 *
 * lud_clipboard_set_files() places `count` file paths on the clipboard,
 * percent-encoding each into a file:// URI.  Paths should be absolute.
 * Returns 0 on success, non-zero on failure.
 *
 * lud_clipboard_get_files() blocks briefly and returns a NULL-terminated array
 * of malloc'd absolute path strings, decoded from the file:// URIs on the
 * clipboard.  The caller must free() each string and then the array.  Returns
 * NULL when the clipboard holds no file list. */
int    lud_clipboard_set_files(const char *const *paths, int count);
char **lud_clipboard_get_files(void);

#endif /* LUDICA_INPUT_H_ */
