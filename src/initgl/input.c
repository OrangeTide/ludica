#include "initgl_internal.h"
#include <string.h>
#include <strings.h> /* strcasecmp */

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

/* Polled input state */
static unsigned char key_state[INITGL_KEY__COUNT];
static int mouse_x, mouse_y;
static unsigned char mouse_buttons[3];
static int gamepad_exists[INITGL_GAMEPAD_MAX];
static float gamepad_axes[INITGL_GAMEPAD_MAX][INITGL_GAMEPAD_AXIS_MAX];
static unsigned char gamepad_buttons_state[INITGL_GAMEPAD_MAX][INITGL_GAMEPAD_BUTTON_MAX];

void
initgl__input_init(void)
{
	memset(key_state, 0, sizeof(key_state));
	memset(mouse_buttons, 0, sizeof(mouse_buttons));
	memset(gamepad_exists, 0, sizeof(gamepad_exists));
	memset(gamepad_axes, 0, sizeof(gamepad_axes));
	memset(gamepad_buttons_state, 0, sizeof(gamepad_buttons_state));
	mouse_x = 0;
	mouse_y = 0;
}

/* Called by app.c after dispatching each event to the user callback,
 * to keep polled state in sync. */
void
initgl__input_update(const initgl_event_t *ev)
{
	switch (ev->type) {
	case INITGL_EV_KEY_DOWN:
		if (ev->key.keycode < INITGL_KEY__COUNT)
			key_state[ev->key.keycode] = 1;
		break;
	case INITGL_EV_KEY_UP:
		if (ev->key.keycode < INITGL_KEY__COUNT)
			key_state[ev->key.keycode] = 0;
		break;
	case INITGL_EV_MOUSE_MOVE:
		mouse_x = ev->mouse_move.x;
		mouse_y = ev->mouse_move.y;
		break;
	case INITGL_EV_MOUSE_DOWN:
		if (ev->mouse_button.button <= INITGL_MOUSE_MIDDLE)
			mouse_buttons[ev->mouse_button.button] = 1;
		mouse_x = ev->mouse_button.x;
		mouse_y = ev->mouse_button.y;
		break;
	case INITGL_EV_MOUSE_UP:
		if (ev->mouse_button.button <= INITGL_MOUSE_MIDDLE)
			mouse_buttons[ev->mouse_button.button] = 0;
		mouse_x = ev->mouse_button.x;
		mouse_y = ev->mouse_button.y;
		break;
	case INITGL_EV_GAMEPAD_CONN:
		if (ev->gamepad_conn.id >= 0 && ev->gamepad_conn.id < INITGL_GAMEPAD_MAX)
			gamepad_exists[ev->gamepad_conn.id] = 1;
		break;
	case INITGL_EV_GAMEPAD_DISCONN:
		if (ev->gamepad_conn.id >= 0 && ev->gamepad_conn.id < INITGL_GAMEPAD_MAX) {
			gamepad_exists[ev->gamepad_conn.id] = 0;
			memset(gamepad_axes[ev->gamepad_conn.id], 0, sizeof(gamepad_axes[0]));
			memset(gamepad_buttons_state[ev->gamepad_conn.id], 0, sizeof(gamepad_buttons_state[0]));
		}
		break;
	case INITGL_EV_GAMEPAD_BUTTON:
		if (ev->gamepad_button.id >= 0 && ev->gamepad_button.id < INITGL_GAMEPAD_MAX &&
		    ev->gamepad_button.button >= 0 && ev->gamepad_button.button < INITGL_GAMEPAD_BUTTON_MAX)
			gamepad_buttons_state[ev->gamepad_button.id][ev->gamepad_button.button] = ev->gamepad_button.down;
		break;
	case INITGL_EV_GAMEPAD_AXIS:
		if (ev->gamepad_axis.id >= 0 && ev->gamepad_axis.id < INITGL_GAMEPAD_MAX &&
		    ev->gamepad_axis.axis >= 0 && ev->gamepad_axis.axis < INITGL_GAMEPAD_AXIS_MAX)
			gamepad_axes[ev->gamepad_axis.id][ev->gamepad_axis.axis] = ev->gamepad_axis.value;
		break;
	default:
		break;
	}
}

/* Public polled state API */

int
initgl_key_down(enum initgl_keycode key)
{
	if (key < 0 || key >= INITGL_KEY__COUNT)
		return 0;
	return key_state[key];
}

void
initgl_mouse_pos(int *x, int *y)
{
	if (x) *x = mouse_x;
	if (y) *y = mouse_y;
}

int
initgl_mouse_button_down(enum initgl_mouse_button button)
{
	if (button < 0 || button > INITGL_MOUSE_MIDDLE)
		return 0;
	return mouse_buttons[button];
}

float
initgl_gamepad_axis(int id, int axis)
{
	if (id < 0 || id >= INITGL_GAMEPAD_MAX)
		return 0.0f;
	if (axis < 0 || axis >= INITGL_GAMEPAD_AXIS_MAX)
		return 0.0f;
	return gamepad_axes[id][axis];
}

int
initgl_gamepad_button_down(int id, int button)
{
	if (id < 0 || id >= INITGL_GAMEPAD_MAX)
		return 0;
	if (button < 0 || button >= INITGL_GAMEPAD_BUTTON_MAX)
		return 0;
	return gamepad_buttons_state[id][button];
}

int
initgl_gamepad_connected(int id)
{
	if (id < 0 || id >= INITGL_GAMEPAD_MAX)
		return 0;
	return gamepad_exists[id];
}

/* ---- Named key lookup ---- */

struct key_name_entry {
	const char *name;
	enum initgl_keycode code;
};

static const struct key_name_entry key_names[] = {
	{ "Space",        INITGL_KEY_SPACE },
	{ "Apostrophe",   INITGL_KEY_APOSTROPHE },
	{ "Comma",        INITGL_KEY_COMMA },
	{ "Minus",        INITGL_KEY_MINUS },
	{ "Period",       INITGL_KEY_PERIOD },
	{ "Slash",        INITGL_KEY_SLASH },
	{ "0", INITGL_KEY_0 }, { "1", INITGL_KEY_1 }, { "2", INITGL_KEY_2 },
	{ "3", INITGL_KEY_3 }, { "4", INITGL_KEY_4 }, { "5", INITGL_KEY_5 },
	{ "6", INITGL_KEY_6 }, { "7", INITGL_KEY_7 }, { "8", INITGL_KEY_8 },
	{ "9", INITGL_KEY_9 },
	{ "Semicolon",    INITGL_KEY_SEMICOLON },
	{ "Equal",        INITGL_KEY_EQUAL },
	{ "A", INITGL_KEY_A }, { "B", INITGL_KEY_B }, { "C", INITGL_KEY_C },
	{ "D", INITGL_KEY_D }, { "E", INITGL_KEY_E }, { "F", INITGL_KEY_F },
	{ "G", INITGL_KEY_G }, { "H", INITGL_KEY_H }, { "I", INITGL_KEY_I },
	{ "J", INITGL_KEY_J }, { "K", INITGL_KEY_K }, { "L", INITGL_KEY_L },
	{ "M", INITGL_KEY_M }, { "N", INITGL_KEY_N }, { "O", INITGL_KEY_O },
	{ "P", INITGL_KEY_P }, { "Q", INITGL_KEY_Q }, { "R", INITGL_KEY_R },
	{ "S", INITGL_KEY_S }, { "T", INITGL_KEY_T }, { "U", INITGL_KEY_U },
	{ "V", INITGL_KEY_V }, { "W", INITGL_KEY_W }, { "X", INITGL_KEY_X },
	{ "Y", INITGL_KEY_Y }, { "Z", INITGL_KEY_Z },
	{ "LeftBracket",  INITGL_KEY_LEFT_BRACKET },
	{ "Backslash",    INITGL_KEY_BACKSLASH },
	{ "RightBracket", INITGL_KEY_RIGHT_BRACKET },
	{ "GraveAccent",  INITGL_KEY_GRAVE_ACCENT },
	{ "Escape",       INITGL_KEY_ESCAPE },
	{ "Enter",        INITGL_KEY_ENTER },
	{ "Return",       INITGL_KEY_ENTER },
	{ "Tab",          INITGL_KEY_TAB },
	{ "Backspace",    INITGL_KEY_BACKSPACE },
	{ "Insert",       INITGL_KEY_INSERT },
	{ "Delete",       INITGL_KEY_DELETE },
	{ "Right",        INITGL_KEY_RIGHT },
	{ "Left",         INITGL_KEY_LEFT },
	{ "Down",         INITGL_KEY_DOWN },
	{ "Up",           INITGL_KEY_UP },
	{ "PageUp",       INITGL_KEY_PAGE_UP },
	{ "PageDown",     INITGL_KEY_PAGE_DOWN },
	{ "Home",         INITGL_KEY_HOME },
	{ "End",          INITGL_KEY_END },
	{ "CapsLock",     INITGL_KEY_CAPS_LOCK },
	{ "ScrollLock",   INITGL_KEY_SCROLL_LOCK },
	{ "NumLock",      INITGL_KEY_NUM_LOCK },
	{ "PrintScreen",  INITGL_KEY_PRINT_SCREEN },
	{ "Pause",        INITGL_KEY_PAUSE },
	{ "F1",  INITGL_KEY_F1 },  { "F2",  INITGL_KEY_F2 },
	{ "F3",  INITGL_KEY_F3 },  { "F4",  INITGL_KEY_F4 },
	{ "F5",  INITGL_KEY_F5 },  { "F6",  INITGL_KEY_F6 },
	{ "F7",  INITGL_KEY_F7 },  { "F8",  INITGL_KEY_F8 },
	{ "F9",  INITGL_KEY_F9 },  { "F10", INITGL_KEY_F10 },
	{ "F11", INITGL_KEY_F11 }, { "F12", INITGL_KEY_F12 },
	{ "KP0", INITGL_KEY_KP_0 }, { "KP1", INITGL_KEY_KP_1 },
	{ "KP2", INITGL_KEY_KP_2 }, { "KP3", INITGL_KEY_KP_3 },
	{ "KP4", INITGL_KEY_KP_4 }, { "KP5", INITGL_KEY_KP_5 },
	{ "KP6", INITGL_KEY_KP_6 }, { "KP7", INITGL_KEY_KP_7 },
	{ "KP8", INITGL_KEY_KP_8 }, { "KP9", INITGL_KEY_KP_9 },
	{ "KPDecimal",  INITGL_KEY_KP_DECIMAL },
	{ "KPDivide",   INITGL_KEY_KP_DIVIDE },
	{ "KPMultiply", INITGL_KEY_KP_MULTIPLY },
	{ "KPSubtract", INITGL_KEY_KP_SUBTRACT },
	{ "KPAdd",      INITGL_KEY_KP_ADD },
	{ "KPEnter",    INITGL_KEY_KP_ENTER },
	{ "KPEqual",    INITGL_KEY_KP_EQUAL },
	{ "LeftShift",    INITGL_KEY_LEFT_SHIFT },
	{ "LeftControl",  INITGL_KEY_LEFT_CONTROL },
	{ "LeftAlt",      INITGL_KEY_LEFT_ALT },
	{ "LeftSuper",    INITGL_KEY_LEFT_SUPER },
	{ "RightShift",   INITGL_KEY_RIGHT_SHIFT },
	{ "RightControl", INITGL_KEY_RIGHT_CONTROL },
	{ "RightAlt",     INITGL_KEY_RIGHT_ALT },
	{ "RightSuper",   INITGL_KEY_RIGHT_SUPER },
	{ "Menu",         INITGL_KEY_MENU },
	{ NULL, INITGL_KEY_UNKNOWN }
};

enum initgl_keycode
initgl_key_from_name(const char *name)
{
	if (!name)
		return INITGL_KEY_UNKNOWN;

	/* Single printable character — match directly to ASCII keycode */
	if (name[0] && !name[1]) {
		char c = name[0];
		if (c >= 'a' && c <= 'z')
			return (enum initgl_keycode)(INITGL_KEY_A + (c - 'a'));
		if (c >= 'A' && c <= 'Z')
			return (enum initgl_keycode)(INITGL_KEY_A + (c - 'A'));
		if (c >= '0' && c <= '9')
			return (enum initgl_keycode)(INITGL_KEY_0 + (c - '0'));
		if (c == ' ')
			return INITGL_KEY_SPACE;
	}

	/* Table lookup (case-insensitive) */
	for (const struct key_name_entry *e = key_names; e->name; e++) {
		if (!strcasecmp(name, e->name))
			return e->code;
	}

	return INITGL_KEY_UNKNOWN;
}
