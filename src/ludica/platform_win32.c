/*
 * platform_win32.c — Win32 + EGL (ANGLE) platform backend for ludica.
 *
 * Implements the platform interface from ludica_internal.h:
 *   lud__platform_init()
 *   lud__platform_shutdown()
 *   lud__platform_poll_events()
 *   lud__platform_swap()
 *   lud__platform_set_fullscreen()
 *
 * GL entry points come from an EGL implementation (ANGLE's libEGL /
 * libGLESv2), matching how platform_x11.c links EGL + GLES directly.
 * This is the Windows twin of platform_x11.c; the older win32/window.c
 * predates this interface and is no longer built.
 *
 * The window uses the wide (-W) Win32 API explicitly so the file does
 * not depend on the UNICODE compile macro. App text (titles, clipboard)
 * is UTF-8 and converted to UTF-16 at the boundary.
 *
 * Made by a machine. PUBLIC DOMAIN (CC0-1.0)
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "ludica_internal.h"

/* EGL 1.5 / EGL_KHR_create_context */
#ifndef EGL_OPENGL_ES3_BIT
#define EGL_OPENGL_ES3_BIT 0x00000040
#endif

#define LUD_WNDCLASS L"ludica_window"

/* ---- VK to lud_keycode translation ---- */

static enum lud_keycode
translate_vk(WPARAM vk)
{
	/* Letters: VK 'A'..'Z' share ASCII values with LUD_KEY_A.. */
	if (vk >= 'A' && vk <= 'Z')
		return (enum lud_keycode)(vk - 'A' + LUD_KEY_A);
	/* Digits: VK '0'..'9' share ASCII values with LUD_KEY_0.. */
	if (vk >= '0' && vk <= '9')
		return (enum lud_keycode)(vk - '0' + LUD_KEY_0);

	switch (vk) {
	case VK_SPACE:      return LUD_KEY_SPACE;
	case VK_ESCAPE:     return LUD_KEY_ESCAPE;
	case VK_RETURN:     return LUD_KEY_ENTER;
	case VK_TAB:        return LUD_KEY_TAB;
	case VK_BACK:       return LUD_KEY_BACKSPACE;
	case VK_INSERT:     return LUD_KEY_INSERT;
	case VK_DELETE:     return LUD_KEY_DELETE;
	case VK_RIGHT:      return LUD_KEY_RIGHT;
	case VK_LEFT:       return LUD_KEY_LEFT;
	case VK_DOWN:       return LUD_KEY_DOWN;
	case VK_UP:         return LUD_KEY_UP;
	case VK_PRIOR:      return LUD_KEY_PAGE_UP;
	case VK_NEXT:       return LUD_KEY_PAGE_DOWN;
	case VK_HOME:       return LUD_KEY_HOME;
	case VK_END:        return LUD_KEY_END;
	case VK_CAPITAL:    return LUD_KEY_CAPS_LOCK;
	case VK_SCROLL:     return LUD_KEY_SCROLL_LOCK;
	case VK_NUMLOCK:    return LUD_KEY_NUM_LOCK;
	case VK_SNAPSHOT:   return LUD_KEY_PRINT_SCREEN;
	case VK_PAUSE:      return LUD_KEY_PAUSE;
	case VK_F1:         return LUD_KEY_F1;
	case VK_F2:         return LUD_KEY_F2;
	case VK_F3:         return LUD_KEY_F3;
	case VK_F4:         return LUD_KEY_F4;
	case VK_F5:         return LUD_KEY_F5;
	case VK_F6:         return LUD_KEY_F6;
	case VK_F7:         return LUD_KEY_F7;
	case VK_F8:         return LUD_KEY_F8;
	case VK_F9:         return LUD_KEY_F9;
	case VK_F10:        return LUD_KEY_F10;
	case VK_F11:        return LUD_KEY_F11;
	case VK_F12:        return LUD_KEY_F12;
	case VK_NUMPAD0:    return LUD_KEY_KP_0;
	case VK_NUMPAD1:    return LUD_KEY_KP_1;
	case VK_NUMPAD2:    return LUD_KEY_KP_2;
	case VK_NUMPAD3:    return LUD_KEY_KP_3;
	case VK_NUMPAD4:    return LUD_KEY_KP_4;
	case VK_NUMPAD5:    return LUD_KEY_KP_5;
	case VK_NUMPAD6:    return LUD_KEY_KP_6;
	case VK_NUMPAD7:    return LUD_KEY_KP_7;
	case VK_NUMPAD8:    return LUD_KEY_KP_8;
	case VK_NUMPAD9:    return LUD_KEY_KP_9;
	case VK_DECIMAL:    return LUD_KEY_KP_DECIMAL;
	case VK_DIVIDE:     return LUD_KEY_KP_DIVIDE;
	case VK_MULTIPLY:   return LUD_KEY_KP_MULTIPLY;
	case VK_SUBTRACT:   return LUD_KEY_KP_SUBTRACT;
	case VK_ADD:        return LUD_KEY_KP_ADD;
	case VK_LSHIFT:     return LUD_KEY_LEFT_SHIFT;
	case VK_RSHIFT:     return LUD_KEY_RIGHT_SHIFT;
	case VK_LCONTROL:   return LUD_KEY_LEFT_CONTROL;
	case VK_RCONTROL:   return LUD_KEY_RIGHT_CONTROL;
	case VK_LMENU:      return LUD_KEY_LEFT_ALT;
	case VK_RMENU:      return LUD_KEY_RIGHT_ALT;
	case VK_LWIN:       return LUD_KEY_LEFT_SUPER;
	case VK_RWIN:       return LUD_KEY_RIGHT_SUPER;
	case VK_APPS:       return LUD_KEY_MENU;
	/* Generic modifier VKs (no left/right distinction from lParam) */
	case VK_SHIFT:      return LUD_KEY_LEFT_SHIFT;
	case VK_CONTROL:    return LUD_KEY_LEFT_CONTROL;
	case VK_MENU:       return LUD_KEY_LEFT_ALT;
	default:            return LUD_KEY_UNKNOWN;
	}
}

static unsigned
current_modifiers(void)
{
	unsigned mods = 0;
	if (GetKeyState(VK_SHIFT) & 0x8000)   mods |= LUD_MOD_SHIFT;
	if (GetKeyState(VK_CONTROL) & 0x8000) mods |= LUD_MOD_CTRL;
	if (GetKeyState(VK_MENU) & 0x8000)    mods |= LUD_MOD_ALT;
	if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000))
		mods |= LUD_MOD_SUPER;
	return mods;
}

/* ---- EGL config ---- */

static EGLint config_attribs[] = {
	EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, /* patched for GLES3 */
	EGL_RED_SIZE, 8,
	EGL_GREEN_SIZE, 8,
	EGL_BLUE_SIZE, 8,
	EGL_ALPHA_SIZE, 8,
	EGL_DEPTH_SIZE, 16,
	EGL_STENCIL_SIZE, 8,
	EGL_SAMPLE_BUFFERS, 0,
	EGL_SAMPLES, 0,
	EGL_NONE
};

/* Index of the EGL_RENDERABLE_TYPE value in config_attribs */
#define CONFIG_RENDERABLE_IDX 3

static EGLint gles_ctx_attribs[] = {
	EGL_CONTEXT_CLIENT_VERSION, 2, /* patched at init */
	EGL_NONE
};

/* ---- Platform state ---- */

static HINSTANCE   hinstance;
static HWND        hwnd;
static HDC         hdc;
static EGLDisplay  egl_display = EGL_NO_DISPLAY;
static EGLSurface  egl_surface = EGL_NO_SURFACE;
static EGLContext  egl_context = EGL_NO_CONTEXT;

static WCHAR       pending_high; /* buffered UTF-16 high surrogate, 0 if none */

/* Windowed geometry saved across a fullscreen toggle */
static LONG        saved_style;
static WINDOWPLACEMENT saved_place;

/* ---- Clipboard (CF_UNICODETEXT) ---- */

char *
lud_clipboard_get_text(void)
{
	char *out = NULL;

	if (!OpenClipboard(hwnd))
		return NULL;

	HANDLE h = GetClipboardData(CF_UNICODETEXT);
	if (h) {
		const WCHAR *wtext = GlobalLock(h);
		if (wtext) {
			int n = WideCharToMultiByte(CP_UTF8, 0, wtext, -1,
						    NULL, 0, NULL, NULL);
			if (n > 0) {
				out = malloc(n);
				if (out)
					WideCharToMultiByte(CP_UTF8, 0, wtext, -1,
							    out, n, NULL, NULL);
			}
			GlobalUnlock(h);
		}
	}

	CloseClipboard();
	return out;
}

int
lud_clipboard_set_text(const char *utf8)
{
	if (!utf8)
		utf8 = "";

	int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
	if (n <= 0)
		return LUD_ERR;

	HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, (size_t)n * sizeof(WCHAR));
	if (!hmem)
		return LUD_ERR;

	WCHAR *dst = GlobalLock(hmem);
	if (!dst) {
		GlobalFree(hmem);
		return LUD_ERR;
	}
	MultiByteToWideChar(CP_UTF8, 0, utf8, -1, dst, n);
	GlobalUnlock(hmem);

	if (!OpenClipboard(hwnd)) {
		GlobalFree(hmem);
		return LUD_ERR;
	}
	EmptyClipboard();
	if (!SetClipboardData(CF_UNICODETEXT, hmem)) {
		/* Still own hmem on failure. */
		CloseClipboard();
		GlobalFree(hmem);
		return LUD_ERR;
	}
	/* On success the clipboard owns hmem. */
	CloseClipboard();
	return LUD_OK;
}

void
lud_clipboard_get_async(const char *format, lud_clipboard_cb cb, void *user)
{
	if (!cb)
		return;
	/* Win32 clipboard reads are synchronous; share the sync path. */
	if (!format)
		format = LUD_CLIPBOARD_TEXT;
	char *text = lud_clipboard_get_text();
	cb(format, text, text ? strlen(text) : 0, user);
	free(text);
}

/* Non-text targets are not yet wired on Windows.  The natural mapping is
 * CF_HDROP (DROPFILES / DragQueryFileW) for files, CF_DIB or a registered
 * "PNG" format for images, and RegisterClipboardFormat for anything else.
 * These honest stubs keep the backend linking until that lands. */
int
lud_clipboard_set_data(const char *format, const void *data, size_t len)
{
	(void)format; (void)data; (void)len;
	return LUD_ERR;
}

void *
lud_clipboard_get_data(const char *format, size_t *len_out)
{
	(void)format;
	if (len_out)
		*len_out = 0;
	return NULL;
}

int
lud_clipboard_set_files(const char *const *paths, int count)
{
	(void)paths; (void)count;
	return LUD_ERR;
}

char **
lud_clipboard_get_files(void)
{
	return NULL;
}

/* ---- Event translation ---- */

static void
push_key(enum lud_event_type type, WPARAM vk, LPARAM lparam)
{
	lud_event_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = type;
	ev.modifiers = current_modifiers();
	ev.key.keycode = translate_vk(vk);
	/* lParam bit 30 is the previous key state: set means auto-repeat. */
	if (type == LUD_EV_KEY_DOWN)
		ev.key.repeat = (lparam & (1 << 30)) ? 1 : 0;
	lud__event_push(&ev);
}

static void
push_char(unsigned codepoint)
{
	/* Skip control characters except tab, enter, backspace (match X11). */
	if (codepoint < 32 && codepoint != '\t' && codepoint != '\r' &&
	    codepoint != '\n')
		return;
	if (codepoint == 8) /* backspace arrives as a control char here */
		return;

	lud_event_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = LUD_EV_CHAR;
	ev.modifiers = current_modifiers();
	ev.ch.codepoint = codepoint;
	lud__event_push(&ev);
}

static void
push_mouse_button(enum lud_event_type type, enum lud_mouse_button button,
		  LPARAM lparam)
{
	lud_event_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = type;
	ev.modifiers = current_modifiers();
	ev.mouse_button.x = (int)(short)LOWORD(lparam);
	ev.mouse_button.y = (int)(short)HIWORD(lparam);
	ev.mouse_button.button = button;
	lud__event_push(&ev);
}

static LRESULT CALLBACK
wnd_proc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	lud_event_t ev;

	switch (msg) {
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		push_key(LUD_EV_KEY_DOWN, wparam, lparam);
		return 0;

	case WM_KEYUP:
	case WM_SYSKEYUP:
		push_key(LUD_EV_KEY_UP, wparam, lparam);
		return 0;

	case WM_CHAR: {
		WCHAR unit = (WCHAR)wparam;
		if (unit >= 0xD800 && unit <= 0xDBFF) {
			pending_high = unit; /* wait for the low surrogate */
		} else if (unit >= 0xDC00 && unit <= 0xDFFF) {
			if (pending_high) {
				unsigned cp = 0x10000 +
					(((unsigned)pending_high - 0xD800) << 10) +
					((unsigned)unit - 0xDC00);
				push_char(cp);
				pending_high = 0;
			}
		} else {
			push_char((unsigned)unit);
		}
		return 0;
	}

	case WM_MOUSEMOVE:
		memset(&ev, 0, sizeof(ev));
		ev.type = LUD_EV_MOUSE_MOVE;
		ev.modifiers = current_modifiers();
		ev.mouse_move.x = (int)(short)LOWORD(lparam);
		ev.mouse_move.y = (int)(short)HIWORD(lparam);
		lud__event_push(&ev);
		return 0;

	case WM_LBUTTONDOWN: push_mouse_button(LUD_EV_MOUSE_DOWN, LUD_MOUSE_LEFT, lparam);   return 0;
	case WM_LBUTTONUP:   push_mouse_button(LUD_EV_MOUSE_UP,   LUD_MOUSE_LEFT, lparam);   return 0;
	case WM_RBUTTONDOWN: push_mouse_button(LUD_EV_MOUSE_DOWN, LUD_MOUSE_RIGHT, lparam);  return 0;
	case WM_RBUTTONUP:   push_mouse_button(LUD_EV_MOUSE_UP,   LUD_MOUSE_RIGHT, lparam);  return 0;
	case WM_MBUTTONDOWN: push_mouse_button(LUD_EV_MOUSE_DOWN, LUD_MOUSE_MIDDLE, lparam); return 0;
	case WM_MBUTTONUP:   push_mouse_button(LUD_EV_MOUSE_UP,   LUD_MOUSE_MIDDLE, lparam); return 0;

	case WM_MOUSEWHEEL:
		memset(&ev, 0, sizeof(ev));
		ev.type = LUD_EV_MOUSE_SCROLL;
		ev.modifiers = current_modifiers();
		ev.scroll.dy = (float)GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
		lud__event_push(&ev);
		return 0;

	case WM_MOUSEHWHEEL:
		memset(&ev, 0, sizeof(ev));
		ev.type = LUD_EV_MOUSE_SCROLL;
		ev.modifiers = current_modifiers();
		ev.scroll.dx = (float)GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
		lud__event_push(&ev);
		return 0;

	case WM_SIZE:
		/* Ignore minimize (0x0); report client-area changes. */
		if (wparam != SIZE_MINIMIZED) {
			int w = LOWORD(lparam), h = HIWORD(lparam);
			if (w > 0 && h > 0 &&
			    (w != lud__state.win_width || h != lud__state.win_height)) {
				memset(&ev, 0, sizeof(ev));
				ev.type = LUD_EV_RESIZED;
				ev.resize.width = w;
				ev.resize.height = h;
				lud__event_push(&ev);
			}
		}
		return 0;

	case WM_SETFOCUS:
		memset(&ev, 0, sizeof(ev));
		ev.type = LUD_EV_FOCUS;
		lud__event_push(&ev);
		return 0;

	case WM_KILLFOCUS:
		memset(&ev, 0, sizeof(ev));
		ev.type = LUD_EV_UNFOCUS;
		lud__event_push(&ev);
		return 0;

	case WM_CLOSE:
		lud_quit();
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProcW(wnd, msg, wparam, lparam);
}

/* ---- Window creation ---- */

static HWND
create_window(const lud_desc_t *desc)
{
	WNDCLASSEXW wc;
	memset(&wc, 0, sizeof(wc));
	wc.cbSize = sizeof(wc);
	wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wc.lpfnWndProc = wnd_proc;
	wc.hInstance = hinstance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.lpszClassName = LUD_WNDCLASS;
	RegisterClassExW(&wc); /* harmless if already registered */

	DWORD style = desc->resizable
		? WS_OVERLAPPEDWINDOW
		: (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);

	/* Grow the outer rect so the client area matches the requested size. */
	RECT r = { 0, 0, desc->width, desc->height };
	AdjustWindowRect(&r, style, FALSE);
	int outer_w = r.right - r.left;
	int outer_h = r.bottom - r.top;

	int sx = (GetSystemMetrics(SM_CXSCREEN) - outer_w) / 2;
	int sy = (GetSystemMetrics(SM_CYSCREEN) - outer_h) / 2;

	/* Title: UTF-8 -> UTF-16 */
	WCHAR wtitle[256];
	const char *name = desc->app_name ? desc->app_name : "ludica";
	if (MultiByteToWideChar(CP_UTF8, 0, name, -1, wtitle,
				sizeof(wtitle) / sizeof(wtitle[0])) == 0)
		wtitle[0] = 0;

	HWND w = CreateWindowExW(0, LUD_WNDCLASS, wtitle, style,
				 sx, sy, outer_w, outer_h,
				 NULL, NULL, hinstance, NULL);
	if (w) {
		ShowWindow(w, SW_SHOW);
		SetForegroundWindow(w);
		SetFocus(w);
	}
	return w;
}

/* ---- Implementation ---- */

int
lud__platform_init(const lud_desc_t *desc)
{
	hinstance = GetModuleHandleW(NULL);

	hwnd = create_window(desc);
	if (!hwnd) {
		lud_err("Failed to create window");
		return LUD_ERR;
	}
	hdc = GetDC(hwnd);

	/* Configure EGL for the requested GLES version. */
	gles_ctx_attribs[1] = desc->gles_version;
	config_attribs[CONFIG_RENDERABLE_IDX] =
		(desc->gles_version >= 3) ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_ES2_BIT;

	/* ANGLE accepts an HDC as the native display. */
	egl_display = eglGetDisplay((EGLNativeDisplayType)hdc);
	if (egl_display == EGL_NO_DISPLAY) {
		lud_err("No EGL display available");
		return LUD_ERR;
	}

	EGLint major, minor;
	if (!eglInitialize(egl_display, &major, &minor)) {
		lud_err("Failed to initialize EGL");
		egl_display = EGL_NO_DISPLAY;
		return LUD_ERR;
	}
	eglBindAPI(EGL_OPENGL_ES_API);

	lud_log("EGL %d.%d: %s (%s)", major, minor,
		eglQueryString(egl_display, EGL_VERSION),
		eglQueryString(egl_display, EGL_VENDOR));

	EGLConfig config;
	EGLint num_config;
	if (!eglChooseConfig(egl_display, config_attribs, &config, 1, &num_config) ||
	    num_config == 0) {
		lud_err("No suitable EGL config found");
		eglTerminate(egl_display);
		egl_display = EGL_NO_DISPLAY;
		return LUD_ERR;
	}

	egl_surface = eglCreateWindowSurface(egl_display, config,
					     (EGLNativeWindowType)hwnd, NULL);
	if (egl_surface == EGL_NO_SURFACE) {
		lud_err("Failed to create EGL surface");
		eglTerminate(egl_display);
		egl_display = EGL_NO_DISPLAY;
		return LUD_ERR;
	}

	egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT,
				       gles_ctx_attribs);
	if (egl_context == EGL_NO_CONTEXT) {
		lud_err("Failed to create EGL context");
		eglDestroySurface(egl_display, egl_surface);
		egl_surface = EGL_NO_SURFACE;
		eglTerminate(egl_display);
		egl_display = EGL_NO_DISPLAY;
		return LUD_ERR;
	}

	eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);

	lud_log("GL: %s (%s)", glGetString(GL_VERSION), glGetString(GL_VENDOR));

	return LUD_OK;
}

void
lud__platform_shutdown(void)
{
	if (egl_display != EGL_NO_DISPLAY) {
		eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
			       EGL_NO_CONTEXT);
		if (egl_context != EGL_NO_CONTEXT) {
			eglDestroyContext(egl_display, egl_context);
			egl_context = EGL_NO_CONTEXT;
		}
		if (egl_surface != EGL_NO_SURFACE) {
			eglDestroySurface(egl_display, egl_surface);
			egl_surface = EGL_NO_SURFACE;
		}
		eglTerminate(egl_display);
		egl_display = EGL_NO_DISPLAY;
	}

	if (hwnd) {
		if (hdc) {
			ReleaseDC(hwnd, hdc);
			hdc = NULL;
		}
		DestroyWindow(hwnd);
		hwnd = NULL;
	}
	UnregisterClassW(LUD_WNDCLASS, hinstance);
}

void
lud__platform_poll_events(void)
{
	MSG msg;
	while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_QUIT) {
			lud_quit();
			break;
		}
		TranslateMessage(&msg); /* generates WM_CHAR from WM_KEYDOWN */
		DispatchMessageW(&msg);
	}
}

void
lud__platform_set_fullscreen(int fullscreen)
{
	if (!hwnd)
		return;

	if (fullscreen) {
		/* Save windowed style and placement to restore later. */
		saved_style = GetWindowLong(hwnd, GWL_STYLE);
		saved_place.length = sizeof(saved_place);
		GetWindowPlacement(hwnd, &saved_place);

		MONITORINFO mi;
		memset(&mi, 0, sizeof(mi));
		mi.cbSize = sizeof(mi);
		HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
		if (!GetMonitorInfo(mon, &mi))
			return;

		SetWindowLong(hwnd, GWL_STYLE, saved_style & ~WS_OVERLAPPEDWINDOW);
		SetWindowPos(hwnd, HWND_TOP,
			     mi.rcMonitor.left, mi.rcMonitor.top,
			     mi.rcMonitor.right - mi.rcMonitor.left,
			     mi.rcMonitor.bottom - mi.rcMonitor.top,
			     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
	} else {
		SetWindowLong(hwnd, GWL_STYLE, saved_style);
		SetWindowPlacement(hwnd, &saved_place);
		SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
			     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
			     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
	}
}

void
lud__platform_swap(void)
{
	eglSwapBuffers(egl_display, egl_surface);
}
