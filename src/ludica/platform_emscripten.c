/* platform_emscripten.c : Emscripten/WebGL platform backend for ludica */

#include "ludica_internal.h"

#include <stdio.h>
#include <string.h>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

/****************************************************************
 * WebGL state
 ****************************************************************/

static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE gl_context;

/****************************************************************
 * Key translation — HTML5 KeyboardEvent.code to lud_keycode
 ****************************************************************/

struct keymap_entry {
	const char *code;
	enum lud_keycode key;
};

static const struct keymap_entry keymap[] = {
	{ "Space",         LUD_KEY_SPACE },
	{ "Quote",         LUD_KEY_APOSTROPHE },
	{ "Comma",         LUD_KEY_COMMA },
	{ "Minus",         LUD_KEY_MINUS },
	{ "Period",        LUD_KEY_PERIOD },
	{ "Slash",         LUD_KEY_SLASH },
	{ "Digit0",        LUD_KEY_0 },
	{ "Digit1",        LUD_KEY_1 },
	{ "Digit2",        LUD_KEY_2 },
	{ "Digit3",        LUD_KEY_3 },
	{ "Digit4",        LUD_KEY_4 },
	{ "Digit5",        LUD_KEY_5 },
	{ "Digit6",        LUD_KEY_6 },
	{ "Digit7",        LUD_KEY_7 },
	{ "Digit8",        LUD_KEY_8 },
	{ "Digit9",        LUD_KEY_9 },
	{ "Semicolon",     LUD_KEY_SEMICOLON },
	{ "Equal",         LUD_KEY_EQUAL },
	{ "KeyA",          LUD_KEY_A },
	{ "KeyB",          LUD_KEY_B },
	{ "KeyC",          LUD_KEY_C },
	{ "KeyD",          LUD_KEY_D },
	{ "KeyE",          LUD_KEY_E },
	{ "KeyF",          LUD_KEY_F },
	{ "KeyG",          LUD_KEY_G },
	{ "KeyH",          LUD_KEY_H },
	{ "KeyI",          LUD_KEY_I },
	{ "KeyJ",          LUD_KEY_J },
	{ "KeyK",          LUD_KEY_K },
	{ "KeyL",          LUD_KEY_L },
	{ "KeyM",          LUD_KEY_M },
	{ "KeyN",          LUD_KEY_N },
	{ "KeyO",          LUD_KEY_O },
	{ "KeyP",          LUD_KEY_P },
	{ "KeyQ",          LUD_KEY_Q },
	{ "KeyR",          LUD_KEY_R },
	{ "KeyS",          LUD_KEY_S },
	{ "KeyT",          LUD_KEY_T },
	{ "KeyU",          LUD_KEY_U },
	{ "KeyV",          LUD_KEY_V },
	{ "KeyW",          LUD_KEY_W },
	{ "KeyX",          LUD_KEY_X },
	{ "KeyY",          LUD_KEY_Y },
	{ "KeyZ",          LUD_KEY_Z },
	{ "BracketLeft",   LUD_KEY_LEFT_BRACKET },
	{ "Backslash",     LUD_KEY_BACKSLASH },
	{ "BracketRight",  LUD_KEY_RIGHT_BRACKET },
	{ "Backquote",     LUD_KEY_GRAVE_ACCENT },
	{ "Escape",        LUD_KEY_ESCAPE },
	{ "Enter",         LUD_KEY_ENTER },
	{ "Tab",           LUD_KEY_TAB },
	{ "Backspace",     LUD_KEY_BACKSPACE },
	{ "Insert",        LUD_KEY_INSERT },
	{ "Delete",        LUD_KEY_DELETE },
	{ "ArrowRight",    LUD_KEY_RIGHT },
	{ "ArrowLeft",     LUD_KEY_LEFT },
	{ "ArrowDown",     LUD_KEY_DOWN },
	{ "ArrowUp",       LUD_KEY_UP },
	{ "PageUp",        LUD_KEY_PAGE_UP },
	{ "PageDown",      LUD_KEY_PAGE_DOWN },
	{ "Home",          LUD_KEY_HOME },
	{ "End",           LUD_KEY_END },
	{ "CapsLock",      LUD_KEY_CAPS_LOCK },
	{ "ScrollLock",    LUD_KEY_SCROLL_LOCK },
	{ "NumLock",       LUD_KEY_NUM_LOCK },
	{ "PrintScreen",   LUD_KEY_PRINT_SCREEN },
	{ "Pause",         LUD_KEY_PAUSE },
	{ "F1",            LUD_KEY_F1 },
	{ "F2",            LUD_KEY_F2 },
	{ "F3",            LUD_KEY_F3 },
	{ "F4",            LUD_KEY_F4 },
	{ "F5",            LUD_KEY_F5 },
	{ "F6",            LUD_KEY_F6 },
	{ "F7",            LUD_KEY_F7 },
	{ "F8",            LUD_KEY_F8 },
	{ "F9",            LUD_KEY_F9 },
	{ "F10",           LUD_KEY_F10 },
	{ "F11",           LUD_KEY_F11 },
	{ "F12",           LUD_KEY_F12 },
	{ "Numpad0",       LUD_KEY_KP_0 },
	{ "Numpad1",       LUD_KEY_KP_1 },
	{ "Numpad2",       LUD_KEY_KP_2 },
	{ "Numpad3",       LUD_KEY_KP_3 },
	{ "Numpad4",       LUD_KEY_KP_4 },
	{ "Numpad5",       LUD_KEY_KP_5 },
	{ "Numpad6",       LUD_KEY_KP_6 },
	{ "Numpad7",       LUD_KEY_KP_7 },
	{ "Numpad8",       LUD_KEY_KP_8 },
	{ "Numpad9",       LUD_KEY_KP_9 },
	{ "NumpadDecimal",  LUD_KEY_KP_DECIMAL },
	{ "NumpadDivide",   LUD_KEY_KP_DIVIDE },
	{ "NumpadMultiply", LUD_KEY_KP_MULTIPLY },
	{ "NumpadSubtract", LUD_KEY_KP_SUBTRACT },
	{ "NumpadAdd",      LUD_KEY_KP_ADD },
	{ "NumpadEnter",    LUD_KEY_KP_ENTER },
	{ "NumpadEqual",    LUD_KEY_KP_EQUAL },
	{ "ShiftLeft",     LUD_KEY_LEFT_SHIFT },
	{ "ControlLeft",   LUD_KEY_LEFT_CONTROL },
	{ "AltLeft",       LUD_KEY_LEFT_ALT },
	{ "MetaLeft",      LUD_KEY_LEFT_SUPER },
	{ "ShiftRight",    LUD_KEY_RIGHT_SHIFT },
	{ "ControlRight",  LUD_KEY_RIGHT_CONTROL },
	{ "AltRight",      LUD_KEY_RIGHT_ALT },
	{ "MetaRight",     LUD_KEY_RIGHT_SUPER },
	{ "ContextMenu",   LUD_KEY_MENU },
};

#define KEYMAP_COUNT (sizeof(keymap) / sizeof(keymap[0]))

static enum lud_keycode
translate_code(const char *code)
{
	for (unsigned i = 0; i < KEYMAP_COUNT; i++) {
		if (strcmp(code, keymap[i].code) == 0)
			return keymap[i].key;
	}
	return LUD_KEY_UNKNOWN;
}

static unsigned
translate_modifiers(const EmscriptenKeyboardEvent *e)
{
	unsigned mods = 0;
	if (e->shiftKey) mods |= LUD_MOD_SHIFT;
	if (e->ctrlKey)  mods |= LUD_MOD_CTRL;
	if (e->altKey)   mods |= LUD_MOD_ALT;
	if (e->metaKey)  mods |= LUD_MOD_SUPER;
	return mods;
}

static unsigned
translate_mouse_modifiers(const EmscriptenMouseEvent *e)
{
	unsigned mods = 0;
	if (e->shiftKey) mods |= LUD_MOD_SHIFT;
	if (e->ctrlKey)  mods |= LUD_MOD_CTRL;
	if (e->altKey)   mods |= LUD_MOD_ALT;
	if (e->metaKey)  mods |= LUD_MOD_SUPER;
	return mods;
}

/****************************************************************
 * HTML5 event callbacks
 ****************************************************************/

/** Keys that should be captured by the canvas to prevent browser
 *  default behavior (scrolling, tab switching, etc). */
static int
should_prevent_default(enum lud_keycode kc)
{
	switch (kc) {
	case LUD_KEY_TAB:
	case LUD_KEY_SPACE:
	case LUD_KEY_UP:
	case LUD_KEY_DOWN:
	case LUD_KEY_LEFT:
	case LUD_KEY_RIGHT:
	case LUD_KEY_BACKSPACE:
	case LUD_KEY_F1:
	case LUD_KEY_F2:
	case LUD_KEY_F3:
	case LUD_KEY_F4:
	case LUD_KEY_F5:
	case LUD_KEY_F6:
	case LUD_KEY_F7:
	case LUD_KEY_F8:
	case LUD_KEY_F9:
	case LUD_KEY_F10:
	case LUD_KEY_F11:
	case LUD_KEY_F12:
		return 1;
	default:
		return 0;
	}
}

static EM_BOOL
on_key_down(int type, const EmscriptenKeyboardEvent *e, void *ud)
{
	(void)type;
	(void)ud;

	enum lud_keycode kc = translate_code(e->code);
	lud_event_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = LUD_EV_KEY_DOWN;
	ev.modifiers = translate_modifiers(e);
	ev.key.keycode = kc;
	ev.key.repeat = e->repeat;
	lud__event_push(&ev);

	/* Generate CHAR event for printable keys */
	if (e->charCode || e->key[0]) {
		unsigned cp = 0;
		/* Use the key string for single characters */
		const char *k = e->key;
		unsigned char c = (unsigned char)k[0];
		if (k[1] == '\0' && c >= 32) {
			cp = c;
		} else if (c >= 0xC0 && c < 0xE0 && k[2] == '\0') {
			cp = ((c & 0x1F) << 6) | ((unsigned char)k[1] & 0x3F);
		} else if (c >= 0xE0 && c < 0xF0 && k[3] == '\0') {
			cp = ((c & 0x0F) << 12) |
			     (((unsigned char)k[1] & 0x3F) << 6) |
			     ((unsigned char)k[2] & 0x3F);
		}
		if (cp >= 32 || cp == '\t' || cp == '\r' || cp == '\n') {
			lud_event_t cev;
			memset(&cev, 0, sizeof(cev));
			cev.type = LUD_EV_CHAR;
			cev.modifiers = translate_modifiers(e);
			cev.ch.codepoint = cp;
			lud__event_push(&cev);
		}
	}

	return should_prevent_default(kc);
}

static EM_BOOL
on_key_up(int type, const EmscriptenKeyboardEvent *e, void *ud)
{
	(void)type;
	(void)ud;

	enum lud_keycode kc = translate_code(e->code);
	lud_event_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = LUD_EV_KEY_UP;
	ev.modifiers = translate_modifiers(e);
	ev.key.keycode = kc;
	lud__event_push(&ev);

	return should_prevent_default(kc);
}

static EM_BOOL
on_mouse_move(int type, const EmscriptenMouseEvent *e, void *ud)
{
	(void)type;
	(void)ud;

	lud_event_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = LUD_EV_MOUSE_MOVE;
	ev.modifiers = translate_mouse_modifiers(e);
	ev.mouse_move.x = e->targetX;
	ev.mouse_move.y = e->targetY;
	lud__event_push(&ev);
	return EM_TRUE;
}

static enum lud_mouse_button
translate_button(unsigned short btn)
{
	switch (btn) {
	case 0:  return LUD_MOUSE_LEFT;
	case 1:  return LUD_MOUSE_MIDDLE;
	case 2:  return LUD_MOUSE_RIGHT;
	default: return LUD_MOUSE_LEFT;
	}
}

static EM_BOOL
on_mouse_down(int type, const EmscriptenMouseEvent *e, void *ud)
{
	(void)type;
	(void)ud;

	lud_event_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = LUD_EV_MOUSE_DOWN;
	ev.modifiers = translate_mouse_modifiers(e);
	ev.mouse_button.x = e->targetX;
	ev.mouse_button.y = e->targetY;
	ev.mouse_button.button = translate_button(e->button);
	lud__event_push(&ev);
	return EM_TRUE;
}

static EM_BOOL
on_mouse_up(int type, const EmscriptenMouseEvent *e, void *ud)
{
	(void)type;
	(void)ud;

	lud_event_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = LUD_EV_MOUSE_UP;
	ev.modifiers = translate_mouse_modifiers(e);
	ev.mouse_button.x = e->targetX;
	ev.mouse_button.y = e->targetY;
	ev.mouse_button.button = translate_button(e->button);
	lud__event_push(&ev);
	return EM_TRUE;
}

static EM_BOOL
on_wheel(int type, const EmscriptenWheelEvent *e, void *ud)
{
	(void)type;
	(void)ud;

	lud_event_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = LUD_EV_MOUSE_SCROLL;
	/* Normalize: DOM deltaY positive = scroll down, ludica dy positive = scroll up */
	ev.scroll.dx = (float)(-e->deltaX / 100.0);
	ev.scroll.dy = (float)(-e->deltaY / 100.0);
	lud__event_push(&ev);
	return EM_TRUE;
}

static EM_BOOL
on_focus(int type, const EmscriptenFocusEvent *e, void *ud)
{
	(void)type;
	(void)e;
	(void)ud;

	lud_event_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = LUD_EV_FOCUS;
	lud__event_push(&ev);
	return EM_TRUE;
}

static EM_BOOL
on_blur(int type, const EmscriptenFocusEvent *e, void *ud)
{
	(void)type;
	(void)e;
	(void)ud;

	lud_event_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = LUD_EV_UNFOCUS;
	lud__event_push(&ev);
	return EM_TRUE;
}

static EM_BOOL
on_resize(int type, const EmscriptenUiEvent *e, void *ud)
{
	(void)type;
	(void)e;
	(void)ud;

	int w, h;
	emscripten_get_canvas_element_size("#canvas", &w, &h);

	if (w != lud__state.win_width || h != lud__state.win_height) {
		lud_event_t ev;
		memset(&ev, 0, sizeof(ev));
		ev.type = LUD_EV_RESIZED;
		ev.resize.width = w;
		ev.resize.height = h;
		lud__event_push(&ev);
	}
	return EM_TRUE;
}

static EM_BOOL
on_fullscreen_change(int type, const EmscriptenFullscreenChangeEvent *e,
                     void *ud)
{
	(void)type;
	(void)ud;

	lud__state.is_fullscreen = e->isFullscreen;

	/* Update canvas size after fullscreen transition */
	int w, h;
	emscripten_get_canvas_element_size("#canvas", &w, &h);
	if (w != lud__state.win_width || h != lud__state.win_height) {
		lud_event_t ev;
		memset(&ev, 0, sizeof(ev));
		ev.type = LUD_EV_RESIZED;
		ev.resize.width = w;
		ev.resize.height = h;
		lud__event_push(&ev);
	}
	return EM_TRUE;
}

/****************************************************************
 * Platform interface
 ****************************************************************/

int
lud__platform_init(const lud_desc_t *desc)
{
	/* Set canvas size */
	emscripten_set_canvas_element_size("#canvas", desc->width, desc->height);

	/* Create WebGL context */
	EmscriptenWebGLContextAttributes attrs;
	emscripten_webgl_init_context_attributes(&attrs);
	attrs.alpha = EM_FALSE;
	attrs.depth = EM_TRUE;
	attrs.stencil = EM_TRUE;
	attrs.antialias = EM_FALSE;
	attrs.majorVersion = (desc->gles_version >= 3) ? 2 : 1;
	attrs.minorVersion = 0;

	gl_context = emscripten_webgl_create_context("#canvas", &attrs);
	if (gl_context <= 0) {
		fprintf(stderr, "ludica: failed to create WebGL context\n");
		return LUD_ERR;
	}
	EMSCRIPTEN_RESULT r = emscripten_webgl_make_context_current(gl_context);
	if (r != EMSCRIPTEN_RESULT_SUCCESS) {
		fprintf(stderr, "ludica: failed to make WebGL context current (%d)\n", r);
		return LUD_ERR;
	}

	/* Register HTML5 event callbacks on the canvas */
	const char *target = "#canvas";
	emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, EM_TRUE, on_key_down);
	emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, EM_TRUE, on_key_up);
	emscripten_set_mousemove_callback(target, NULL, EM_TRUE, on_mouse_move);
	emscripten_set_mousedown_callback(target, NULL, EM_TRUE, on_mouse_down);
	emscripten_set_mouseup_callback(target, NULL, EM_TRUE, on_mouse_up);
	emscripten_set_wheel_callback(target, NULL, EM_TRUE, on_wheel);
	emscripten_set_focusin_callback(target, NULL, EM_TRUE, on_focus);
	emscripten_set_focusout_callback(target, NULL, EM_TRUE, on_blur);
	emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, EM_TRUE, on_resize);
	emscripten_set_fullscreenchange_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, NULL, EM_TRUE, on_fullscreen_change);

	return LUD_OK;
}

void
lud__platform_shutdown(void)
{
	if (gl_context > 0) {
		emscripten_webgl_destroy_context(gl_context);
		gl_context = 0;
	}
}

void
lud__platform_poll_events(void)
{
	/* Events arrive asynchronously via HTML5 callbacks.
	 * Nothing to poll -- they are already in the event queue. */
}

void
lud__platform_swap(void)
{
	/* Emscripten auto-presents the backbuffer at the end of each
	 * requestAnimationFrame callback.  Nothing to do here. */
}

void
lud__platform_set_fullscreen(int fullscreen)
{
	if (fullscreen) {
		EmscriptenFullscreenStrategy strategy = {
			.scaleMode = EMSCRIPTEN_FULLSCREEN_SCALE_STRETCH,
			.canvasResolutionScaleMode = EMSCRIPTEN_FULLSCREEN_CANVAS_SCALE_HIDEF,
			.filteringMode = EMSCRIPTEN_FULLSCREEN_FILTERING_DEFAULT,
		};
		emscripten_enter_soft_fullscreen("#canvas", &strategy);
	} else {
		emscripten_exit_soft_fullscreen();
	}
}
