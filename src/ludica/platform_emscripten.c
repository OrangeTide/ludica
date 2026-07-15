/* platform_emscripten.c : Emscripten/WebGL platform backend for ludica */

#include "ludica_internal.h"

#include <stdio.h>
#include <stdlib.h>
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
 * Audio unlock — resume suspended AudioContexts on first gesture
 ****************************************************************/

static int audio_unlocked;

static void
try_unlock_audio(void)
{
	if (audio_unlocked)
		return;
	audio_unlocked = 1;
	EM_ASM({
		if (typeof miniaudio !== 'undefined' && miniaudio.devices) {
			for (var i = 0; i < miniaudio.devices.length; ++i) {
				var d = miniaudio.devices[i];
				if (d != null && d.webaudio != null &&
				    d.webaudio.state === 'suspended') {
					d.webaudio.resume();
				}
			}
		}
	});
}

/****************************************************************
 * HTML5 event callbacks
 ****************************************************************/

/** Keys that should be captured by the canvas to prevent browser
 *  default behavior (scrolling, tab switching, etc). */
static int
should_prevent_default(enum lud_keycode kc)
{
	if (lud__state.desc.capture_keyboard)
		return 1;

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

	try_unlock_audio();

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

	try_unlock_audio();

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
		lud_err("failed to create WebGL context");
		return LUD_ERR;
	}
	EMSCRIPTEN_RESULT r = emscripten_webgl_make_context_current(gl_context);
	if (r != EMSCRIPTEN_RESULT_SUCCESS) {
		lud_err("failed to make WebGL context current (%d)", r);
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

	/* Drag-and-drop drop target: listen for DOM drop events on the canvas and
	 * route them to the lud__drop_* trampolines below.  Files are read
	 * asynchronously (File.arrayBuffer), text is available synchronously. */
	EM_ASM({
		var c = Module['canvas'] || document.getElementById('canvas');
		if (!c)
			return;
		c.addEventListener('dragover', function (e) {
			e.preventDefault();
			if (e.dataTransfer)
				e.dataTransfer.dropEffect = 'copy';
		});
		c.addEventListener('drop', function (e) {
			e.preventDefault();
			var dt = e.dataTransfer;
			if (!dt)
				return;
			var r = c.getBoundingClientRect();
			var x = (e.clientX - r.left) | 0, y = (e.clientY - r.top) | 0;
			if (dt.files && dt.files.length) {
				var f = dt.files[0];
				f.arrayBuffer().then(function (buf) {
					var u8 = new Uint8Array(buf);
					Module.ccall('lud__drop_bytes', null,
						['string', 'array', 'number', 'number', 'number'],
						[f.type || 'application/octet-stream', u8, u8.length, x, y]);
				});
				return;
			}
			var html = dt.getData('text/html');
			if (html) {
				Module.ccall('lud__drop_text', null,
					['string', 'string', 'number', 'number'],
					['text/html', html, x, y]);
				return;
			}
			var txt = dt.getData('text/plain');
			if (txt) {
				Module.ccall('lud__drop_text', null,
					['string', 'string', 'number', 'number'],
					['text/plain', txt, x, y]);
			}
		});
	});

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

/* ---- Clipboard ----
 *
 * The browser clipboard (navigator.clipboard) is asynchronous and gated behind
 * a user gesture and a permission prompt, so it maps only partially onto this
 * API.  What works:
 *   - Writes (set_text/set_data/set_multi): dispatched to navigator.clipboard,
 *     which resolves in the background.  We return LUD_OK once dispatched; the
 *     browser may still reject it without a gesture.  The safelisted MIME types
 *     are text/plain, text/html and image/png.
 *   - Async reads (get_async): navigator.clipboard.readText / read map cleanly
 *     onto the callback, delivering when the promise resolves.
 * What cannot work: synchronous reads (get_text/get_data/get_files) -- the
 * browser has no synchronous clipboard read -- and set_files, because the
 * browser exposes no filesystem paths.  Those stay honest failures. */

/* One in-flight async read; its callback state lives here until JS resolves. */
static lud_clipboard_cb em_async_cb;
static void *em_async_user;
static const char *em_async_fmt;
static int em_async_active;

static void
em_async_finish(void *data, size_t len)
{
	lud_clipboard_cb cb = em_async_cb;
	void *user = em_async_user;
	const char *fmt = em_async_fmt;
	em_async_active = 0;
	em_async_cb = NULL;
	if (cb)
		cb(fmt, data, len, user);
}

/* Called from JS when navigator.clipboard.readText resolves (NULL on failure).
 * `utf8` is marshaled onto the stack by ccall and is valid for this call. */
EMSCRIPTEN_KEEPALIVE void
lud__clip_async_text(const char *utf8)
{
	em_async_finish((void *)utf8, utf8 ? strlen(utf8) : 0);
}

/* Called from JS when navigator.clipboard.read resolves with binary bytes. */
EMSCRIPTEN_KEEPALIVE void
lud__clip_async_bytes(unsigned char *data, int len)
{
	em_async_finish(data, data ? (size_t)len : 0);
}

char *
lud_clipboard_get_text(void)
{
	/* No synchronous clipboard read in the browser; use get_async. */
	return NULL;
}

void *
lud_clipboard_get_data(const char *format, size_t *len_out)
{
	(void)format;
	if (len_out)
		*len_out = 0;
	return NULL;
}

char **
lud_clipboard_get_files(void)
{
	return NULL;
}

void
lud_clipboard_get_async(const char *format, lud_clipboard_cb cb, void *user)
{
	if (!cb)
		return;
	if (em_async_active) { /* one request at a time */
		cb(format, NULL, 0, user);
		return;
	}
	em_async_active = 1;
	em_async_cb = cb;
	em_async_user = user;
	em_async_fmt = format;

	int is_text = !format || !strcmp(format, LUD_CLIPBOARD_TEXT) ||
		      !strcmp(format, "text/plain");
	if (is_text) {
		EM_ASM({
			if (!navigator.clipboard || !navigator.clipboard.readText) {
				Module.ccall('lud__clip_async_text', null, ['string'], [null]);
				return;
			}
			navigator.clipboard.readText().then(function (t) {
				Module.ccall('lud__clip_async_text', null, ['string'], [t]);
			}).catch(function () {
				Module.ccall('lud__clip_async_text', null, ['string'], [null]);
			});
		});
	} else {
		/* Binary: read the first clipboard item matching this MIME type. */
		EM_ASM({
			var mime = UTF8ToString($0);
			if (!navigator.clipboard || !navigator.clipboard.read) {
				Module.ccall('lud__clip_async_bytes', null, ['number', 'number'], [0, 0]);
				return;
			}
			navigator.clipboard.read().then(function (items) {
				for (var i = 0; i < items.length; i++) {
					if (items[i].types.indexOf(mime) >= 0)
						return items[i].getType(mime);
				}
				throw 0;
			}).then(function (blob) {
				return blob.arrayBuffer();
			}).then(function (buf) {
				var u8 = new Uint8Array(buf);
				Module.ccall('lud__clip_async_bytes', null,
					['array', 'number'], [u8, u8.length]);
			}).catch(function () {
				Module.ccall('lud__clip_async_bytes', null, ['number', 'number'], [0, 0]);
			});
		}, format);
	}
}

int
lud_clipboard_set_text(const char *utf8)
{
	if (!utf8)
		return LUD_ERR;
	EM_ASM({
		if (navigator.clipboard && navigator.clipboard.writeText)
			navigator.clipboard.writeText(UTF8ToString($0));
	}, utf8);
	return LUD_OK;
}

/* Map a ludica format to a browser-safelisted ClipboardItem MIME type, or NULL
 * if the browser will not accept it on the clipboard. */
static const char *
em_clip_mime(const char *format)
{
	if (!format || !strcmp(format, LUD_CLIPBOARD_TEXT))
		return "text/plain";
	if (!strcmp(format, LUD_CLIPBOARD_HTML))
		return "text/html";
	if (!strcmp(format, LUD_CLIPBOARD_PNG))
		return "image/png";
	return NULL;
}

int
lud_clipboard_set_data(const char *format, const void *data, size_t len)
{
	const char *mime = em_clip_mime(format);
	if (!mime || !data)
		return LUD_ERR;

	if (!strcmp(mime, "text/plain")) {
		/* writeText wants a string; decode the bytes as UTF-8. */
		EM_ASM({
			if (navigator.clipboard && navigator.clipboard.writeText) {
				var s = new TextDecoder('utf-8').decode(HEAPU8.subarray($0, $0 + $1));
				navigator.clipboard.writeText(s);
			}
		}, data, (int)len);
		return LUD_OK;
	}

	EM_ASM({
		if (!navigator.clipboard || !navigator.clipboard.write ||
		    typeof ClipboardItem === 'undefined')
			return;
		var mime = UTF8ToString($0);
		var bytes = HEAPU8.slice($1, $1 + $2);
		var item = {};
		item[mime] = new Blob([bytes], { type: mime });
		navigator.clipboard.write([new ClipboardItem(item)]);
	}, mime, data, (int)len);
	return LUD_OK;
}

int
lud_clipboard_set_multi(const lud_clip_item_t *items, int count)
{
	if (!items || count <= 0)
		return LUD_ERR;

	/* Accumulate the safelisted entries into one JS ClipboardItem. */
	EM_ASM({ Module.__lud_clip = {}; });
	int offered = 0;
	for (int i = 0; i < count; i++) {
		const char *mime = em_clip_mime(items[i].format);
		if (!mime || !items[i].data)
			continue;
		EM_ASM({
			var mime = UTF8ToString($0);
			var bytes = HEAPU8.slice($1, $1 + $2);
			Module.__lud_clip[mime] = new Blob([bytes], { type: mime });
		}, mime, items[i].data, (int)items[i].len);
		offered++;
	}
	if (!offered) {
		EM_ASM({ delete Module.__lud_clip; });
		return LUD_ERR;
	}
	EM_ASM({
		var item = Module.__lud_clip;
		delete Module.__lud_clip;
		if (navigator.clipboard && navigator.clipboard.write &&
		    typeof ClipboardItem !== 'undefined')
			navigator.clipboard.write([new ClipboardItem(item)]);
	});
	return LUD_OK;
}

int
lud_clipboard_set_files(const char *const *paths, int count)
{
	/* The browser exposes no filesystem paths to put on the clipboard. */
	(void)paths; (void)count;
	return LUD_ERR;
}

/* ---- Drag and drop ----
 *
 * Drop target: DOM drop events on the canvas (registered in platform_init)
 * deliver text, HTML and files here.  The delivered bytes and format string
 * are owned here and stay valid until the next drop, covering the frame in
 * which the app's LUD_EV_DROP handler runs.
 *
 * Drag source: a browser cannot start an HTML5 drag programmatically -- a drag
 * must originate from a real user gesture on a draggable element and populate
 * dataTransfer inside the dragstart handler.  So lud_drag_* stay failures. */

static void *em_drop_data;
static char *em_drop_format;

static void
em_drop_free(void)
{
	free(em_drop_data);
	em_drop_data = NULL;
	free(em_drop_format);
	em_drop_format = NULL;
}

static void
em_drop_push(const char *mime, const void *data, size_t len, int x, int y)
{
	em_drop_free();
	em_drop_data = malloc(len ? len : 1);
	em_drop_format = mime ? strdup(mime) : NULL;
	if (!em_drop_data || !em_drop_format) {
		em_drop_free();
		return;
	}
	if (len)
		memcpy(em_drop_data, data, len);

	lud_event_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = LUD_EV_DROP;
	ev.drop.format = em_drop_format;
	ev.drop.data = em_drop_data;
	ev.drop.len = len;
	ev.drop.x = x;
	ev.drop.y = y;
	lud__event_push(&ev);
}

/* JS drop trampolines. `mime` and the payload are marshaled by ccall and valid
 * for the call; em_drop_push copies what it keeps. */
EMSCRIPTEN_KEEPALIVE void
lud__drop_text(const char *mime, const char *text, int x, int y)
{
	em_drop_push(mime, text, text ? strlen(text) : 0, x, y);
}

EMSCRIPTEN_KEEPALIVE void
lud__drop_bytes(const char *mime, unsigned char *data, int len, int x, int y)
{
	em_drop_push(mime, data, len > 0 ? (size_t)len : 0, x, y);
}

int
lud_drag_data(const char *format, const void *data, size_t len)
{
	(void)format; (void)data; (void)len;
	return LUD_ERR;
}

int
lud_drag_files(const char *const *paths, int count)
{
	(void)paths; (void)count;
	return LUD_ERR;
}

int
lud_drag_multi(const lud_clip_item_t *items, int count)
{
	(void)items; (void)count;
	return LUD_ERR;
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
