#ifndef LITHOS_INPUT_H_
#define LITHOS_INPUT_H_

/* Platform-independent keycodes */
enum lithos_keycode {
	LITHOS_KEY_UNKNOWN = 0,

	/* Printable keys */
	LITHOS_KEY_SPACE = 32,
	LITHOS_KEY_APOSTROPHE = 39,
	LITHOS_KEY_COMMA = 44,
	LITHOS_KEY_MINUS = 45,
	LITHOS_KEY_PERIOD = 46,
	LITHOS_KEY_SLASH = 47,
	LITHOS_KEY_0 = 48,
	LITHOS_KEY_1, LITHOS_KEY_2, LITHOS_KEY_3, LITHOS_KEY_4,
	LITHOS_KEY_5, LITHOS_KEY_6, LITHOS_KEY_7, LITHOS_KEY_8, LITHOS_KEY_9,
	LITHOS_KEY_SEMICOLON = 59,
	LITHOS_KEY_EQUAL = 61,
	LITHOS_KEY_A = 65,
	LITHOS_KEY_B, LITHOS_KEY_C, LITHOS_KEY_D, LITHOS_KEY_E,
	LITHOS_KEY_F, LITHOS_KEY_G, LITHOS_KEY_H, LITHOS_KEY_I,
	LITHOS_KEY_J, LITHOS_KEY_K, LITHOS_KEY_L, LITHOS_KEY_M,
	LITHOS_KEY_N, LITHOS_KEY_O, LITHOS_KEY_P, LITHOS_KEY_Q,
	LITHOS_KEY_R, LITHOS_KEY_S, LITHOS_KEY_T, LITHOS_KEY_U,
	LITHOS_KEY_V, LITHOS_KEY_W, LITHOS_KEY_X, LITHOS_KEY_Y, LITHOS_KEY_Z,
	LITHOS_KEY_LEFT_BRACKET = 91,
	LITHOS_KEY_BACKSLASH = 92,
	LITHOS_KEY_RIGHT_BRACKET = 93,
	LITHOS_KEY_GRAVE_ACCENT = 96,

	/* Function keys */
	LITHOS_KEY_ESCAPE = 256,
	LITHOS_KEY_ENTER,
	LITHOS_KEY_TAB,
	LITHOS_KEY_BACKSPACE,
	LITHOS_KEY_INSERT,
	LITHOS_KEY_DELETE,
	LITHOS_KEY_RIGHT,
	LITHOS_KEY_LEFT,
	LITHOS_KEY_DOWN,
	LITHOS_KEY_UP,
	LITHOS_KEY_PAGE_UP,
	LITHOS_KEY_PAGE_DOWN,
	LITHOS_KEY_HOME,
	LITHOS_KEY_END,
	LITHOS_KEY_CAPS_LOCK = 280,
	LITHOS_KEY_SCROLL_LOCK,
	LITHOS_KEY_NUM_LOCK,
	LITHOS_KEY_PRINT_SCREEN,
	LITHOS_KEY_PAUSE,
	LITHOS_KEY_F1 = 290,
	LITHOS_KEY_F2, LITHOS_KEY_F3, LITHOS_KEY_F4, LITHOS_KEY_F5,
	LITHOS_KEY_F6, LITHOS_KEY_F7, LITHOS_KEY_F8, LITHOS_KEY_F9,
	LITHOS_KEY_F10, LITHOS_KEY_F11, LITHOS_KEY_F12,
	LITHOS_KEY_KP_0 = 320,
	LITHOS_KEY_KP_1, LITHOS_KEY_KP_2, LITHOS_KEY_KP_3, LITHOS_KEY_KP_4,
	LITHOS_KEY_KP_5, LITHOS_KEY_KP_6, LITHOS_KEY_KP_7, LITHOS_KEY_KP_8,
	LITHOS_KEY_KP_9,
	LITHOS_KEY_KP_DECIMAL,
	LITHOS_KEY_KP_DIVIDE,
	LITHOS_KEY_KP_MULTIPLY,
	LITHOS_KEY_KP_SUBTRACT,
	LITHOS_KEY_KP_ADD,
	LITHOS_KEY_KP_ENTER,
	LITHOS_KEY_KP_EQUAL,
	LITHOS_KEY_LEFT_SHIFT = 340,
	LITHOS_KEY_LEFT_CONTROL,
	LITHOS_KEY_LEFT_ALT,
	LITHOS_KEY_LEFT_SUPER,
	LITHOS_KEY_RIGHT_SHIFT,
	LITHOS_KEY_RIGHT_CONTROL,
	LITHOS_KEY_RIGHT_ALT,
	LITHOS_KEY_RIGHT_SUPER,
	LITHOS_KEY_MENU,

	LITHOS_KEY__COUNT
};

/* Mouse buttons */
enum lithos_mouse_button {
	LITHOS_MOUSE_LEFT = 0,
	LITHOS_MOUSE_RIGHT = 1,
	LITHOS_MOUSE_MIDDLE = 2,
};

/* Event types */
enum lithos_event_type {
	LITHOS_EV_NONE = 0,
	LITHOS_EV_KEY_DOWN,
	LITHOS_EV_KEY_UP,
	LITHOS_EV_CHAR,
	LITHOS_EV_MOUSE_MOVE,
	LITHOS_EV_MOUSE_DOWN,
	LITHOS_EV_MOUSE_UP,
	LITHOS_EV_MOUSE_SCROLL,
	LITHOS_EV_GAMEPAD_CONN,
	LITHOS_EV_GAMEPAD_DISCONN,
	LITHOS_EV_GAMEPAD_BUTTON,
	LITHOS_EV_GAMEPAD_AXIS,
	LITHOS_EV_RESIZED,
	LITHOS_EV_FOCUS,
	LITHOS_EV_UNFOCUS,
};

/* Modifier key bitmask */
enum lithos_modifier {
	LITHOS_MOD_SHIFT = 1,
	LITHOS_MOD_CTRL = 2,
	LITHOS_MOD_ALT = 4,
	LITHOS_MOD_SUPER = 8,
};

typedef struct lithos_event {
	enum lithos_event_type type;
	unsigned modifiers; /* bitmask of lithos_modifier */
	union {
		struct {
			enum lithos_keycode keycode;
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
			enum lithos_mouse_button button;
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
} lithos_event_t;

/* Polled input state queries */
int lithos_key_down(enum lithos_keycode key);
void lithos_mouse_pos(int *x, int *y);
int lithos_mouse_button_down(enum lithos_mouse_button button);
float lithos_gamepad_axis(int id, int axis);
int lithos_gamepad_button_down(int id, int button);
int lithos_gamepad_connected(int id);

/* Named key lookup (returns LITHOS_KEY_UNKNOWN on failure) */
enum lithos_keycode lithos_key_from_name(const char *name);

#endif /* LITHOS_INPUT_H_ */
