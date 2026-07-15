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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/select.h>
#include <sys/time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

/* EGL 1.5 / EGL_KHR_create_context */
#ifndef EGL_OPENGL_ES3_BIT
#define EGL_OPENGL_ES3_BIT 0x00000040
#endif

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

static Display    *xdisplay;
static Window      xwindow;
static EGLDisplay  egl_display = EGL_NO_DISPLAY;
static EGLSurface  egl_surface = EGL_NO_SURFACE;
static EGLContext  egl_context = EGL_NO_CONTEXT;

static Atom wm_protocols;
static Atom wm_delete_window;
static Atom net_wm_state;
static Atom net_wm_state_fullscreen;

/* ---- Clipboard state ---- */

static Atom clipboard_atom;   /* CLIPBOARD selection (Ctrl+C/Ctrl+V) */
static Atom utf8_atom;        /* UTF8_STRING target */
static Atom targets_atom;     /* TARGETS meta-target */
static Atom incr_atom;        /* INCR (incremental transfer) type */
static Atom clip_prop;        /* our destination property for reads */
static Atom uri_list_atom;    /* text/uri-list target (files) */
static Atom png_atom;         /* image/png target */

/* Selection data we serve while we own CLIPBOARD.  A copy may offer several
 * formats at once (e.g. image/png plus image/bmp); each is one offer, and a
 * requestor picks the target it understands. */
#define CLIP_MAX_OFFERS 8
struct clip_offer {
	Atom           target;
	unsigned char *data;
	size_t         len;
};
static struct clip_offer owned_offers[CLIP_MAX_OFFERS];
static int               owned_count;

static void
clip_offers_free(void)
{
	for (int i = 0; i < owned_count; i++) {
		free(owned_offers[i].data);
		owned_offers[i].data = NULL;
	}
	owned_count = 0;
}

static struct clip_offer *
clip_find_offer(Atom target)
{
	for (int i = 0; i < owned_count; i++)
		if (owned_offers[i].target == target)
			return &owned_offers[i];
	return NULL;
}

/* A single pending asynchronous read.  Only one may be in flight. */
static struct {
	int              active;
	lud_clipboard_cb cb;
	void            *user;
	char             format[64];
} clip_req;

/* Incremental receive: the selection owner advertised INCR, so the payload
 * arrives in chunks via PropertyNotify.  The mode selects how the completed
 * buffer is delivered: SYNC is drained inline by clip_in_sync, ASYNC goes to
 * clip_req.cb, DROP becomes a LUD_EV_DROP event. */
enum { CLIP_IN_SYNC, CLIP_IN_ASYNC, CLIP_IN_DROP };
static struct {
	int             active;
	int             mode;
	unsigned char  *buf;
	size_t          len;
	size_t          cap;
} clip_in;

/* Incremental send: we own the selection and a requestor is pulling a large
 * payload from us one chunk per PropertyDelete.  Only one may be in flight;
 * a snapshot of the bytes keeps a concurrent set_text from freeing them. */
static struct {
	int             active;
	Window          requestor;
	Atom            prop;
	Atom            target;
	unsigned char  *data;
	size_t          len;
	size_t          sent;
} clip_out;

/* ---- Drag and drop (XDND drop target) ---- */

static Atom xdnd_aware;         /* XdndAware property advertising support */
static Atom xdnd_enter;         /* drag entered our window */
static Atom xdnd_position;      /* pointer moved during a drag */
static Atom xdnd_status;        /* our reply: accept/refuse this position */
static Atom xdnd_leave;         /* drag left without dropping */
static Atom xdnd_drop;          /* user released: fetch the data */
static Atom xdnd_finished;      /* our reply: transfer done */
static Atom xdnd_selection;     /* selection the data is offered on */
static Atom xdnd_type_list;     /* source's full type list (property) */
static Atom xdnd_action_copy;   /* the copy action */

#define XDND_VERSION 5

/* In-progress drop negotiation.  source==0 means no drag is over us. */
static struct {
	Window source;
	int    version;   /* min(ours, source's) */
	Atom   type;      /* the target we chose to request; None if unsupported */
	int    x, y;      /* last pointer position, window pixels */
} xdnd;

/* Data delivered to the app for one frame via LUD_EV_DROP, owned here and
 * freed at the top of the next poll. */
static unsigned char *drop_data;
static size_t         drop_len;
static const char    *drop_format;

/* A drag we started (source side).  While active we own XdndSelection and have
 * grabbed the pointer; `over` tracks the XDND-aware window under the pointer. */
static struct {
	int    active;
	Window over;         /* current target window, 0 if none */
	int    over_version; /* target's XDND version */
	int    accepted;     /* last XdndStatus from `over` accepted */
	int    dropping;     /* XdndDrop sent, awaiting XdndFinished */
	struct timeval deadline; /* give up waiting for XdndFinished after this */
} drag;
/* Formats we serve on XdndSelection for the active drag (like the clipboard
 * offers, a drag can advertise several types at once). */
static struct clip_offer drag_offers[CLIP_MAX_OFFERS];
static int               drag_count;

static void
drag_offers_free(void)
{
	for (int i = 0; i < drag_count; i++) {
		free(drag_offers[i].data);
		drag_offers[i].data = NULL;
	}
	drag_count = 0;
}

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
		PropertyChangeMask |   /* INCR clipboard transfers */
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

static int (*prev_x_error_handler)(Display *, XErrorEvent *);

/* Xlib delivers protocol errors asynchronously and its default handler exits
 * the process.  During an INCR clipboard transfer we touch another client's
 * window, and that requestor can vanish mid-transfer (clipboard managers do
 * this routinely), producing a BadWindow or BadMatch we must survive.  Swallow
 * exactly those; defer anything else to the previous handler so real faults
 * still surface. */
static int
x_error_handler(Display *d, XErrorEvent *e)
{
	if (e->error_code == BadWindow || e->error_code == BadMatch)
		return 0;
	return prev_x_error_handler ? prev_x_error_handler(d, e) : 0;
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

	/* Survive protocol errors from clipboard requestors that disappear. */
	prev_x_error_handler = XSetErrorHandler(x_error_handler);

	/* Intern atoms */
	wm_protocols = XInternAtom(xdisplay, "WM_PROTOCOLS", False);
	wm_delete_window = XInternAtom(xdisplay, "WM_DELETE_WINDOW", False);
	net_wm_state = XInternAtom(xdisplay, "_NET_WM_STATE", False);
	net_wm_state_fullscreen = XInternAtom(xdisplay, "_NET_WM_STATE_FULLSCREEN", False);
	clipboard_atom = XInternAtom(xdisplay, "CLIPBOARD", False);
	utf8_atom = XInternAtom(xdisplay, "UTF8_STRING", False);
	targets_atom = XInternAtom(xdisplay, "TARGETS", False);
	incr_atom = XInternAtom(xdisplay, "INCR", False);
	clip_prop = XInternAtom(xdisplay, "LUD_CLIPBOARD", False);
	uri_list_atom = XInternAtom(xdisplay, LUD_CLIPBOARD_URI_LIST, False);
	png_atom = XInternAtom(xdisplay, LUD_CLIPBOARD_PNG, False);

	xdnd_aware = XInternAtom(xdisplay, "XdndAware", False);
	xdnd_enter = XInternAtom(xdisplay, "XdndEnter", False);
	xdnd_position = XInternAtom(xdisplay, "XdndPosition", False);
	xdnd_status = XInternAtom(xdisplay, "XdndStatus", False);
	xdnd_leave = XInternAtom(xdisplay, "XdndLeave", False);
	xdnd_drop = XInternAtom(xdisplay, "XdndDrop", False);
	xdnd_finished = XInternAtom(xdisplay, "XdndFinished", False);
	xdnd_selection = XInternAtom(xdisplay, "XdndSelection", False);
	xdnd_type_list = XInternAtom(xdisplay, "XdndTypeList", False);
	xdnd_action_copy = XInternAtom(xdisplay, "XdndActionCopy", False);

	/* Configure EGL for requested GLES version */
	gles_ctx_attribs[1] = desc->gles_version;
	if (desc->gles_version >= 3)
		config_attribs[CONFIG_RENDERABLE_IDX] = EGL_OPENGL_ES3_BIT;
	else
		config_attribs[CONFIG_RENDERABLE_IDX] = EGL_OPENGL_ES2_BIT;

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

	/* Advertise drag-and-drop support (XDND drop target). */
	{
		Atom version = XDND_VERSION;
		XChangeProperty(xdisplay, xwindow, xdnd_aware, XA_ATOM, 32,
				PropModeReplace, (unsigned char *)&version, 1);
	}

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

	clip_offers_free();

	free(clip_in.buf);
	memset(&clip_in, 0, sizeof(clip_in));
	free(clip_out.data);
	memset(&clip_out, 0, sizeof(clip_out));

	free(drop_data);
	drop_data = NULL;
	drop_len = 0;
	drop_format = NULL;

	drag_offers_free();
}

/* ---- Clipboard ---- */

/* Map a MIME-ish format string to an X11 target atom.  Plain text maps to
 * the conventional UTF8_STRING atom; any other string (including structured
 * text targets like "text/uri-list") is interned verbatim, so callers can
 * request targets like "image/png" or "text/uri-list" unchanged. */
static Atom
clip_format_to_target(const char *format)
{
	if (!format ||
	    !strcmp(format, LUD_CLIPBOARD_TEXT) ||
	    !strcmp(format, "text/plain"))
		return utf8_atom;
	return XInternAtom(xdisplay, format, False);
}

static void
clip_req_reset(void)
{
	clip_req.active = 0;
	clip_req.cb = NULL;
	clip_req.user = NULL;
}

/* Largest single-property transfer we attempt, in bytes.  ICCCM advises
 * staying well under the server's maximum request size so an INCR transfer
 * does not collide with other traffic; a quarter of it is the usual choice.
 * Payloads above this switch to INCR. */
static size_t
clip_chunk_max(void)
{
	long units = XExtendedMaxRequestSize(xdisplay);
	if (units <= 0)
		units = XMaxRequestSize(xdisplay);
	size_t bytes = (size_t)units * 4;
	if (bytes < 4096)
		bytes = 4096;
	return bytes / 4;
}

/* Probe the type of our destination property without fetching its value.
 * Used to spot an INCR reply before reading. */
static Atom
clip_probe_type(void)
{
	Atom type;
	int format;
	unsigned long nitems, bytes_after;
	unsigned char *prop = NULL;

	if (XGetWindowProperty(xdisplay, xwindow, clip_prop, 0, 0, False,
			       AnyPropertyType, &type, &format,
			       &nitems, &bytes_after, &prop) != Success)
		return None;
	if (prop)
		XFree(prop);
	return type;
}

/* Read our destination property (filled by the selection owner) into a
 * malloc'd, NUL-terminated buffer.  Returns NULL on failure.  The caller
 * must have ruled out an INCR reply first (see clip_probe_type). */
static unsigned char *
clip_read_prop(size_t *len_out)
{
	Atom type;
	int format;
	unsigned long nitems, bytes_after;
	unsigned char *prop = NULL;

	/* Probe the size without fetching data. */
	if (XGetWindowProperty(xdisplay, xwindow, clip_prop, 0, 0, False,
			       AnyPropertyType, &type, &format,
			       &nitems, &bytes_after, &prop) != Success)
		return NULL;
	if (prop) {
		XFree(prop);
		prop = NULL;
	}

	if (type == None)
		return NULL;
	if (type == incr_atom) {
		/* Callers probe for INCR first and drive it incrementally; reaching
		 * here means a caller skipped that, so decline rather than misread. */
		XDeleteProperty(xdisplay, xwindow, clip_prop);
		return NULL;
	}

	long nbytes = (long)bytes_after;
	if (XGetWindowProperty(xdisplay, xwindow, clip_prop, 0, (nbytes + 3) / 4,
			       False, AnyPropertyType, &type, &format,
			       &nitems, &bytes_after, &prop) != Success)
		return NULL;

	size_t len = (size_t)nitems * (format / 8);
	unsigned char *out = malloc(len + 1);
	if (out) {
		memcpy(out, prop, len);
		out[len] = 0;
		if (len_out)
			*len_out = len;
	}
	XFree(prop);
	XDeleteProperty(xdisplay, xwindow, clip_prop);
	return out;
}

/* Begin an incremental receive after the owner advertised INCR.  Deleting
 * the INCR property tells the owner to start appending data chunks, each of
 * which arrives as a PropertyNotify(PropertyNewValue). */
static void
clip_in_begin(int mode)
{
	free(clip_in.buf);
	clip_in.buf = NULL;
	clip_in.len = 0;
	clip_in.cap = 0;
	clip_in.active = 1;
	clip_in.mode = mode;
	XDeleteProperty(xdisplay, xwindow, clip_prop);
	XFlush(xdisplay);
}

/* Consume one INCR chunk from the destination property and delete it to
 * request the next.  A zero-length chunk terminates the transfer.  Returns 1
 * when the transfer is complete (buf/len then hold the result), 0 for more. */
static int
clip_in_step(void)
{
	Atom type;
	int format;
	unsigned long nitems, bytes_after;
	unsigned char *prop = NULL;

	/* Delete-on-read hands the owner its cue to send the following chunk. */
	if (XGetWindowProperty(xdisplay, xwindow, clip_prop, 0, LONG_MAX, True,
			       AnyPropertyType, &type, &format,
			       &nitems, &bytes_after, &prop) != Success)
		return 1;

	size_t chunk = (size_t)nitems * (format / 8);
	if (chunk == 0) {
		if (prop)
			XFree(prop);
		return 1; /* terminator: transfer complete */
	}

	if (clip_in.len + chunk + 1 > clip_in.cap) {
		size_t ncap = clip_in.cap ? clip_in.cap : 4096;
		while (ncap < clip_in.len + chunk + 1)
			ncap *= 2;
		unsigned char *nb = realloc(clip_in.buf, ncap);
		if (!nb) {
			XFree(prop);
			return 1; /* out of memory: give up with what we have */
		}
		clip_in.buf = nb;
		clip_in.cap = ncap;
	}
	memcpy(clip_in.buf + clip_in.len, prop, chunk);
	clip_in.len += chunk;
	clip_in.buf[clip_in.len] = 0;
	XFree(prop);
	return 0;
}

/* Synchronously drain an INCR transfer for the blocking get path.  Blocks
 * with a per-chunk timeout so a stalled owner cannot hang the caller.  Returns
 * a malloc'd, NUL-terminated buffer (ownership passes to caller) and writes
 * the byte count to *len_out (which may be NULL). */
static unsigned char *
clip_in_sync(size_t *len_out)
{
	int fd = ConnectionNumber(xdisplay);

	clip_in_begin(CLIP_IN_SYNC);
	for (;;) {
		XEvent xev;
		int progressed = 0;
		while (XCheckTypedWindowEvent(xdisplay, xwindow, PropertyNotify, &xev)) {
			if (xev.xproperty.atom != clip_prop ||
			    xev.xproperty.state != PropertyNewValue)
				continue;
			progressed = 1;
			if (clip_in_step()) {
				unsigned char *out = clip_in.buf;
				if (len_out)
					*len_out = clip_in.len;
				clip_in.buf = NULL;
				clip_in.active = 0;
				return out; /* NULL only if nothing was received */
			}
		}
		if (progressed)
			continue; /* made progress: reset the wait */

		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		struct timeval tv = { 1, 0 }; /* 1s per chunk */
		if (select(fd + 1, &fds, NULL, NULL, &tv) <= 0) {
			free(clip_in.buf);
			clip_in.buf = NULL;
			clip_in.active = 0;
			return NULL; /* stalled owner */
		}
		XEventsQueued(xdisplay, QueuedAfterReading);
	}
}

/* Send the next INCR chunk to the requestor, or the terminating zero-length
 * write once all data has gone.  Driven by PropertyDelete on the requestor. */
static void
clip_out_step(void)
{
	size_t chunk = clip_chunk_max();
	size_t remaining = clip_out.len - clip_out.sent;
	size_t n = remaining < chunk ? remaining : chunk;

	XChangeProperty(xdisplay, clip_out.requestor, clip_out.prop,
			clip_out.target, 8, PropModeReplace,
			clip_out.data + clip_out.sent, (int)n);
	clip_out.sent += n;
	XFlush(xdisplay);

	if (n == 0) {
		/* The zero-length write just sent marks the end.  Stop watching
		 * the requestor and drop the snapshot. */
		XSelectInput(xdisplay, clip_out.requestor, NoEventMask);
		free(clip_out.data);
		memset(&clip_out, 0, sizeof(clip_out));
	}
}

/* Answer one SelectionRequest from a set of offers: advertise every target on
 * a TARGETS request, else serve the matching offer (single write, or INCR when
 * large).  Sets notify->property on success. */
static void
serve_offers(const XSelectionRequestEvent *req, XSelectionEvent *notify,
	     const struct clip_offer *offers, int count)
{
	/* Obsolete clients set property to None; fall back to target. */
	Atom prop = req->property != None ? req->property : req->target;

	if (req->target == targets_atom) {
		Atom list[1 + CLIP_MAX_OFFERS];
		int n = 0;
		list[n++] = targets_atom;
		for (int i = 0; i < count; i++)
			list[n++] = offers[i].target;
		XChangeProperty(xdisplay, req->requestor, prop, XA_ATOM, 32,
				PropModeReplace, (unsigned char *)list, n);
		notify->property = prop;
		return;
	}

	const struct clip_offer *o = NULL;
	for (int i = 0; i < count; i++)
		if (offers[i].target == req->target) {
			o = &offers[i];
			break;
		}
	if (!o)
		return; /* unsupported target: leave property None to refuse */

	if (o->len <= clip_chunk_max()) {
		/* Small enough for a single property write. */
		XChangeProperty(xdisplay, req->requestor, prop, o->target, 8,
				PropModeReplace, o->data, (int)o->len);
		notify->property = prop;
	} else if (!clip_out.active) {
		/* Large: advertise INCR, then stream on PropertyDelete.  Snapshot
		 * the bytes so a later set cannot free them mid-transfer. */
		unsigned char *snap = malloc(o->len);
		if (snap) {
			long hint = (long)o->len;
			memcpy(snap, o->data, o->len);
			XChangeProperty(xdisplay, req->requestor, prop, incr_atom,
					32, PropModeReplace,
					(unsigned char *)&hint, 1);
			XSelectInput(xdisplay, req->requestor, PropertyChangeMask);
			clip_out.active = 1;
			clip_out.requestor = req->requestor;
			clip_out.prop = prop;
			clip_out.target = o->target;
			clip_out.data = snap;
			clip_out.len = o->len;
			clip_out.sent = 0;
			notify->property = prop;
		}
		/* If an INCR send is already in flight, leave property None to
		 * refuse this second requestor. */
	}
}

/* Serve an incoming request from another client.  We answer two selections
 * with the same machinery: CLIPBOARD from the clipboard offers, and
 * XdndSelection from the active drag's single-format payload. */
static void
clip_handle_request(const XSelectionRequestEvent *req)
{
	XSelectionEvent notify;

	memset(&notify, 0, sizeof(notify));
	notify.type = SelectionNotify;
	notify.display = req->display;
	notify.requestor = req->requestor;
	notify.selection = req->selection;
	notify.target = req->target;
	notify.time = req->time;
	notify.property = None; /* None signals refusal */

	if (req->selection == clipboard_atom && owned_count > 0) {
		serve_offers(req, &notify, owned_offers, owned_count);
	} else if (req->selection == xdnd_selection && drag_count > 0) {
		serve_offers(req, &notify, drag_offers, drag_count);
	}

	XSendEvent(xdisplay, req->requestor, False, 0, (XEvent *)&notify);
	XFlush(xdisplay);
}

/* Claim CLIPBOARD ownership now that owned_offers is populated.  On refusal,
 * drop the offers so we do not claim to serve what we no longer own. */
static int
clip_own(void)
{
	XSetSelectionOwner(xdisplay, clipboard_atom, xwindow, CurrentTime);
	if (XGetSelectionOwner(xdisplay, clipboard_atom) != xwindow) {
		clip_offers_free();
		return LUD_ERR;
	}
	XFlush(xdisplay);
	return LUD_OK;
}

/* Replace the clipboard with a single offer serving `len` bytes under
 * `target`.  The bytes are copied and NUL-terminated (terminator not counted
 * in the offer length). */
static int
clip_set(Atom target, const void *data, size_t len)
{
	if (!xdisplay)
		return LUD_ERR;

	unsigned char *copy = malloc(len + 1);
	if (!copy)
		return LUD_ERR;
	if (len)
		memcpy(copy, data, len);
	copy[len] = 0;

	clip_offers_free();
	owned_offers[0].target = target;
	owned_offers[0].data = copy;
	owned_offers[0].len = len;
	owned_count = 1;
	return clip_own();
}

/* Blocking read of `target` from the clipboard into a malloc'd, NUL-terminated
 * buffer.  Writes the byte count to *len_out (may be NULL).  Returns NULL when
 * the clipboard is empty, lacks that target, or the owner does not answer. */
static unsigned char *
clip_get(Atom target, size_t *len_out)
{
	if (len_out)
		*len_out = 0;
	if (!xdisplay || clip_req.active)
		return NULL;

	/* If we are the owner, answer from our own copy with no round-trip. */
	if (XGetSelectionOwner(xdisplay, clipboard_atom) == xwindow) {
		struct clip_offer *o = clip_find_offer(target);
		if (o) {
			unsigned char *out = malloc(o->len + 1);
			if (out) {
				memcpy(out, o->data, o->len);
				out[o->len] = 0;
				if (len_out)
					*len_out = o->len;
			}
			return out;
		}
		return NULL;
	}

	XConvertSelection(xdisplay, clipboard_atom, target, clip_prop,
			  xwindow, CurrentTime);
	XFlush(xdisplay);

	/* Block up to ~100ms for the reply.  XCheckTypedWindowEvent pulls only
	 * our SelectionNotify, leaving other queued events (keys, mouse) in
	 * place for the normal frame poll. */
	int fd = ConnectionNumber(xdisplay);
	struct timeval start, now;
	gettimeofday(&start, NULL);
	for (;;) {
		XEvent xev;
		if (XCheckTypedWindowEvent(xdisplay, xwindow, SelectionNotify, &xev)) {
			if (xev.xselection.property == None)
				return NULL; /* owner refused */
			if (clip_probe_type() == incr_atom)
				return clip_in_sync(len_out);
			return clip_read_prop(len_out);
		}

		gettimeofday(&now, NULL);
		long elapsed = (now.tv_sec - start.tv_sec) * 1000000L +
			       (now.tv_usec - start.tv_usec);
		long remaining = 100000L - elapsed;
		if (remaining <= 0)
			return NULL; /* timeout: no owner or slow owner */

		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		struct timeval tv = { 0, remaining };
		if (select(fd + 1, &fds, NULL, NULL, &tv) <= 0)
			return NULL;
		XEventsQueued(xdisplay, QueuedAfterReading);
	}
}

int
lud_clipboard_set_text(const char *utf8)
{
	if (!utf8)
		utf8 = "";
	return clip_set(utf8_atom, utf8, strlen(utf8));
}

char *
lud_clipboard_get_text(void)
{
	return (char *)clip_get(utf8_atom, NULL);
}

int
lud_clipboard_set_data(const char *format, const void *data, size_t len)
{
	if (!xdisplay)
		return LUD_ERR;
	return clip_set(clip_format_to_target(format), data, len);
}

void *
lud_clipboard_get_data(const char *format, size_t *len_out)
{
	if (len_out)
		*len_out = 0;
	if (!xdisplay)
		return NULL;
	return clip_get(clip_format_to_target(format), len_out);
}

int
lud_clipboard_set_multi(const lud_clip_item_t *items, int count)
{
	if (!xdisplay || !items || count <= 0 || count > CLIP_MAX_OFFERS)
		return LUD_ERR;

	/* Build the offers in a scratch array first; only swap them in once all
	 * copies succeed, so a mid-way failure leaves the clipboard untouched. */
	struct clip_offer tmp[CLIP_MAX_OFFERS];
	int n = 0;
	for (int i = 0; i < count; i++) {
		size_t len = items[i].len;
		unsigned char *copy = malloc(len + 1);
		if (!copy) {
			for (int j = 0; j < n; j++)
				free(tmp[j].data);
			return LUD_ERR;
		}
		if (len)
			memcpy(copy, items[i].data, len);
		copy[len] = 0;
		tmp[n].target = clip_format_to_target(items[i].format);
		tmp[n].data = copy;
		tmp[n].len = len;
		n++;
	}

	clip_offers_free();
	for (int i = 0; i < n; i++)
		owned_offers[i] = tmp[i];
	owned_count = n;
	return clip_own();
}

/* ---- File lists (text/uri-list) ---- */

int
lud_clipboard_set_files(const char *const *paths, int count)
{
	if (!xdisplay)
		return LUD_ERR;

	size_t len = 0;
	char *buf = lud__uri_list_encode(paths, count, &len);
	if (!buf)
		return LUD_ERR;

	int rc = clip_set(clip_format_to_target(LUD_CLIPBOARD_URI_LIST), buf, len);
	free(buf);
	return rc;
}

char **
lud_clipboard_get_files(void)
{
	size_t len = 0;
	unsigned char *raw = clip_get(clip_format_to_target(LUD_CLIPBOARD_URI_LIST),
				      &len);
	if (!raw)
		return NULL;
	char **list = lud_parse_uri_list(raw, len);
	free(raw);
	return list;
}

void
lud_clipboard_get_async(const char *format, lud_clipboard_cb cb, void *user)
{
	if (!cb)
		return;

	/* Fail fast if unavailable or another request is still pending. */
	if (!xdisplay || clip_req.active) {
		cb(format, NULL, 0, user);
		return;
	}

	Atom target = clip_format_to_target(format);
	snprintf(clip_req.format, sizeof(clip_req.format), "%s",
		 format ? format : LUD_CLIPBOARD_TEXT);

	/* Owner shortcut: satisfy from our own copy immediately. */
	if (XGetSelectionOwner(xdisplay, clipboard_atom) == xwindow) {
		struct clip_offer *o = clip_find_offer(target);
		if (o)
			cb(clip_req.format, o->data, o->len, user);
		else
			cb(clip_req.format, NULL, 0, user);
		return;
	}

	clip_req.active = 1;
	clip_req.cb = cb;
	clip_req.user = user;
	XConvertSelection(xdisplay, clipboard_atom, target, clip_prop,
			  xwindow, CurrentTime);
	XFlush(xdisplay);
}

/* Deliver a completed async read (called from the event poll).  For an INCR
 * reply, start the incremental receive instead and defer delivery until the
 * PropertyNotify handler sees the terminating chunk. */
static void
clip_deliver(const XSelectionEvent *sel)
{
	unsigned char *data = NULL;
	size_t len = 0;

	if (sel->property != None && clip_probe_type() == incr_atom) {
		clip_in_begin(CLIP_IN_ASYNC);
		return; /* completion happens in the PropertyNotify handler */
	}

	if (sel->property != None)
		data = clip_read_prop(&len);

	if (clip_req.cb)
		clip_req.cb(clip_req.format, data, len, clip_req.user);

	free(data);
	clip_req_reset();
}

/* Finish an async INCR receive once the terminating chunk has arrived. */
static void
clip_in_finish_async(void)
{
	if (clip_req.cb)
		clip_req.cb(clip_req.format, clip_in.buf, clip_in.len, clip_req.user);
	free(clip_in.buf);
	clip_in.buf = NULL;
	clip_in.active = 0;
	clip_req_reset();
}

/* ---- Drag and drop (XDND drop target) ---- */

static void
xdnd_reset(void)
{
	xdnd.source = 0;
	xdnd.version = 0;
	xdnd.type = None;
}

/* Map a droppable target atom to a stable public format string, or NULL if we
 * do not accept it. */
static const char *
xdnd_format_for(Atom target)
{
	if (target == uri_list_atom)
		return LUD_CLIPBOARD_URI_LIST;
	if (target == png_atom)
		return LUD_CLIPBOARD_PNG;
	if (target == utf8_atom)
		return LUD_CLIPBOARD_TEXT;
	return NULL;
}

/* Choose the target we prefer from a candidate list, most useful first. */
static Atom
xdnd_pick(const Atom *cands, int n)
{
	static const Atom *const order[] = { &uri_list_atom, &png_atom, &utf8_atom };
	for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++)
		for (int j = 0; j < n; j++)
			if (cands[j] == *order[i])
				return *order[i];
	return None;
}

/* Send an XDND ClientMessage of `type` with five long data words. */
static void
xdnd_send(Window to, Atom type, long d0, long d1, long d2, long d3, long d4)
{
	XClientMessageEvent m;
	memset(&m, 0, sizeof(m));
	m.type = ClientMessage;
	m.display = xdisplay;
	m.window = to;
	m.message_type = type;
	m.format = 32;
	m.data.l[0] = (long)xwindow;
	m.data.l[1] = d1;
	m.data.l[2] = d2;
	m.data.l[3] = d3;
	m.data.l[4] = d4;
	(void)d0; /* l[0] is always our window per the protocol */
	XSendEvent(xdisplay, to, False, NoEventMask, (XEvent *)&m);
	XFlush(xdisplay);
}

static void
xdnd_handle_enter(const XClientMessageEvent *m)
{
	xdnd_reset();
	xdnd.source = (Window)m->data.l[0];
	xdnd.version = (int)((unsigned long)m->data.l[1] >> 24);

	Atom cands[16];
	int n = 0;
	if (m->data.l[1] & 1) {
		/* More than three types: read the source's XdndTypeList. */
		Atom type;
		int fmt;
		unsigned long nitems, after;
		unsigned char *data = NULL;
		if (XGetWindowProperty(xdisplay, xdnd.source, xdnd_type_list, 0, 16,
				       False, XA_ATOM, &type, &fmt, &nitems,
				       &after, &data) == Success && data) {
			Atom *a = (Atom *)data;
			for (unsigned long i = 0; i < nitems && n < 16; i++)
				cands[n++] = a[i];
			XFree(data);
		}
	} else {
		for (int i = 2; i <= 4; i++)
			if (m->data.l[i])
				cands[n++] = (Atom)m->data.l[i];
	}
	xdnd.type = xdnd_pick(cands, n);
}

static void
xdnd_handle_position(const XClientMessageEvent *m)
{
	Window src = (Window)m->data.l[0];
	if (src != xdnd.source)
		return;

	/* Root coordinates packed in the high/low halves of data.l[2]. */
	int rx = (int)((unsigned long)m->data.l[2] >> 16);
	int ry = (int)(m->data.l[2] & 0xffff);
	Window child;
	XTranslateCoordinates(xdisplay, DefaultRootWindow(xdisplay), xwindow,
			      rx, ry, &xdnd.x, &xdnd.y, &child);

	int accept = xdnd.type != None;
	/* XdndStatus: l[1] bit0 = accept; empty rect (l[2]=l[3]=0) asks the
	 * source to keep sending XdndPosition; l[4] = action we will perform. */
	xdnd_send(xdnd.source, xdnd_status, 0, accept ? 1 : 0, 0, 0,
		  accept ? (long)xdnd_action_copy : (long)None);
}

/* Deliver the dropped bytes (ownership transferred here) to the app and tell
 * the source the transfer finished. */
static void
xdnd_deliver(unsigned char *data, size_t len)
{
	const char *fmt = xdnd_format_for(xdnd.type);

	free(drop_data);
	drop_data = data;
	drop_len = len;
	drop_format = fmt;

	if (fmt && data) {
		lud_event_t ev;
		memset(&ev, 0, sizeof(ev));
		ev.type = LUD_EV_DROP;
		ev.drop.format = fmt;
		ev.drop.data = drop_data;
		ev.drop.len = drop_len;
		ev.drop.x = xdnd.x;
		ev.drop.y = xdnd.y;
		lud__event_push(&ev);
	}

	if (xdnd.source)
		xdnd_send(xdnd.source, xdnd_finished, 0,
			  (fmt && data) ? 1 : 0, (long)xdnd_action_copy, 0, 0);
	xdnd_reset();
}

static void
xdnd_handle_drop(const XClientMessageEvent *m)
{
	Window src = (Window)m->data.l[0];
	if (src != xdnd.source || xdnd.type == None) {
		/* Nothing we can take: acknowledge with failure. */
		if (src)
			xdnd_send(src, xdnd_finished, 0, 0, 0, 0, 0);
		xdnd_reset();
		return;
	}
	Time t = (Time)m->data.l[2];
	XConvertSelection(xdisplay, xdnd_selection, xdnd.type, clip_prop,
			  xwindow, t);
	XFlush(xdisplay);
	/* Data arrives via SelectionNotify on xdnd_selection. */
}

/* SelectionNotify whose selection is XdndSelection: the post-drop data. */
static void
xdnd_handle_selection(const XSelectionEvent *sel)
{
	if (sel->property == None) {
		xdnd_deliver(NULL, 0); /* refused: finish with failure */
		return;
	}
	if (clip_probe_type() == incr_atom) {
		clip_in_begin(CLIP_IN_DROP);
		return; /* completion in the PropertyNotify handler */
	}
	size_t len = 0;
	unsigned char *data = clip_read_prop(&len);
	xdnd_deliver(data, len);
}

/* Finish a drop whose data came over INCR. */
static void
xdnd_finish_drop(void)
{
	unsigned char *data = clip_in.buf;
	size_t len = clip_in.len;
	clip_in.buf = NULL;
	clip_in.active = 0;
	xdnd_deliver(data, len);
}

/* ---- Drag and drop (XDND drag source) ---- */

/* The XDND version a window advertises, or 0 if it is not a drop target. */
static int
win_xdnd_version(Window w)
{
	Atom type;
	int fmt;
	unsigned long nitems, after;
	unsigned char *data = NULL;
	int ver = 0;

	if (XGetWindowProperty(xdisplay, w, xdnd_aware, 0, 1, False, XA_ATOM,
			       &type, &fmt, &nitems, &after, &data) == Success) {
		if (data && type == XA_ATOM && nitems >= 1)
			ver = (int)(((long *)data)[0]);
		if (data)
			XFree(data);
	}
	return ver;
}

/* Find the XDND-aware window at root coordinates (rx, ry): descend to the
 * deepest window under the point, then walk up to the first aware ancestor
 * (aware lives on the toplevel).  Skips our own window. */
static Window
drag_find_target(int rx, int ry, int *ver_out)
{
	Window root = DefaultRootWindow(xdisplay);
	Window w = root, child;
	int wx, wy;

	for (;;) {
		if (!XTranslateCoordinates(xdisplay, root, w, rx, ry, &wx, &wy, &child))
			break;
		if (!child)
			break;
		w = child;
	}

	for (Window cur = w; cur && cur != root; ) {
		int ver = win_xdnd_version(cur);
		if (ver > 0 && cur != xwindow) {
			if (ver_out)
				*ver_out = ver;
			return cur;
		}
		Window r, parent, *kids = NULL;
		unsigned n = 0;
		if (!XQueryTree(xdisplay, cur, &r, &parent, &kids, &n))
			break;
		if (kids)
			XFree(kids);
		cur = parent;
	}
	if (ver_out)
		*ver_out = 0;
	return 0;
}

/* Announce our offered types to a new target.  Up to three ride in the
 * XdndEnter message; more are published in the XdndTypeList property with the
 * "more than three" flag set. */
static void
drag_send_enter(Window t)
{
	long flag = drag_count > 3 ? 1 : 0;
	if (flag) {
		Atom list[CLIP_MAX_OFFERS];
		for (int i = 0; i < drag_count; i++)
			list[i] = drag_offers[i].target;
		XChangeProperty(xdisplay, xwindow, xdnd_type_list, XA_ATOM, 32,
				PropModeReplace, (unsigned char *)list, drag_count);
	}
	long t0 = drag_count > 0 ? (long)drag_offers[0].target : 0;
	long t1 = drag_count > 1 ? (long)drag_offers[1].target : 0;
	long t2 = drag_count > 2 ? (long)drag_offers[2].target : 0;
	xdnd_send(t, xdnd_enter, 0,
		  (long)((unsigned long)XDND_VERSION << 24) | flag, t0, t1, t2);
}

/* Update the drag as the pointer moves to root coordinates (rx, ry): switch
 * target windows with XdndLeave/XdndEnter and send XdndPosition. */
static void
drag_update(int rx, int ry)
{
	int ver = 0;
	Window t = drag_find_target(rx, ry, &ver);

	if (t != drag.over) {
		if (drag.over)
			xdnd_send(drag.over, xdnd_leave, 0, 0, 0, 0, 0);
		drag.over = t;
		drag.over_version = ver;
		drag.accepted = 0;
		if (t)
			drag_send_enter(t);
	}
	if (drag.over)
		xdnd_send(drag.over, xdnd_position, 0, 0,
			  ((long)rx << 16) | (ry & 0xffff),
			  (long)CurrentTime, (long)xdnd_action_copy);
}

/* End the drag and report the outcome to the app. */
static void
drag_end(int accepted)
{
	drag_offers_free();
	memset(&drag, 0, sizeof(drag));

	lud_event_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = LUD_EV_DRAG_END;
	ev.drag_end.accepted = accepted;
	lud__event_push(&ev);
}

/* Pointer released during a drag: drop on the target if it accepts, else
 * cancel.  A successful drop keeps the drag alive to serve the data until
 * XdndFinished (or the deadline). */
static void
drag_release(void)
{
	XUngrabPointer(xdisplay, CurrentTime);

	if (drag.over && drag.accepted) {
		xdnd_send(drag.over, xdnd_drop, 0, 0, (long)CurrentTime, 0, 0);
		drag.dropping = 1;
		gettimeofday(&drag.deadline, NULL);
		drag.deadline.tv_sec += 5; /* bound the wait for XdndFinished */
	} else {
		if (drag.over)
			xdnd_send(drag.over, xdnd_leave, 0, 0, 0, 0, 0);
		drag_end(0);
	}
	XFlush(xdisplay);
}

int
lud_drag_multi(const lud_clip_item_t *items, int count)
{
	if (!xdisplay || drag.active)
		return LUD_ERR;
	if (!items || count <= 0 || count > CLIP_MAX_OFFERS)
		return LUD_ERR;

	/* Copy the payloads into a scratch array; only commit them once we own
	 * the selection and hold the pointer grab. */
	struct clip_offer tmp[CLIP_MAX_OFFERS];
	int n = 0;
	for (int i = 0; i < count; i++) {
		size_t len = items[i].len;
		unsigned char *copy = malloc(len + 1);
		if (!copy) {
			for (int j = 0; j < n; j++)
				free(tmp[j].data);
			return LUD_ERR;
		}
		if (len)
			memcpy(copy, items[i].data, len);
		copy[len] = 0;
		tmp[n].target = clip_format_to_target(items[i].format);
		tmp[n].data = copy;
		tmp[n].len = len;
		n++;
	}

	XSetSelectionOwner(xdisplay, xdnd_selection, xwindow, CurrentTime);
	if (XGetSelectionOwner(xdisplay, xdnd_selection) != xwindow ||
	    XGrabPointer(xdisplay, xwindow, False,
			 ButtonReleaseMask | PointerMotionMask,
			 GrabModeAsync, GrabModeAsync, None, None,
			 CurrentTime) != GrabSuccess) {
		XSetSelectionOwner(xdisplay, xdnd_selection, None, CurrentTime);
		for (int j = 0; j < n; j++)
			free(tmp[j].data);
		return LUD_ERR;
	}

	drag_offers_free();
	for (int i = 0; i < n; i++)
		drag_offers[i] = tmp[i];
	drag_count = n;
	drag.active = 1;

	/* Kick off the handshake from the pointer's current position. */
	Window root_ret, child;
	int rx, ry, wx, wy;
	unsigned mask;
	XQueryPointer(xdisplay, xwindow, &root_ret, &child, &rx, &ry,
		      &wx, &wy, &mask);
	drag_update(rx, ry);
	XFlush(xdisplay);
	return LUD_OK;
}

int
lud_drag_data(const char *format, const void *data, size_t len)
{
	lud_clip_item_t item = { format, data, len };
	return lud_drag_multi(&item, 1);
}

int
lud_drag_files(const char *const *paths, int count)
{
	size_t len = 0;
	char *buf = lud__uri_list_encode(paths, count, &len);
	if (!buf)
		return LUD_ERR;
	int rc = lud_drag_data(LUD_CLIPBOARD_URI_LIST, buf, len);
	free(buf);
	return rc;
}

void
lud__platform_poll_events(void)
{
	XEvent xev;
	lud_event_t ev;

	/* Release last frame's drop payload; the app consumed it during the
	 * previous dispatch. */
	if (drop_data) {
		free(drop_data);
		drop_data = NULL;
		drop_len = 0;
		drop_format = NULL;
	}

	/* Give up on a drop whose target never sent XdndFinished. */
	if (drag.active && drag.dropping) {
		struct timeval now;
		gettimeofday(&now, NULL);
		if (now.tv_sec > drag.deadline.tv_sec ||
		    (now.tv_sec == drag.deadline.tv_sec &&
		     now.tv_usec >= drag.deadline.tv_usec))
			drag_end(0);
	}

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
			if (drag.active && !drag.dropping)
				drag_update(xev.xmotion.x_root, xev.xmotion.y_root);
			ev.type = LUD_EV_MOUSE_MOVE;
			ev.modifiers = translate_modifiers(xev.xmotion.state);
			ev.mouse_move.x = xev.xmotion.x;
			ev.mouse_move.y = xev.xmotion.y;
			lud__event_push(&ev);
			break;

		case ButtonPress:
		case ButtonRelease: {
			unsigned btn = xev.xbutton.button;

			/* A button release ends an active drag we started. */
			if (xev.type == ButtonRelease && drag.active && !drag.dropping)
				drag_release();

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
			} else if (xev.xclient.message_type == xdnd_enter) {
				xdnd_handle_enter(&xev.xclient);
			} else if (xev.xclient.message_type == xdnd_position) {
				xdnd_handle_position(&xev.xclient);
			} else if (xev.xclient.message_type == xdnd_leave) {
				xdnd_reset();
			} else if (xev.xclient.message_type == xdnd_drop) {
				xdnd_handle_drop(&xev.xclient);
			} else if (xev.xclient.message_type == xdnd_status) {
				/* Source side: the target reports whether it accepts. */
				if (drag.active &&
				    (Window)xev.xclient.data.l[0] == drag.over)
					drag.accepted =
						(xev.xclient.data.l[1] & 1) != 0;
			} else if (xev.xclient.message_type == xdnd_finished) {
				/* Source side: the target finished the transfer. */
				if (drag.active &&
				    (Window)xev.xclient.data.l[0] == drag.over) {
					int ok = drag.over_version >= 5
						? (int)(xev.xclient.data.l[1] & 1)
						: 1;
					drag_end(ok);
				}
			}
			break;

		case SelectionRequest:
			clip_handle_request(&xev.xselectionrequest);
			break;

		case SelectionNotify:
			if (xev.xselection.selection == xdnd_selection)
				xdnd_handle_selection(&xev.xselection);
			else if (clip_req.active &&
				 xev.xselection.selection == clipboard_atom)
				clip_deliver(&xev.xselection);
			break;

		case SelectionClear:
			/* Lost ownership of a selection; drop our cached copy. */
			if (xev.xselectionclear.selection == clipboard_atom)
				clip_offers_free();
			break;

		case PropertyNotify:
			/* Incoming INCR chunk on our window (clipboard or drop). */
			if (clip_in.active && clip_in.mode != CLIP_IN_SYNC &&
			    xev.xproperty.atom == clip_prop &&
			    xev.xproperty.state == PropertyNewValue) {
				int mode = clip_in.mode;
				if (clip_in_step()) {
					if (mode == CLIP_IN_DROP)
						xdnd_finish_drop();
					else
						clip_in_finish_async();
				}
			}
			/* Requestor consumed a chunk of our outgoing INCR send. */
			else if (clip_out.active &&
				 xev.xproperty.window == clip_out.requestor &&
				 xev.xproperty.atom == clip_out.prop &&
				 xev.xproperty.state == PropertyDelete) {
				clip_out_step();
			}
			break;

		default:
			break;
		}
	}
}

void
lud__platform_set_fullscreen(int fullscreen)
{
	if (!xdisplay || !xwindow)
		return;

	if (fullscreen) {
		/* Save current windowed position and size */
		XWindowAttributes wa;
		XGetWindowAttributes(xdisplay, xwindow, &wa);

		Window child;
		int abs_x, abs_y;
		XTranslateCoordinates(xdisplay, xwindow,
			DefaultRootWindow(xdisplay),
			0, 0, &abs_x, &abs_y, &child);

		lud__state.saved_x = abs_x - wa.x;
		lud__state.saved_y = abs_y - wa.y;
		lud__state.saved_w = wa.width;
		lud__state.saved_h = wa.height;
	}

	/* Send _NET_WM_STATE client message to the window manager */
	XEvent xev;
	memset(&xev, 0, sizeof(xev));
	xev.type = ClientMessage;
	xev.xclient.window = xwindow;
	xev.xclient.message_type = net_wm_state;
	xev.xclient.format = 32;
	xev.xclient.data.l[0] = fullscreen ? 1 : 0; /* _NET_WM_STATE_ADD / _REMOVE */
	xev.xclient.data.l[1] = (long)net_wm_state_fullscreen;
	xev.xclient.data.l[2] = 0;
	xev.xclient.data.l[3] = 1; /* source = normal application */

	XSendEvent(xdisplay, DefaultRootWindow(xdisplay), False,
		SubstructureNotifyMask | SubstructureRedirectMask, &xev);
	XFlush(xdisplay);
}

void
lud__platform_swap(void)
{
	eglSwapBuffers(egl_display, egl_surface);
}
