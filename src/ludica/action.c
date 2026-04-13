/*
 * action.c — Named action bindings for ludica
 *
 * Actions decouple game logic from physical keys. Multiple keys (or
 * gamepad buttons) can be bound to the same named action. The game
 * loop polls actions with lud_action_down/pressed/released instead
 * of checking raw keycodes.
 */

#include "ludica_internal.h"
#include <string.h>
#include <strings.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

/* ---- Limits ---- */

#define MAX_ACTIONS       64
#define MAX_NAME          32
#define MAX_KEY_BINDINGS  4
#define MAX_BTN_BINDINGS  4

/* ---- Per-action state ---- */

struct lud__action {
	char name[MAX_NAME];
	/* key bindings */
	enum lud_keycode keys[MAX_KEY_BINDINGS];
	int num_keys;
	/* gamepad button bindings */
	struct { int pad; int button; } buttons[MAX_BTN_BINDINGS];
	int num_buttons;
	/* frame state: two bits for edge detection */
	unsigned char current;
	unsigned char previous;
	/* automation: virtual press state */
	unsigned char virtual_down;
	unsigned char auto_release;
};

static struct lud__action actions[MAX_ACTIONS];
static int num_actions;

/* ---- Internal helpers ---- */

static struct lud__action *
find_action(unsigned id)
{
	if (id == 0 || id > (unsigned)num_actions)
		return NULL;
	return &actions[id - 1];
}

static struct lud__action *
find_by_name(const char *name)
{
	int i;
	for (i = 0; i < num_actions; i++) {
		if (!strcasecmp(actions[i].name, name))
			return &actions[i];
	}
	return NULL;
}

/* Called from app.c at the start of each frame */
void
lud__action_update(void)
{
	int i;
	for (i = 0; i < num_actions; i++) {
		struct lud__action *a = &actions[i];
		int down = 0;
		int k;

		/* check bound keys */
		for (k = 0; k < a->num_keys; k++) {
			if (lud_key_down(a->keys[k])) {
				down = 1;
				break;
			}
		}

		/* check bound gamepad buttons */
		if (!down) {
			for (k = 0; k < a->num_buttons; k++) {
				if (lud_gamepad_button_down(a->buttons[k].pad,
				                            a->buttons[k].button)) {
					down = 1;
					break;
				}
			}
		}

		/* include virtual press from automation */
		if (a->virtual_down)
			down = 1;

		a->previous = a->current;
		a->current = (unsigned char)down;
	}
}

/* Called from app.c on shutdown */
void
lud__action_reset(void)
{
	memset(actions, 0, sizeof(actions));
	num_actions = 0;
}

/* ---- Public API ---- */

lud_action_t
lud_make_action(const char *name)
{
	struct lud__action *a;

	/* return existing action if name matches */
	a = find_by_name(name);
	if (a)
		return (lud_action_t){ (unsigned)(a - actions) + 1 };

	if (num_actions >= MAX_ACTIONS)
		return (lud_action_t){ 0 };

	a = &actions[num_actions++];
	memset(a, 0, sizeof(*a));
	strncpy(a->name, name, MAX_NAME - 1);

	return (lud_action_t){ (unsigned)(a - actions) + 1 };
}

lud_action_t
lud_find_action(const char *name)
{
	struct lud__action *a = find_by_name(name);
	if (!a)
		return (lud_action_t){ 0 };
	return (lud_action_t){ (unsigned)(a - actions) + 1 };
}

void
lud_bind_key(enum lud_keycode key, lud_action_t action)
{
	struct lud__action *a = find_action(action.id);
	if (!a || a->num_keys >= MAX_KEY_BINDINGS)
		return;

	/* avoid duplicate bindings */
	int i;
	for (i = 0; i < a->num_keys; i++) {
		if (a->keys[i] == key)
			return;
	}

	a->keys[a->num_keys++] = key;
}

void
lud_bind_gamepad_button(int pad, int button, lud_action_t action)
{
	struct lud__action *a = find_action(action.id);
	if (!a || a->num_buttons >= MAX_BTN_BINDINGS)
		return;

	/* avoid duplicate bindings */
	int i;
	for (i = 0; i < a->num_buttons; i++) {
		if (a->buttons[i].pad == pad && a->buttons[i].button == button)
			return;
	}

	a->buttons[a->num_buttons].pad = pad;
	a->buttons[a->num_buttons].button = button;
	a->num_buttons++;
}

void
lud_unbind_action(lud_action_t action)
{
	struct lud__action *a = find_action(action.id);
	if (!a)
		return;
	a->num_keys = 0;
	a->num_buttons = 0;
}

int
lud_action_down(lud_action_t action)
{
	struct lud__action *a = find_action(action.id);
	return a ? a->current : 0;
}

int
lud_action_pressed(lud_action_t action)
{
	struct lud__action *a = find_action(action.id);
	return a ? (a->current && !a->previous) : 0;
}

int
lud_action_released(lud_action_t action)
{
	struct lud__action *a = find_action(action.id);
	return a ? (!a->current && a->previous) : 0;
}

/* ---- Internal API for automation ---- */

int
lud__action_count(void)
{
	return num_actions;
}

const char *
lud__action_name(int index)
{
	if (index < 0 || index >= num_actions)
		return NULL;
	return actions[index].name;
}

int
lud__action_key_count(int index)
{
	if (index < 0 || index >= num_actions)
		return 0;
	return actions[index].num_keys;
}

enum lud_keycode
lud__action_key(int index, int ki)
{
	if (index < 0 || index >= num_actions)
		return LUD_KEY_UNKNOWN;
	if (ki < 0 || ki >= actions[index].num_keys)
		return LUD_KEY_UNKNOWN;
	return actions[index].keys[ki];
}

int
lud__action_inject(const char *name, int mode)
{
	struct lud__action *a = find_by_name(name);
	if (!a)
		return -1;
	switch (mode) {
	case 0: /* press + auto-release next frame */
		a->virtual_down = 1;
		a->auto_release = 1;
		break;
	case 1: /* hold */
		a->virtual_down = 1;
		a->auto_release = 0;
		break;
	case 2: /* release */
		a->virtual_down = 0;
		a->auto_release = 0;
		break;
	}
	return 0;
}

void
lud__action_auto_release(void)
{
	int i;
	for (i = 0; i < num_actions; i++) {
		if (actions[i].auto_release) {
			actions[i].virtual_down = 0;
			actions[i].auto_release = 0;
		}
	}
}
