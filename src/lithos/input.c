#include "lithos_internal.h"
#include <string.h>
#include <strings.h> /* strcasecmp */

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

/* Polled input state */
static unsigned char key_state[LITHOS_KEY__COUNT];
static int mouse_x, mouse_y;
static unsigned char mouse_buttons[3];
static int gamepad_exists[LITHOS_GAMEPAD_MAX];
static float gamepad_axes[LITHOS_GAMEPAD_MAX][LITHOS_GAMEPAD_AXIS_MAX];
static unsigned char gamepad_buttons_state[LITHOS_GAMEPAD_MAX][LITHOS_GAMEPAD_BUTTON_MAX];

void
lithos__input_init(void)
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
lithos__input_update(const lithos_event_t *ev)
{
	switch (ev->type) {
	case LITHOS_EV_KEY_DOWN:
		if (ev->key.keycode < LITHOS_KEY__COUNT)
			key_state[ev->key.keycode] = 1;
		break;
	case LITHOS_EV_KEY_UP:
		if (ev->key.keycode < LITHOS_KEY__COUNT)
			key_state[ev->key.keycode] = 0;
		break;
	case LITHOS_EV_MOUSE_MOVE:
		mouse_x = ev->mouse_move.x;
		mouse_y = ev->mouse_move.y;
		break;
	case LITHOS_EV_MOUSE_DOWN:
		if (ev->mouse_button.button <= LITHOS_MOUSE_MIDDLE)
			mouse_buttons[ev->mouse_button.button] = 1;
		mouse_x = ev->mouse_button.x;
		mouse_y = ev->mouse_button.y;
		break;
	case LITHOS_EV_MOUSE_UP:
		if (ev->mouse_button.button <= LITHOS_MOUSE_MIDDLE)
			mouse_buttons[ev->mouse_button.button] = 0;
		mouse_x = ev->mouse_button.x;
		mouse_y = ev->mouse_button.y;
		break;
	case LITHOS_EV_GAMEPAD_CONN:
		if (ev->gamepad_conn.id >= 0 && ev->gamepad_conn.id < LITHOS_GAMEPAD_MAX)
			gamepad_exists[ev->gamepad_conn.id] = 1;
		break;
	case LITHOS_EV_GAMEPAD_DISCONN:
		if (ev->gamepad_conn.id >= 0 && ev->gamepad_conn.id < LITHOS_GAMEPAD_MAX) {
			gamepad_exists[ev->gamepad_conn.id] = 0;
			memset(gamepad_axes[ev->gamepad_conn.id], 0, sizeof(gamepad_axes[0]));
			memset(gamepad_buttons_state[ev->gamepad_conn.id], 0, sizeof(gamepad_buttons_state[0]));
		}
		break;
	case LITHOS_EV_GAMEPAD_BUTTON:
		if (ev->gamepad_button.id >= 0 && ev->gamepad_button.id < LITHOS_GAMEPAD_MAX &&
		    ev->gamepad_button.button >= 0 && ev->gamepad_button.button < LITHOS_GAMEPAD_BUTTON_MAX)
			gamepad_buttons_state[ev->gamepad_button.id][ev->gamepad_button.button] = ev->gamepad_button.down;
		break;
	case LITHOS_EV_GAMEPAD_AXIS:
		if (ev->gamepad_axis.id >= 0 && ev->gamepad_axis.id < LITHOS_GAMEPAD_MAX &&
		    ev->gamepad_axis.axis >= 0 && ev->gamepad_axis.axis < LITHOS_GAMEPAD_AXIS_MAX)
			gamepad_axes[ev->gamepad_axis.id][ev->gamepad_axis.axis] = ev->gamepad_axis.value;
		break;
	default:
		break;
	}
}

/* Public polled state API */

int
lithos_key_down(enum lithos_keycode key)
{
	if (key < 0 || key >= LITHOS_KEY__COUNT)
		return 0;
	return key_state[key];
}

void
lithos_mouse_pos(int *x, int *y)
{
	if (x) *x = mouse_x;
	if (y) *y = mouse_y;
}

int
lithos_mouse_button_down(enum lithos_mouse_button button)
{
	if (button < 0 || button > LITHOS_MOUSE_MIDDLE)
		return 0;
	return mouse_buttons[button];
}

float
lithos_gamepad_axis(int id, int axis)
{
	if (id < 0 || id >= LITHOS_GAMEPAD_MAX)
		return 0.0f;
	if (axis < 0 || axis >= LITHOS_GAMEPAD_AXIS_MAX)
		return 0.0f;
	return gamepad_axes[id][axis];
}

int
lithos_gamepad_button_down(int id, int button)
{
	if (id < 0 || id >= LITHOS_GAMEPAD_MAX)
		return 0;
	if (button < 0 || button >= LITHOS_GAMEPAD_BUTTON_MAX)
		return 0;
	return gamepad_buttons_state[id][button];
}

int
lithos_gamepad_connected(int id)
{
	if (id < 0 || id >= LITHOS_GAMEPAD_MAX)
		return 0;
	return gamepad_exists[id];
}

/* ---- Named key lookup ---- */

struct key_name_entry {
	const char *name;
	enum lithos_keycode code;
};

static const struct key_name_entry key_names[] = {
	{ "Space",        LITHOS_KEY_SPACE },
	{ "Apostrophe",   LITHOS_KEY_APOSTROPHE },
	{ "Comma",        LITHOS_KEY_COMMA },
	{ "Minus",        LITHOS_KEY_MINUS },
	{ "Period",       LITHOS_KEY_PERIOD },
	{ "Slash",        LITHOS_KEY_SLASH },
	{ "0", LITHOS_KEY_0 }, { "1", LITHOS_KEY_1 }, { "2", LITHOS_KEY_2 },
	{ "3", LITHOS_KEY_3 }, { "4", LITHOS_KEY_4 }, { "5", LITHOS_KEY_5 },
	{ "6", LITHOS_KEY_6 }, { "7", LITHOS_KEY_7 }, { "8", LITHOS_KEY_8 },
	{ "9", LITHOS_KEY_9 },
	{ "Semicolon",    LITHOS_KEY_SEMICOLON },
	{ "Equal",        LITHOS_KEY_EQUAL },
	{ "A", LITHOS_KEY_A }, { "B", LITHOS_KEY_B }, { "C", LITHOS_KEY_C },
	{ "D", LITHOS_KEY_D }, { "E", LITHOS_KEY_E }, { "F", LITHOS_KEY_F },
	{ "G", LITHOS_KEY_G }, { "H", LITHOS_KEY_H }, { "I", LITHOS_KEY_I },
	{ "J", LITHOS_KEY_J }, { "K", LITHOS_KEY_K }, { "L", LITHOS_KEY_L },
	{ "M", LITHOS_KEY_M }, { "N", LITHOS_KEY_N }, { "O", LITHOS_KEY_O },
	{ "P", LITHOS_KEY_P }, { "Q", LITHOS_KEY_Q }, { "R", LITHOS_KEY_R },
	{ "S", LITHOS_KEY_S }, { "T", LITHOS_KEY_T }, { "U", LITHOS_KEY_U },
	{ "V", LITHOS_KEY_V }, { "W", LITHOS_KEY_W }, { "X", LITHOS_KEY_X },
	{ "Y", LITHOS_KEY_Y }, { "Z", LITHOS_KEY_Z },
	{ "LeftBracket",  LITHOS_KEY_LEFT_BRACKET },
	{ "Backslash",    LITHOS_KEY_BACKSLASH },
	{ "RightBracket", LITHOS_KEY_RIGHT_BRACKET },
	{ "GraveAccent",  LITHOS_KEY_GRAVE_ACCENT },
	{ "Escape",       LITHOS_KEY_ESCAPE },
	{ "Enter",        LITHOS_KEY_ENTER },
	{ "Return",       LITHOS_KEY_ENTER },
	{ "Tab",          LITHOS_KEY_TAB },
	{ "Backspace",    LITHOS_KEY_BACKSPACE },
	{ "Insert",       LITHOS_KEY_INSERT },
	{ "Delete",       LITHOS_KEY_DELETE },
	{ "Right",        LITHOS_KEY_RIGHT },
	{ "Left",         LITHOS_KEY_LEFT },
	{ "Down",         LITHOS_KEY_DOWN },
	{ "Up",           LITHOS_KEY_UP },
	{ "PageUp",       LITHOS_KEY_PAGE_UP },
	{ "PageDown",     LITHOS_KEY_PAGE_DOWN },
	{ "Home",         LITHOS_KEY_HOME },
	{ "End",          LITHOS_KEY_END },
	{ "CapsLock",     LITHOS_KEY_CAPS_LOCK },
	{ "ScrollLock",   LITHOS_KEY_SCROLL_LOCK },
	{ "NumLock",      LITHOS_KEY_NUM_LOCK },
	{ "PrintScreen",  LITHOS_KEY_PRINT_SCREEN },
	{ "Pause",        LITHOS_KEY_PAUSE },
	{ "F1",  LITHOS_KEY_F1 },  { "F2",  LITHOS_KEY_F2 },
	{ "F3",  LITHOS_KEY_F3 },  { "F4",  LITHOS_KEY_F4 },
	{ "F5",  LITHOS_KEY_F5 },  { "F6",  LITHOS_KEY_F6 },
	{ "F7",  LITHOS_KEY_F7 },  { "F8",  LITHOS_KEY_F8 },
	{ "F9",  LITHOS_KEY_F9 },  { "F10", LITHOS_KEY_F10 },
	{ "F11", LITHOS_KEY_F11 }, { "F12", LITHOS_KEY_F12 },
	{ "KP0", LITHOS_KEY_KP_0 }, { "KP1", LITHOS_KEY_KP_1 },
	{ "KP2", LITHOS_KEY_KP_2 }, { "KP3", LITHOS_KEY_KP_3 },
	{ "KP4", LITHOS_KEY_KP_4 }, { "KP5", LITHOS_KEY_KP_5 },
	{ "KP6", LITHOS_KEY_KP_6 }, { "KP7", LITHOS_KEY_KP_7 },
	{ "KP8", LITHOS_KEY_KP_8 }, { "KP9", LITHOS_KEY_KP_9 },
	{ "KPDecimal",  LITHOS_KEY_KP_DECIMAL },
	{ "KPDivide",   LITHOS_KEY_KP_DIVIDE },
	{ "KPMultiply", LITHOS_KEY_KP_MULTIPLY },
	{ "KPSubtract", LITHOS_KEY_KP_SUBTRACT },
	{ "KPAdd",      LITHOS_KEY_KP_ADD },
	{ "KPEnter",    LITHOS_KEY_KP_ENTER },
	{ "KPEqual",    LITHOS_KEY_KP_EQUAL },
	{ "LeftShift",    LITHOS_KEY_LEFT_SHIFT },
	{ "LeftControl",  LITHOS_KEY_LEFT_CONTROL },
	{ "LeftAlt",      LITHOS_KEY_LEFT_ALT },
	{ "LeftSuper",    LITHOS_KEY_LEFT_SUPER },
	{ "RightShift",   LITHOS_KEY_RIGHT_SHIFT },
	{ "RightControl", LITHOS_KEY_RIGHT_CONTROL },
	{ "RightAlt",     LITHOS_KEY_RIGHT_ALT },
	{ "RightSuper",   LITHOS_KEY_RIGHT_SUPER },
	{ "Menu",         LITHOS_KEY_MENU },
	{ NULL, LITHOS_KEY_UNKNOWN }
};

enum lithos_keycode
lithos_key_from_name(const char *name)
{
	if (!name)
		return LITHOS_KEY_UNKNOWN;

	/* Single printable character — match directly to ASCII keycode */
	if (name[0] && !name[1]) {
		char c = name[0];
		if (c >= 'a' && c <= 'z')
			return (enum lithos_keycode)(LITHOS_KEY_A + (c - 'a'));
		if (c >= 'A' && c <= 'Z')
			return (enum lithos_keycode)(LITHOS_KEY_A + (c - 'A'));
		if (c >= '0' && c <= '9')
			return (enum lithos_keycode)(LITHOS_KEY_0 + (c - '0'));
		if (c == ' ')
			return LITHOS_KEY_SPACE;
	}

	/* Table lookup (case-insensitive) */
	for (const struct key_name_entry *e = key_names; e->name; e++) {
		if (!strcasecmp(name, e->name))
			return e->code;
	}

	return LITHOS_KEY_UNKNOWN;
}
