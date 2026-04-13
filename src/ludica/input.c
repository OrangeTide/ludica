#include "ludica_internal.h"
#include <string.h>
#include <strings.h> /* strcasecmp */

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

/* Polled input state */
static unsigned char key_state[LUD_KEY__COUNT];
static int mouse_x, mouse_y;
static unsigned char mouse_buttons[3];
static int gamepad_exists[LUD_GAMEPAD_MAX];
static float gamepad_axes[LUD_GAMEPAD_MAX][LUD_GAMEPAD_AXIS_MAX];
static unsigned char gamepad_buttons_state[LUD_GAMEPAD_MAX][LUD_GAMEPAD_BUTTON_MAX];

void
lud__input_init(void)
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
lud__input_update(const lud_event_t *ev)
{
	switch (ev->type) {
	case LUD_EV_KEY_DOWN:
		if (ev->key.keycode < LUD_KEY__COUNT)
			key_state[ev->key.keycode] = 1;
		break;
	case LUD_EV_KEY_UP:
		if (ev->key.keycode < LUD_KEY__COUNT)
			key_state[ev->key.keycode] = 0;
		break;
	case LUD_EV_MOUSE_MOVE:
		mouse_x = ev->mouse_move.x;
		mouse_y = ev->mouse_move.y;
		break;
	case LUD_EV_MOUSE_DOWN:
		if (ev->mouse_button.button <= LUD_MOUSE_MIDDLE)
			mouse_buttons[ev->mouse_button.button] = 1;
		mouse_x = ev->mouse_button.x;
		mouse_y = ev->mouse_button.y;
		break;
	case LUD_EV_MOUSE_UP:
		if (ev->mouse_button.button <= LUD_MOUSE_MIDDLE)
			mouse_buttons[ev->mouse_button.button] = 0;
		mouse_x = ev->mouse_button.x;
		mouse_y = ev->mouse_button.y;
		break;
	case LUD_EV_GAMEPAD_CONN:
		if (ev->gamepad_conn.id >= 0 && ev->gamepad_conn.id < LUD_GAMEPAD_MAX)
			gamepad_exists[ev->gamepad_conn.id] = 1;
		break;
	case LUD_EV_GAMEPAD_DISCONN:
		if (ev->gamepad_conn.id >= 0 && ev->gamepad_conn.id < LUD_GAMEPAD_MAX) {
			gamepad_exists[ev->gamepad_conn.id] = 0;
			memset(gamepad_axes[ev->gamepad_conn.id], 0, sizeof(gamepad_axes[0]));
			memset(gamepad_buttons_state[ev->gamepad_conn.id], 0, sizeof(gamepad_buttons_state[0]));
		}
		break;
	case LUD_EV_GAMEPAD_BUTTON:
		if (ev->gamepad_button.id >= 0 && ev->gamepad_button.id < LUD_GAMEPAD_MAX &&
		    ev->gamepad_button.button >= 0 && ev->gamepad_button.button < LUD_GAMEPAD_BUTTON_MAX)
			gamepad_buttons_state[ev->gamepad_button.id][ev->gamepad_button.button] = ev->gamepad_button.down;
		break;
	case LUD_EV_GAMEPAD_AXIS:
		if (ev->gamepad_axis.id >= 0 && ev->gamepad_axis.id < LUD_GAMEPAD_MAX &&
		    ev->gamepad_axis.axis >= 0 && ev->gamepad_axis.axis < LUD_GAMEPAD_AXIS_MAX)
			gamepad_axes[ev->gamepad_axis.id][ev->gamepad_axis.axis] = ev->gamepad_axis.value;
		break;
	default:
		break;
	}
}

/* Public polled state API */

int
lud_key_down(enum lud_keycode key)
{
	if (key < 0 || key >= LUD_KEY__COUNT)
		return 0;
	return key_state[key];
}

void
lud_mouse_pos(int *x, int *y)
{
	if (x) *x = mouse_x;
	if (y) *y = mouse_y;
}

int
lud_mouse_button_down(enum lud_mouse_button button)
{
	if (button < 0 || button > LUD_MOUSE_MIDDLE)
		return 0;
	return mouse_buttons[button];
}

float
lud_gamepad_axis(int id, int axis)
{
	if (id < 0 || id >= LUD_GAMEPAD_MAX)
		return 0.0f;
	if (axis < 0 || axis >= LUD_GAMEPAD_AXIS_MAX)
		return 0.0f;
	return gamepad_axes[id][axis];
}

int
lud_gamepad_button_down(int id, int button)
{
	if (id < 0 || id >= LUD_GAMEPAD_MAX)
		return 0;
	if (button < 0 || button >= LUD_GAMEPAD_BUTTON_MAX)
		return 0;
	return gamepad_buttons_state[id][button];
}

int
lud_gamepad_connected(int id)
{
	if (id < 0 || id >= LUD_GAMEPAD_MAX)
		return 0;
	return gamepad_exists[id];
}

/* ---- Named key lookup ---- */

struct key_name_entry {
	const char *name;
	enum lud_keycode code;
};

static const struct key_name_entry key_names[] = {
	{ "Space",        LUD_KEY_SPACE },
	{ "Apostrophe",   LUD_KEY_APOSTROPHE },
	{ "Comma",        LUD_KEY_COMMA },
	{ "Minus",        LUD_KEY_MINUS },
	{ "Period",       LUD_KEY_PERIOD },
	{ "Slash",        LUD_KEY_SLASH },
	{ "0", LUD_KEY_0 }, { "1", LUD_KEY_1 }, { "2", LUD_KEY_2 },
	{ "3", LUD_KEY_3 }, { "4", LUD_KEY_4 }, { "5", LUD_KEY_5 },
	{ "6", LUD_KEY_6 }, { "7", LUD_KEY_7 }, { "8", LUD_KEY_8 },
	{ "9", LUD_KEY_9 },
	{ "Semicolon",    LUD_KEY_SEMICOLON },
	{ "Equal",        LUD_KEY_EQUAL },
	{ "A", LUD_KEY_A }, { "B", LUD_KEY_B }, { "C", LUD_KEY_C },
	{ "D", LUD_KEY_D }, { "E", LUD_KEY_E }, { "F", LUD_KEY_F },
	{ "G", LUD_KEY_G }, { "H", LUD_KEY_H }, { "I", LUD_KEY_I },
	{ "J", LUD_KEY_J }, { "K", LUD_KEY_K }, { "L", LUD_KEY_L },
	{ "M", LUD_KEY_M }, { "N", LUD_KEY_N }, { "O", LUD_KEY_O },
	{ "P", LUD_KEY_P }, { "Q", LUD_KEY_Q }, { "R", LUD_KEY_R },
	{ "S", LUD_KEY_S }, { "T", LUD_KEY_T }, { "U", LUD_KEY_U },
	{ "V", LUD_KEY_V }, { "W", LUD_KEY_W }, { "X", LUD_KEY_X },
	{ "Y", LUD_KEY_Y }, { "Z", LUD_KEY_Z },
	{ "LeftBracket",  LUD_KEY_LEFT_BRACKET },
	{ "Backslash",    LUD_KEY_BACKSLASH },
	{ "RightBracket", LUD_KEY_RIGHT_BRACKET },
	{ "GraveAccent",  LUD_KEY_GRAVE_ACCENT },
	{ "Escape",       LUD_KEY_ESCAPE },
	{ "Enter",        LUD_KEY_ENTER },
	{ "Return",       LUD_KEY_ENTER },
	{ "Tab",          LUD_KEY_TAB },
	{ "Backspace",    LUD_KEY_BACKSPACE },
	{ "Insert",       LUD_KEY_INSERT },
	{ "Delete",       LUD_KEY_DELETE },
	{ "Right",        LUD_KEY_RIGHT },
	{ "Left",         LUD_KEY_LEFT },
	{ "Down",         LUD_KEY_DOWN },
	{ "Up",           LUD_KEY_UP },
	{ "PageUp",       LUD_KEY_PAGE_UP },
	{ "PageDown",     LUD_KEY_PAGE_DOWN },
	{ "Home",         LUD_KEY_HOME },
	{ "End",          LUD_KEY_END },
	{ "CapsLock",     LUD_KEY_CAPS_LOCK },
	{ "ScrollLock",   LUD_KEY_SCROLL_LOCK },
	{ "NumLock",      LUD_KEY_NUM_LOCK },
	{ "PrintScreen",  LUD_KEY_PRINT_SCREEN },
	{ "Pause",        LUD_KEY_PAUSE },
	{ "F1",  LUD_KEY_F1 },  { "F2",  LUD_KEY_F2 },
	{ "F3",  LUD_KEY_F3 },  { "F4",  LUD_KEY_F4 },
	{ "F5",  LUD_KEY_F5 },  { "F6",  LUD_KEY_F6 },
	{ "F7",  LUD_KEY_F7 },  { "F8",  LUD_KEY_F8 },
	{ "F9",  LUD_KEY_F9 },  { "F10", LUD_KEY_F10 },
	{ "F11", LUD_KEY_F11 }, { "F12", LUD_KEY_F12 },
	{ "KP0", LUD_KEY_KP_0 }, { "KP1", LUD_KEY_KP_1 },
	{ "KP2", LUD_KEY_KP_2 }, { "KP3", LUD_KEY_KP_3 },
	{ "KP4", LUD_KEY_KP_4 }, { "KP5", LUD_KEY_KP_5 },
	{ "KP6", LUD_KEY_KP_6 }, { "KP7", LUD_KEY_KP_7 },
	{ "KP8", LUD_KEY_KP_8 }, { "KP9", LUD_KEY_KP_9 },
	{ "KPDecimal",  LUD_KEY_KP_DECIMAL },
	{ "KPDivide",   LUD_KEY_KP_DIVIDE },
	{ "KPMultiply", LUD_KEY_KP_MULTIPLY },
	{ "KPSubtract", LUD_KEY_KP_SUBTRACT },
	{ "KPAdd",      LUD_KEY_KP_ADD },
	{ "KPEnter",    LUD_KEY_KP_ENTER },
	{ "KPEqual",    LUD_KEY_KP_EQUAL },
	{ "LeftShift",    LUD_KEY_LEFT_SHIFT },
	{ "LeftControl",  LUD_KEY_LEFT_CONTROL },
	{ "LeftAlt",      LUD_KEY_LEFT_ALT },
	{ "LeftSuper",    LUD_KEY_LEFT_SUPER },
	{ "RightShift",   LUD_KEY_RIGHT_SHIFT },
	{ "RightControl", LUD_KEY_RIGHT_CONTROL },
	{ "RightAlt",     LUD_KEY_RIGHT_ALT },
	{ "RightSuper",   LUD_KEY_RIGHT_SUPER },
	{ "Menu",         LUD_KEY_MENU },
	{ NULL, LUD_KEY_UNKNOWN }
};

enum lud_keycode
lud_key_from_name(const char *name)
{
	if (!name)
		return LUD_KEY_UNKNOWN;

	/* Single printable character — match directly to ASCII keycode */
	if (name[0] && !name[1]) {
		char c = name[0];
		if (c >= 'a' && c <= 'z')
			return (enum lud_keycode)(LUD_KEY_A + (c - 'a'));
		if (c >= 'A' && c <= 'Z')
			return (enum lud_keycode)(LUD_KEY_A + (c - 'A'));
		if (c >= '0' && c <= '9')
			return (enum lud_keycode)(LUD_KEY_0 + (c - '0'));
		if (c == ' ')
			return LUD_KEY_SPACE;
	}

	/* Table lookup (case-insensitive) */
	for (const struct key_name_entry *e = key_names; e->name; e++) {
		if (!strcasecmp(name, e->name))
			return e->code;
	}

	return LUD_KEY_UNKNOWN;
}
