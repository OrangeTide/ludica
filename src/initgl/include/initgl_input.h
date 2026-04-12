#ifndef INITGL_INPUT_H_
#define INITGL_INPUT_H_

/* Platform-independent keycodes */
enum initgl_keycode {
	INITGL_KEY_UNKNOWN = 0,

	/* Printable keys */
	INITGL_KEY_SPACE = 32,
	INITGL_KEY_APOSTROPHE = 39,
	INITGL_KEY_COMMA = 44,
	INITGL_KEY_MINUS = 45,
	INITGL_KEY_PERIOD = 46,
	INITGL_KEY_SLASH = 47,
	INITGL_KEY_0 = 48,
	INITGL_KEY_1, INITGL_KEY_2, INITGL_KEY_3, INITGL_KEY_4,
	INITGL_KEY_5, INITGL_KEY_6, INITGL_KEY_7, INITGL_KEY_8, INITGL_KEY_9,
	INITGL_KEY_SEMICOLON = 59,
	INITGL_KEY_EQUAL = 61,
	INITGL_KEY_A = 65,
	INITGL_KEY_B, INITGL_KEY_C, INITGL_KEY_D, INITGL_KEY_E,
	INITGL_KEY_F, INITGL_KEY_G, INITGL_KEY_H, INITGL_KEY_I,
	INITGL_KEY_J, INITGL_KEY_K, INITGL_KEY_L, INITGL_KEY_M,
	INITGL_KEY_N, INITGL_KEY_O, INITGL_KEY_P, INITGL_KEY_Q,
	INITGL_KEY_R, INITGL_KEY_S, INITGL_KEY_T, INITGL_KEY_U,
	INITGL_KEY_V, INITGL_KEY_W, INITGL_KEY_X, INITGL_KEY_Y, INITGL_KEY_Z,
	INITGL_KEY_LEFT_BRACKET = 91,
	INITGL_KEY_BACKSLASH = 92,
	INITGL_KEY_RIGHT_BRACKET = 93,
	INITGL_KEY_GRAVE_ACCENT = 96,

	/* Function keys */
	INITGL_KEY_ESCAPE = 256,
	INITGL_KEY_ENTER,
	INITGL_KEY_TAB,
	INITGL_KEY_BACKSPACE,
	INITGL_KEY_INSERT,
	INITGL_KEY_DELETE,
	INITGL_KEY_RIGHT,
	INITGL_KEY_LEFT,
	INITGL_KEY_DOWN,
	INITGL_KEY_UP,
	INITGL_KEY_PAGE_UP,
	INITGL_KEY_PAGE_DOWN,
	INITGL_KEY_HOME,
	INITGL_KEY_END,
	INITGL_KEY_CAPS_LOCK = 280,
	INITGL_KEY_SCROLL_LOCK,
	INITGL_KEY_NUM_LOCK,
	INITGL_KEY_PRINT_SCREEN,
	INITGL_KEY_PAUSE,
	INITGL_KEY_F1 = 290,
	INITGL_KEY_F2, INITGL_KEY_F3, INITGL_KEY_F4, INITGL_KEY_F5,
	INITGL_KEY_F6, INITGL_KEY_F7, INITGL_KEY_F8, INITGL_KEY_F9,
	INITGL_KEY_F10, INITGL_KEY_F11, INITGL_KEY_F12,
	INITGL_KEY_KP_0 = 320,
	INITGL_KEY_KP_1, INITGL_KEY_KP_2, INITGL_KEY_KP_3, INITGL_KEY_KP_4,
	INITGL_KEY_KP_5, INITGL_KEY_KP_6, INITGL_KEY_KP_7, INITGL_KEY_KP_8,
	INITGL_KEY_KP_9,
	INITGL_KEY_KP_DECIMAL,
	INITGL_KEY_KP_DIVIDE,
	INITGL_KEY_KP_MULTIPLY,
	INITGL_KEY_KP_SUBTRACT,
	INITGL_KEY_KP_ADD,
	INITGL_KEY_KP_ENTER,
	INITGL_KEY_KP_EQUAL,
	INITGL_KEY_LEFT_SHIFT = 340,
	INITGL_KEY_LEFT_CONTROL,
	INITGL_KEY_LEFT_ALT,
	INITGL_KEY_LEFT_SUPER,
	INITGL_KEY_RIGHT_SHIFT,
	INITGL_KEY_RIGHT_CONTROL,
	INITGL_KEY_RIGHT_ALT,
	INITGL_KEY_RIGHT_SUPER,
	INITGL_KEY_MENU,

	INITGL_KEY__COUNT
};

/* Mouse buttons */
enum initgl_mouse_button {
	INITGL_MOUSE_LEFT = 0,
	INITGL_MOUSE_RIGHT = 1,
	INITGL_MOUSE_MIDDLE = 2,
};

/* Event types */
enum initgl_event_type {
	INITGL_EV_NONE = 0,
	INITGL_EV_KEY_DOWN,
	INITGL_EV_KEY_UP,
	INITGL_EV_CHAR,
	INITGL_EV_MOUSE_MOVE,
	INITGL_EV_MOUSE_DOWN,
	INITGL_EV_MOUSE_UP,
	INITGL_EV_MOUSE_SCROLL,
	INITGL_EV_GAMEPAD_CONN,
	INITGL_EV_GAMEPAD_DISCONN,
	INITGL_EV_GAMEPAD_BUTTON,
	INITGL_EV_GAMEPAD_AXIS,
	INITGL_EV_RESIZED,
	INITGL_EV_FOCUS,
	INITGL_EV_UNFOCUS,
};

/* Modifier key bitmask */
enum initgl_modifier {
	INITGL_MOD_SHIFT = 1,
	INITGL_MOD_CTRL = 2,
	INITGL_MOD_ALT = 4,
	INITGL_MOD_SUPER = 8,
};

typedef struct initgl_event {
	enum initgl_event_type type;
	unsigned modifiers; /* bitmask of initgl_modifier */
	union {
		struct {
			enum initgl_keycode keycode;
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
			enum initgl_mouse_button button;
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
} initgl_event_t;

/* Polled input state queries */
int initgl_key_down(enum initgl_keycode key);
void initgl_mouse_pos(int *x, int *y);
int initgl_mouse_button_down(enum initgl_mouse_button button);
float initgl_gamepad_axis(int id, int axis);
int initgl_gamepad_button_down(int id, int button);
int initgl_gamepad_connected(int id);

/* Named key lookup (returns INITGL_KEY_UNKNOWN on failure) */
enum initgl_keycode initgl_key_from_name(const char *name);

#endif /* INITGL_INPUT_H_ */
