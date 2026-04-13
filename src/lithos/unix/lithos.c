#include "lithos.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/select.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "log.h"
#include "report.h"


#ifndef EGL_OPENGL_ES3_BIT_KHR
#  define EGL_OPENGL_ES3_BIT_KHR 0x00000040
#endif

/**********************************************************************/

struct xwindow_info;
void surface_swap(struct xwindow_info *info);

/**********************************************************************/

int terminate_flag;
static fd_set readfds;
static int max_fd;
static const double max_frame_time = 1 / 60.0f;

/**********************************************************************/

static const
EGLint config_attribs[] = {
	EGL_SURFACE_TYPE, EGL_WINDOW_BIT, // | EGL_PBUFFER_BIT
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

static const
EGLint gles_attr_list[] = {
	EGL_CONTEXT_CLIENT_VERSION, 2,
	EGL_NONE
};

/**********************************************************************/

struct xwindow_info {
	struct xwindow_info *next;
	NativeWindowType native_window;
	EGLContext context;
	EGLSurface surface;
	int out_width, out_height;
	int resize_pending;
	int hidden;
	enum { GAME_MODE, KEYBOARD_MODE } input_mode;
	XComposeStatus compose_status;
	char input_buf[16];
	int input_buf_used;
	struct window_callback_functions callback;
};

typedef Window NativeWindowType;

static struct xwindow_info *current_xwindow_info;

static EGLDisplay display = EGL_NO_DISPLAY;
static EGLConfig config;
static int default_width = 640, default_height = 480;
static Display *xdisplay;
static char *atom_names[] = { "WM_PROTOCOLS", "WM_DELETE_WINDOW" };
#define NUM_ATOMS (sizeof(atom_names) / sizeof(*atom_names))
static Atom atoms[NUM_ATOMS];

#define WM_PROTOCOLS atoms[0]
#define WM_DELETE_WINDOW atoms[1]

static NativeWindowType
native_window_create(int width, int height)
{
	Screen *screen = DefaultScreenOfDisplay(xdisplay);
	Window root = RootWindowOfScreen(screen);
	unsigned long black = BlackPixelOfScreen(screen);
	char *title = "TODO: name this window";
	XTextProperty window_name;

	XSetWindowAttributes wattr = {
		.event_mask = StructureNotifyMask | KeyPressMask,
		// .background_pixmap = None,
		.background_pixel = black,
		.border_pixel = black,
		// .colormap = XCreateColormap(xdpy, root, xvi->visual, AllocNone),
	};

	int x = (WidthOfScreen(screen) - width) / 2;
	int y = (HeightOfScreen(screen) - height) / 2;
	Window win = XCreateWindow(xdisplay, root, x, y, width, height, 0,
				   CopyFromParent, InputOutput, CopyFromParent,
				   CWBorderPixel | CWEventMask, &wattr);

	Atom prots[] = { WM_DELETE_WINDOW };
	XSetWMProtocols(xdisplay, win, prots, sizeof(prots) / sizeof(*prots));

	// Pixmap icon = None; // TODO: load icon
	XSizeHints sizehints = {
		.flags = USSize | PSize | PMinSize | PMaxSize | PAspect,
		.x = 0, .y = 0, .width = width, .height = height,
		.min_width = width / 2, .min_height = height / 2,
		.max_width = width * 4, .max_height = height * 4,
		.min_aspect.x = width, .min_aspect.y = height,
		.max_aspect.x = width, .max_aspect.y = height,
		};
	XWMHints wm_hints = {};
	XClassHint class_hints = {};
	Xutf8TextListToTextProperty(xdisplay, &title, 1, XTextStyle, &window_name);

	XSetWMProperties(xdisplay, win, &window_name, &window_name, NULL, 0, &sizehints, &wm_hints, &class_hints);
	XFree(window_name.value);
	window_name.value = NULL;

	XMapWindow(xdisplay, win);

	return win;
}

int
display_init(void)
{
	//// X11 ////

	xdisplay = XOpenDisplay(NULL);
	if (!xdisplay) {
		return LITHOS_ERR;
	}

	XInternAtoms(xdisplay, atom_names, NUM_ATOMS, True, atoms);

	//// EGL ////

	display = eglGetDisplay(xdisplay);
	if (display == EGL_NO_DISPLAY) {
		report_error("No EGL display available.");
		XCloseDisplay(xdisplay);
		return LITHOS_ERR;
	}

	EGLint major, minor;
	if (eglInitialize(display, &major, &minor) == EGL_FALSE) {
		report_error("Unable to initialize EGL.");
		eglTerminate(display);
		XCloseDisplay(xdisplay);
		return LITHOS_ERR;
	}

	log_debug("EGL %u.%u version: %s (%s)", major, minor,
		eglQueryString(display, EGL_VERSION),
		eglQueryString(display, EGL_VENDOR));

	// default: eglBindAPI(EGL_OPENGL_ES_API);

#if 0 // DEBUG
	/* Dump some information about the EGL and GLES we've initialized */
	EGLint num_config;
	EGLBoolean result = eglGetConfigs(display, NULL, 0, &num_config);
	if (!result || !num_config) {
		report_error("No EGL configs available");
		terminate_flag = 1;
		return LITHOS_ERR;
	}

	EGLConfig *configs = malloc(sizeof(EGLConfig) * num_config);
	if (!configs) {
		report_error("Out of memory for EGL configs");
		terminate_flag = 1;
		return LITHOS_ERR;
	}
	result = eglGetConfigs(display, configs, num_config, &num_config);
	if (!result || !num_config) {
		report_error("No EGL configs available");
		terminate_flag = 1;
		return LITHOS_ERR;
	}

	int i;
	for (i = 0; i < num_config; i++) {
		log_debug("Config #%u", i);
		EGLint value;
#define SHOW(x) \
		if (eglGetConfigAttrib(display, configs[i], x, &value) == EGL_TRUE) { \
			log_debug("    " #x "=%d", value); \
		} else { \
			log_debug("    " #x "=ERROR"); \
		}

		SHOW(EGL_CONFIG_ID);
		SHOW(EGL_RENDERABLE_TYPE);
		SHOW(EGL_ALPHA_SIZE);
		SHOW(EGL_RED_SIZE);
		SHOW(EGL_GREEN_SIZE);
		SHOW(EGL_BLUE_SIZE);
#undef SHOW
	}


	free(configs);
#endif

	FD_ZERO(&readfds);
	max_fd = XConnectionNumber(xdisplay);

	return LITHOS_OK;
}

/* NOTE: window cannot be current */
void
window_free(struct xwindow_info *info)
{
	if (!display || display == EGL_NO_DISPLAY) {
		return;
	}
	if (!info) {
		return;
	}
	if (info->context) {
		eglDestroyContext(display, info->context);
		info->context = EGL_NO_CONTEXT;
	}
	if (info->surface) {
		eglDestroySurface(display, info->surface);
		info->surface = EGL_NO_SURFACE;
	}
	if (xdisplay && info->native_window) {
		XDestroyWindow(xdisplay, info->native_window);
		info->native_window = None;
	}
	free(info);
}

int
window_new(const struct window_callback_functions *callbacks)
{
	EGLint num_config;
	EGLBoolean result = eglChooseConfig(display, config_attribs, &config, 1, &num_config);
	if (!result || !num_config) {
		report_error("No EGL configs available");
		terminate_flag = 1;
		return LITHOS_ERR;
	}
	struct xwindow_info *info = malloc(sizeof(*info));
	*info = (struct xwindow_info){
		.next = current_xwindow_info,
		.out_width = default_width,
		.out_height = default_height,
		.callback = *callbacks,
	};
	info->native_window = native_window_create(info->out_width, info->out_height);
	info->surface = eglCreateWindowSurface(display, config, info->native_window, NULL);
	info->context = eglCreateContext(display, config, EGL_NO_CONTEXT, gles_attr_list);
	eglMakeCurrent(display, info->surface, info->surface, info->context),
	current_xwindow_info = info;

	log_debug("OpenGL version: %s (%s)", glGetString(GL_VERSION), glGetString(GL_VENDOR));

	return LITHOS_OK;
}

unsigned long long
clock_now(void)
{
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC, &tp);
	unsigned long long t = tp.tv_sec * 1000000000UL + tp.tv_nsec;
	return t;
}

/* find the difference between two monotonic timestamps, return is in seconds. */
double
clock_diff(unsigned long long t1, unsigned long long t0)
{
	return (double)((t1 - t0) / 1e9);
}

static void
lithos_wait(void)
{
	static long long prev;
	unsigned long long now = clock_now();
	double sec = max_frame_time - clock_diff(now, prev);

	if (!prev || sec < 0) {
		sec = max_frame_time;
	}

	struct timeval tv = { sec, (sec - (unsigned)sec) * 1e6 };

	FD_SET(XConnectionNumber(xdisplay), &readfds);
	// log_debug("Waiting %d,%d", tv.tv_sec, tv.tv_usec);
	select(max_fd + 1, &readfds, NULL, NULL, &tv);
	prev = clock_now();
}

void
surface_swap(struct xwindow_info *info)
{
	lithos_wait();
	eglSwapBuffers(display, info->surface);
	XSync(xdisplay, False);
#if 0
	if (XEventsQueued(xdpy, QueuedAfterFlush)) {
		return; /* events are pending */
	}
#endif
}

void
display_done(void)
{
	if (display) {
		eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	}

	struct xwindow_info *info;
	while ((info = current_xwindow_info)) {
		current_xwindow_info = info->next;
		info->next = NULL;
		window_free(info);
	}
	current_xwindow_info = NULL;
	if (display != EGL_NO_DISPLAY) {
		eglTerminate(display);
		display = EGL_NO_DISPLAY;
	}
	if (xdisplay) {
		XCloseDisplay(xdisplay);
		xdisplay = NULL;
	}
}

void
end_loop(void)
{
	terminate_flag = 1;
}

void
check_resize(struct xwindow_info *info)
{
	if (info->resize_pending) {
		XWindowAttributes gwa;
		XGetWindowAttributes(xdisplay, info->native_window, &gwa);
		info->resize_pending = 0;
		info->out_width = gwa.width;
		info->out_height = gwa.height;
		if (info->callback.reshape) {
			info->callback.reshape(info->out_width, info->out_height);
		}
	}
}

static void
activate_xwindow_info(struct xwindow_info *info)
{
	eglMakeCurrent(display, info->surface, info->surface, info->context);
}

static struct xwindow_info *
activate_xwindow(Window win)
{
	struct xwindow_info *info;
	for (info = current_xwindow_info; info && info->native_window != win; info = info->next)
		;
	if (!info) return NULL;
	activate_xwindow_info(info);
	return info;
}

static void
handle_keyevent(struct xwindow_info *info, XKeyEvent *xkey)
{
	(void)info;
	if (info->input_mode == KEYBOARD_MODE) {
		KeySym sym;
		info->input_buf_used = XLookupString(xkey, info->input_buf, sizeof(info->input_buf) - 1, &sym, &info->compose_status);
		if (info->input_buf_used < 0) {
			// TODO: handle error
			info->input_buf_used = 0;
		}
		info->input_buf[info->input_buf_used] = 0;

		if (info->callback.typingkeyboard) {
			info->callback.typingkeyboard(info->input_buf, info->input_buf_used);
		}
	} else {
		static KeyCode escape = NoSymbol;
		if (escape == NoSymbol) {
			escape = XKeysymToKeycode(xdisplay, XK_Escape);
		}
		/*
		KeySym sym = XLookupKeysym(xkey, 0);
		log_debug("Key sym = %#x\n", sym);
		*/
		if (xkey->keycode == escape) {
			terminate_flag = 1;
		} else if (info->callback.keyevent) {
			info->callback.keyevent(xkey->keycode);
		}
	}
}

void
process_events(void)
{
	XEvent xev;
	struct xwindow_info *info = NULL;
	while (XPending(xdisplay)) {
		XNextEvent(xdisplay, &xev);
		if (XFilterEvent(&xev, None)) {
			continue; /* ignore possible input method events */
		}
		switch (xev.type) {
		case ReparentNotify:
			break;

		case MapNotify:
		case ConfigureNotify:
			info = activate_xwindow(xev.xany.window);
			if (!info) {
				continue;
			}
			info->resize_pending = 1;
			break;

		case Expose:
			if (xev.xexpose.count == 0) {
				info = activate_xwindow(xev.xany.window);
				if (!info) {
					continue;
				}
				check_resize(info);
				if (info->callback.paint) {
					info->callback.paint();
				}
				surface_swap(info);
			}
			break;

		case KeyPress:
		case KeyRelease:
			info = activate_xwindow(xev.xany.window);
			if (!info) {
				continue;
			}
			handle_keyevent(info, &xev.xkey);
			break;

		case ClientMessage:
			if (xev.xclient.message_type == WM_PROTOCOLS && (Atom)xev.xclient.data.l[0] == WM_DELETE_WINDOW) {
				end_loop();
			}
			break;

		default:
			log_debug("Ignoring event type %d", xev.type);
		}
	}
}

void
paint_all(void)
{
	struct xwindow_info *info;
	for (info = current_xwindow_info; info; info = info->next) {
		if (!info->hidden) {
			activate_xwindow_info(info);
			if (info->callback.paint) {
				info->callback.paint();
			}
			surface_swap(info);
		}
	}
}

int
lookup_key(const char *name)
{
	/* hack around a few names that don't have symbolic name */
	if (!strcasecmp(name, "Space")) {
		name = " ";
	}

	KeySym sym = XStringToKeysym(name);
	if (sym == NoSymbol)
		return -1;
	KeyCode code = XKeysymToKeycode(xdisplay, sym);
	if (code == 0)
		return -1;
	return code;
}

void
window_register_paint(window_paintfunc *f)
{
	current_xwindow_info->callback.paint = f;
}

void
window_register_idle(window_idlefunc *f)
{
	current_xwindow_info->callback.idle = f;
}

void
window_register_reshape(window_reshapefunc *f)
{
	current_xwindow_info->callback.reshape = f;
}

void
window_register_keyevent(window_keyeventfunc *f)
{
	current_xwindow_info->callback.keyevent = f;
}

void
window_register_typingkeyboard(window_typingkeyboardfunc *f)
{
	current_xwindow_info->callback.typingkeyboard = f;
}

#if 0 // TODO
/* return 0 to ignore, or non-zero if processed */
int event_parse_input_utf8(struct window *w, XEvent *ev,
	char *buf, size_t bufmax)
{
	KeySym sym;
	Status status;
	int buflen;

	if (ev->xkey.type != KeyPress)
		return -1;

	buflen = Xutf8LookupString(w->ic, &ev->xkey, buf, bufmax - 1,
		&sym, &status);
	if (buflen < 0) abort(); // TODO: implement error handler
	buf[buflen] = 0;

	if (status == XLookupChars || status == XLookupBoth)
		return buflen;
	// TODO: write a function that uses XLookupKeySym
	return 0;
}

/* return 0 to ignore, or non-zero if processed */
int event_parse_key(struct window *w, XEvent *ev, unsigned *keycode, int *down)
{
	if (ev->xkey.type == KeyPress || ev->xkey.type == KeyRelease) {
		if (down)
			*down = ev->xkey.type == KeyPress;
		if (keycode)
			*keycode = ev->xkey.keycode;
		return 1;
	}
	return 0;
}
#endif
