/*
 * platform_x11.c — X11 + EGL platform backend for ludica
 *
 * Implements the platform interface from ludica_internal.h:
 *   lud__platform_init()
 *   lud__platform_shutdown()
 *   lud__platform_poll_events()
 *   lud__platform_swap()
 */

#include "ludica_internal.h"

#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

/* ---- X11 keysym to lud_keycode translation ---- */

static enum lud_keycode
translate_keysym(KeySym sym)
{
	/* Printable ASCII range */
	if (sym >= XK_space && sym <= XK_asciitilde) {
		/* Uppercase letters -> our enum uses uppercase ASCII values */
		if (sym >= XK_a && sym <= XK_z)
			return (enum lud_keycode)(sym - XK_a + LUD_KEY_A);
		/* Already matches ASCII for digits, punctuation */
		return (enum lud_keycode)sym;
	}

	switch (sym) {
	case XK_Escape:       return LUD_KEY_ESCAPE;
	case XK_Return:       return LUD_KEY_ENTER;
	case XK_Tab:          return LUD_KEY_TAB;
	case XK_BackSpace:    return LUD_KEY_BACKSPACE;
	case XK_Insert:       return LUD_KEY_INSERT;
	case XK_Delete:       return LUD_KEY_DELETE;
	case XK_Right:        return LUD_KEY_RIGHT;
	case XK_Left:         return LUD_KEY_LEFT;
	case XK_Down:         return LUD_KEY_DOWN;
	case XK_Up:           return LUD_KEY_UP;
	case XK_Page_Up:      return LUD_KEY_PAGE_UP;
	case XK_Page_Down:    return LUD_KEY_PAGE_DOWN;
	case XK_Home:         return LUD_KEY_HOME;
	case XK_End:          return LUD_KEY_END;
	case XK_Caps_Lock:    return LUD_KEY_CAPS_LOCK;
	case XK_Scroll_Lock:  return LUD_KEY_SCROLL_LOCK;
	case XK_Num_Lock:     return LUD_KEY_NUM_LOCK;
	case XK_Print:        return LUD_KEY_PRINT_SCREEN;
	case XK_Pause:        return LUD_KEY_PAUSE;
	case XK_F1:           return LUD_KEY_F1;
	case XK_F2:           return LUD_KEY_F2;
	case XK_F3:           return LUD_KEY_F3;
	case XK_F4:           return LUD_KEY_F4;
	case XK_F5:           return LUD_KEY_F5;
	case XK_F6:           return LUD_KEY_F6;
	case XK_F7:           return LUD_KEY_F7;
	case XK_F8:           return LUD_KEY_F8;
	case XK_F9:           return LUD_KEY_F9;
	case XK_F10:          return LUD_KEY_F10;
	case XK_F11:          return LUD_KEY_F11;
	case XK_F12:          return LUD_KEY_F12;
	case XK_KP_0:         return LUD_KEY_KP_0;
	case XK_KP_1:         return LUD_KEY_KP_1;
	case XK_KP_2:         return LUD_KEY_KP_2;
	case XK_KP_3:         return LUD_KEY_KP_3;
	case XK_KP_4:         return LUD_KEY_KP_4;
	case XK_KP_5:         return LUD_KEY_KP_5;
	case XK_KP_6:         return LUD_KEY_KP_6;
	case XK_KP_7:         return LUD_KEY_KP_7;
	case XK_KP_8:         return LUD_KEY_KP_8;
	case XK_KP_9:         return LUD_KEY_KP_9;
	case XK_KP_Decimal:   return LUD_KEY_KP_DECIMAL;
	case XK_KP_Divide:    return LUD_KEY_KP_DIVIDE;
	case XK_KP_Multiply:  return LUD_KEY_KP_MULTIPLY;
	case XK_KP_Subtract:  return LUD_KEY_KP_SUBTRACT;
	case XK_KP_Add:       return LUD_KEY_KP_ADD;
	case XK_KP_Enter:     return LUD_KEY_KP_ENTER;
	case XK_KP_Equal:     return LUD_KEY_KP_EQUAL;
	case XK_Shift_L:      return LUD_KEY_LEFT_SHIFT;
	case XK_Control_L:    return LUD_KEY_LEFT_CONTROL;
	case XK_Alt_L:        return LUD_KEY_LEFT_ALT;
	case XK_Super_L:      return LUD_KEY_LEFT_SUPER;
	case XK_Shift_R:      return LUD_KEY_RIGHT_SHIFT;
	case XK_Control_R:    return LUD_KEY_RIGHT_CONTROL;
	case XK_Alt_R:        return LUD_KEY_RIGHT_ALT;
	case XK_Super_R:      return LUD_KEY_RIGHT_SUPER;
	case XK_Menu:         return LUD_KEY_MENU;
	default:              return LUD_KEY_UNKNOWN;
	}
}

static unsigned
translate_modifiers(unsigned x11_state)
{
	unsigned mods = 0;
	if (x11_state & ShiftMask)   mods |= LUD_MOD_SHIFT;
	if (x11_state & ControlMask) mods |= LUD_MOD_CTRL;
	if (x11_state & Mod1Mask)    mods |= LUD_MOD_ALT;
	if (x11_state & Mod4Mask)    mods |= LUD_MOD_SUPER;
	return mods;
}

/* ---- EGL config ---- */

static const EGLint config_attribs[] = {
	EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
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

static const EGLint gles_ctx_attribs[] = {
	EGL_CONTEXT_CLIENT_VERSION, 2,
	EGL_NONE
};

/* ---- Platform state ---- */

static Display    *xdisplay;
static Window      xwindow;
static EGLDisplay  egl_display = EGL_NO_DISPLAY;
static EGLSurface  egl_surface = EGL_NO_SURFACE;
static EGLContext  egl_context = EGL_NO_CONTEXT;

static Atom wm_protocols;
static Atom wm_delete_window;

/* Track key-repeat: X11 generates KeyRelease+KeyPress pairs for held keys */
static int auto_repeat_detected;

/* ---- Implementation ---- */

static Window
create_window(const lud_desc_t *desc)
{
	Screen *screen = DefaultScreenOfDisplay(xdisplay);
	Window root = RootWindowOfScreen(screen);
	unsigned long black = BlackPixelOfScreen(screen);

	long event_mask =
		StructureNotifyMask |
		KeyPressMask | KeyReleaseMask |
		ButtonPressMask | ButtonReleaseMask |
		PointerMotionMask |
		EnterWindowMask | LeaveWindowMask |
		FocusChangeMask |
		ExposureMask;

	XSetWindowAttributes wattr;
	memset(&wattr, 0, sizeof(wattr));
	wattr.event_mask = event_mask;
	wattr.background_pixel = black;
	wattr.border_pixel = black;

	int w = desc->width;
	int h = desc->height;
	int x = (WidthOfScreen(screen) - w) / 2;
	int y = (HeightOfScreen(screen) - h) / 2;

	Window win = XCreateWindow(xdisplay, root, x, y, w, h, 0,
				   CopyFromParent, InputOutput, CopyFromParent,
				   CWBorderPixel | CWBackPixel | CWEventMask, &wattr);

	/* WM_DELETE_WINDOW protocol */
	Atom prots[] = { wm_delete_window };
	XSetWMProtocols(xdisplay, win, prots, 1);

	/* Window title */
	XTextProperty title_prop;
	char *title = (char *)desc->app_name;
	Xutf8TextListToTextProperty(xdisplay, &title, 1, XTextStyle, &title_prop);

	/* Size hints */
	XSizeHints sizehints;
	memset(&sizehints, 0, sizeof(sizehints));
	if (desc->resizable) {
		sizehints.flags = USSize | PSize | PMinSize;
		sizehints.width = w;
		sizehints.height = h;
		sizehints.min_width = w / 4;
		sizehints.min_height = h / 4;
	} else {
		sizehints.flags = USSize | PSize | PMinSize | PMaxSize;
		sizehints.width = w;
		sizehints.height = h;
		sizehints.min_width = w;
		sizehints.min_height = h;
		sizehints.max_width = w;
		sizehints.max_height = h;
	}

	XWMHints wm_hints;
	memset(&wm_hints, 0, sizeof(wm_hints));
	XClassHint class_hints;
	memset(&class_hints, 0, sizeof(class_hints));

	XSetWMProperties(xdisplay, win, &title_prop, &title_prop,
			 NULL, 0, &sizehints, &wm_hints, &class_hints);
	XFree(title_prop.value);

	XMapWindow(xdisplay, win);

	return win;
}

int
lud__platform_init(const lud_desc_t *desc)
{
	/* Open X11 display */
	xdisplay = XOpenDisplay(NULL);
	if (!xdisplay) {
		lud_err("Failed to open X11 display");
		return LUD_ERR;
	}

	/* Intern atoms */
	wm_protocols = XInternAtom(xdisplay, "WM_PROTOCOLS", False);
	wm_delete_window = XInternAtom(xdisplay, "WM_DELETE_WINDOW", False);

	/* Initialize EGL */
	egl_display = eglGetDisplay((EGLNativeDisplayType)xdisplay);
	if (egl_display == EGL_NO_DISPLAY) {
		lud_err("No EGL display available");
		XCloseDisplay(xdisplay);
		xdisplay = NULL;
		return LUD_ERR;
	}

	EGLint major, minor;
	if (!eglInitialize(egl_display, &major, &minor)) {
		lud_err("Failed to initialize EGL");
		eglTerminate(egl_display);
		egl_display = EGL_NO_DISPLAY;
		XCloseDisplay(xdisplay);
		xdisplay = NULL;
		return LUD_ERR;
	}

	lud_log("EGL %d.%d: %s (%s)", major, minor,
		   eglQueryString(egl_display, EGL_VERSION),
		   eglQueryString(egl_display, EGL_VENDOR));

	/* Choose EGL config */
	EGLConfig egl_config;
	EGLint num_config;
	if (!eglChooseConfig(egl_display, config_attribs, &egl_config, 1, &num_config) ||
	    num_config == 0) {
		lud_err("No suitable EGL config found");
		eglTerminate(egl_display);
		egl_display = EGL_NO_DISPLAY;
		XCloseDisplay(xdisplay);
		xdisplay = NULL;
		return LUD_ERR;
	}

	/* Create X11 window */
	xwindow = create_window(desc);

	/* Create EGL surface + context */
	egl_surface = eglCreateWindowSurface(egl_display, egl_config,
					     (EGLNativeWindowType)xwindow, NULL);
	egl_context = eglCreateContext(egl_display, egl_config,
				       EGL_NO_CONTEXT, gles_ctx_attribs);
	eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);

	lud_log("GL: %s (%s)", glGetString(GL_VERSION), glGetString(GL_VENDOR));

	return LUD_OK;
}

void
lud__platform_shutdown(void)
{
	if (egl_display != EGL_NO_DISPLAY) {
		eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
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

	if (xdisplay) {
		if (xwindow) {
			XDestroyWindow(xdisplay, xwindow);
			xwindow = None;
		}
		XCloseDisplay(xdisplay);
		xdisplay = NULL;
	}
}

void
lud__platform_poll_events(void)
{
	XEvent xev;
	lud_event_t ev;

	while (XPending(xdisplay)) {
		XNextEvent(xdisplay, &xev);
		if (XFilterEvent(&xev, None))
			continue;

		memset(&ev, 0, sizeof(ev));

		switch (xev.type) {
		case KeyPress: {
			KeySym sym = XLookupKeysym(&xev.xkey, 0);
			enum lud_keycode kc = translate_keysym(sym);

			/* Detect auto-repeat: X11 sends KeyRelease+KeyPress
			 * with identical timestamps for held keys */
			auto_repeat_detected = 0;

			ev.type = LUD_EV_KEY_DOWN;
			ev.modifiers = translate_modifiers(xev.xkey.state);
			ev.key.keycode = kc;
			ev.key.repeat = 0; /* updated below if repeat detected */

			/* Check if this KeyPress follows an identical KeyRelease (auto-repeat) */
			if (XPending(xdisplay)) {
				XEvent next;
				XPeekEvent(xdisplay, &next);
				if (next.type == KeyPress &&
				    next.xkey.time == xev.xkey.time &&
				    next.xkey.keycode == xev.xkey.keycode) {
					ev.key.repeat = 1;
				}
			}

			lud__event_push(&ev);

			/* Also generate CHAR event for text input */
			{
				char buf[8];
				KeySym dummy;
				int len = XLookupString(&xev.xkey, buf, sizeof(buf) - 1, &dummy, NULL);
				if (len > 0) {
					/* Decode UTF-8 to codepoint (single byte for ASCII) */
					unsigned cp = 0;
					if ((unsigned char)buf[0] < 0x80) {
						cp = (unsigned char)buf[0];
					} else if ((unsigned char)buf[0] < 0xE0 && len >= 2) {
						cp = ((unsigned char)buf[0] & 0x1F) << 6 |
						     ((unsigned char)buf[1] & 0x3F);
					} else if ((unsigned char)buf[0] < 0xF0 && len >= 3) {
						cp = ((unsigned char)buf[0] & 0x0F) << 12 |
						     ((unsigned char)buf[1] & 0x3F) << 6 |
						     ((unsigned char)buf[2] & 0x3F);
					} else if (len >= 4) {
						cp = ((unsigned char)buf[0] & 0x07) << 18 |
						     ((unsigned char)buf[1] & 0x3F) << 12 |
						     ((unsigned char)buf[2] & 0x3F) << 6 |
						     ((unsigned char)buf[3] & 0x3F);
					}
					/* Skip control characters except tab, enter, backspace */
					if (cp >= 32 || cp == '\t' || cp == '\r' || cp == '\n') {
						lud_event_t cev;
						memset(&cev, 0, sizeof(cev));
						cev.type = LUD_EV_CHAR;
						cev.modifiers = translate_modifiers(xev.xkey.state);
						cev.ch.codepoint = cp;
						lud__event_push(&cev);
					}
				}
			}
			break;
		}

		case KeyRelease: {
			/* Detect auto-repeat: peek at next event. If it's a
			 * KeyPress with same keycode and time, this release
			 * is synthetic — skip it. */
			if (XPending(xdisplay)) {
				XEvent next;
				XPeekEvent(xdisplay, &next);
				if (next.type == KeyPress &&
				    next.xkey.time == xev.xkey.time &&
				    next.xkey.keycode == xev.xkey.keycode) {
					/* Auto-repeat — consume the release,
					 * the next KeyPress will be marked as repeat */
					auto_repeat_detected = 1;
					break;
				}
			}

			KeySym sym = XLookupKeysym(&xev.xkey, 0);
			ev.type = LUD_EV_KEY_UP;
			ev.modifiers = translate_modifiers(xev.xkey.state);
			ev.key.keycode = translate_keysym(sym);
			lud__event_push(&ev);
			break;
		}

		case MotionNotify:
			ev.type = LUD_EV_MOUSE_MOVE;
			ev.modifiers = translate_modifiers(xev.xmotion.state);
			ev.mouse_move.x = xev.xmotion.x;
			ev.mouse_move.y = xev.xmotion.y;
			lud__event_push(&ev);
			break;

		case ButtonPress:
		case ButtonRelease: {
			unsigned btn = xev.xbutton.button;

			/* Buttons 4/5 are scroll wheel on X11 */
			if (btn == Button4 || btn == Button5) {
				if (xev.type == ButtonPress) {
					ev.type = LUD_EV_MOUSE_SCROLL;
					ev.modifiers = translate_modifiers(xev.xbutton.state);
					ev.scroll.dx = 0.0f;
					ev.scroll.dy = (btn == Button4) ? 1.0f : -1.0f;
					lud__event_push(&ev);
				}
				break;
			}
			/* Buttons 6/7 are horizontal scroll */
			if (btn == 6 || btn == 7) {
				if (xev.type == ButtonPress) {
					ev.type = LUD_EV_MOUSE_SCROLL;
					ev.modifiers = translate_modifiers(xev.xbutton.state);
					ev.scroll.dx = (btn == 6) ? -1.0f : 1.0f;
					ev.scroll.dy = 0.0f;
					lud__event_push(&ev);
				}
				break;
			}

			ev.type = (xev.type == ButtonPress) ? LUD_EV_MOUSE_DOWN : LUD_EV_MOUSE_UP;
			ev.modifiers = translate_modifiers(xev.xbutton.state);
			ev.mouse_button.x = xev.xbutton.x;
			ev.mouse_button.y = xev.xbutton.y;
			if (btn == Button1)
				ev.mouse_button.button = LUD_MOUSE_LEFT;
			else if (btn == Button2)
				ev.mouse_button.button = LUD_MOUSE_MIDDLE;
			else if (btn == Button3)
				ev.mouse_button.button = LUD_MOUSE_RIGHT;
			else
				break; /* ignore extra buttons */
			lud__event_push(&ev);
			break;
		}

		case ConfigureNotify:
			if (xev.xconfigure.width != lud__state.win_width ||
			    xev.xconfigure.height != lud__state.win_height) {
				ev.type = LUD_EV_RESIZED;
				ev.resize.width = xev.xconfigure.width;
				ev.resize.height = xev.xconfigure.height;
				lud__event_push(&ev);
			}
			break;

		case FocusIn:
			ev.type = LUD_EV_FOCUS;
			lud__event_push(&ev);
			break;

		case FocusOut:
			ev.type = LUD_EV_UNFOCUS;
			lud__event_push(&ev);
			break;

		case ClientMessage:
			if (xev.xclient.message_type == wm_protocols &&
			    (Atom)xev.xclient.data.l[0] == wm_delete_window) {
				lud_quit();
			}
			break;

		default:
			break;
		}
	}
}

void
lud__platform_swap(void)
{
	eglSwapBuffers(egl_display, egl_surface);
}
