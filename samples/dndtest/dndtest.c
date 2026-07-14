/*
 * dndtest — drag-and-drop (XDND) drop-target sample and self-test.
 *
 * Interactive: drag files from a file manager onto the window; each drop is
 * printed. Drops of text or a PNG image are reported by type and size.
 *
 * Run --selftest to drive a full synthetic drag-and-drop and exit non-zero on
 * failure (used by 'make run-tests').
 *
 * The self-test opens a second, independent X11 connection in the same process
 * to act as an XDND drag source. It finds ludica's window by its XdndAware
 * property, then performs the real protocol handshake (XdndEnter, XdndPosition,
 * XdndStatus, XdndDrop, the XdndSelection transfer, XdndFinished), so the drop
 * path is exercised end to end against an independent implementation.
 *
 * Made by a machine. PUBLIC DOMAIN (CC0-1.0)
 */

#include <ludica.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <limits.h>

/* A uri-list offering two files, with a space and a non-ASCII byte
 * percent-encoded, so the round trip proves decoding. */
#define DROP_URI_LIST \
	"file:///tmp/drop%20one.txt\r\n" \
	"file:///home/u/pi%C3%A7.png\r\n"
static const char *const expect_paths[] = {
	"/tmp/drop one.txt",
	"/home/u/pi\xC3\xA7.png",
};
#define EXPECT_N ((int)(sizeof(expect_paths) / sizeof(expect_paths[0])))

/* A large binary "image" payload, over one property's max size, to force the
 * INCR path in the drop transfer. */
#define IMG_LEN 200000
static unsigned char img_blob[IMG_LEN];

static unsigned long
checksum(const unsigned char *p, size_t n)
{
	unsigned long s = 1469598103934665603UL; /* FNV-ish, just a digest */
	for (size_t i = 0; i < n; i++)
		s = (s ^ p[i]) * 1099511628211UL;
	return s;
}

static int selftest;
static int failures;
static int frames;

/* ---- drop results recorded by the ludica event handler ---- */

static int   got_drop;
static char  got_format[64];
static char *got_paths[8];
static int   got_paths_n;
static int   got_x, got_y;
static size_t        got_data_len;
static unsigned long got_data_sum;

static int
on_event(const lud_event_t *ev)
{
	if (ev->type != LUD_EV_DROP)
		return 0;

	got_drop = 1;
	got_x = ev->drop.x;
	got_y = ev->drop.y;
	got_data_len = ev->drop.len;
	got_data_sum = checksum(ev->drop.data, ev->drop.len);
	snprintf(got_format, sizeof(got_format), "%s", ev->drop.format);

	if (!strcmp(ev->drop.format, LUD_CLIPBOARD_URI_LIST)) {
		char **files = lud_parse_uri_list(ev->drop.data, ev->drop.len);
		for (int i = 0; files && files[i]; i++) {
			if (got_paths_n < (int)(sizeof(got_paths) / sizeof(got_paths[0])))
				got_paths[got_paths_n++] = strdup(files[i]);
			free(files[i]);
		}
		free(files);
	}

	if (!selftest) {
		printf("drop: format=%s len=%zu at (%d,%d)\n", ev->drop.format,
		       ev->drop.len, ev->drop.x, ev->drop.y);
		for (int i = 0; i < got_paths_n; i++)
			printf("  file: %s\n", got_paths[i]);
		fflush(stdout);
	}
	return 1;
}

/* ---- second X client: an XDND drag source ---- */

static Display *dpy_b;
static Window   win_b;
static Window   target;      /* ludica's window, found by XdndAware */
static Atom     xa_aware, xa_enter, xa_position, xa_status, xa_drop,
		xa_finished, xa_selection, xa_action_copy, xa_uri_list,
		xa_png, xa_incr;
static int      b_status_seen, b_status_accept;
static int      b_finished_seen;

/* What B currently offers on XdndSelection. */
static Atom                 b_offer_target;
static const unsigned char *b_offer_data;
static size_t               b_offer_len;

/* INCR send state (B as selection owner, for large offers). */
static int    b_out_active;
static Window b_out_req;
static Atom   b_out_prop;
static size_t b_out_sent;

static size_t
b_chunk_max(void)
{
	long units = XExtendedMaxRequestSize(dpy_b);
	if (units <= 0)
		units = XMaxRequestSize(dpy_b);
	size_t bytes = (size_t)units * 4;
	if (bytes < 4096)
		bytes = 4096;
	return bytes / 4;
}

static int
client_b_init(void)
{
	dpy_b = XOpenDisplay(NULL);
	if (!dpy_b)
		return -1;
	win_b = XCreateSimpleWindow(dpy_b, DefaultRootWindow(dpy_b),
				    0, 0, 1, 1, 0, 0, 0);
	xa_aware       = XInternAtom(dpy_b, "XdndAware", False);
	xa_enter       = XInternAtom(dpy_b, "XdndEnter", False);
	xa_position    = XInternAtom(dpy_b, "XdndPosition", False);
	xa_status      = XInternAtom(dpy_b, "XdndStatus", False);
	xa_drop        = XInternAtom(dpy_b, "XdndDrop", False);
	xa_finished    = XInternAtom(dpy_b, "XdndFinished", False);
	xa_selection   = XInternAtom(dpy_b, "XdndSelection", False);
	xa_action_copy = XInternAtom(dpy_b, "XdndActionCopy", False);
	xa_uri_list    = XInternAtom(dpy_b, "text/uri-list", False);
	xa_png         = XInternAtom(dpy_b, "image/png", False);
	xa_incr        = XInternAtom(dpy_b, "INCR", False);
	return 0;
}

/* Does window w advertise XDND support? */
static int
is_aware(Window w)
{
	Atom type;
	int fmt;
	unsigned long nitems, after;
	unsigned char *data = NULL;
	int aware = 0;

	if (XGetWindowProperty(dpy_b, w, xa_aware, 0, 1, False, AnyPropertyType,
			       &type, &fmt, &nitems, &after, &data) == Success) {
		if (data && nitems >= 1)
			aware = 1;
		if (data)
			XFree(data);
	}
	return aware;
}

/* Is w one of ludica's dndtest windows (by WM_NAME)?  Disambiguates from other
 * XDND-aware apps on a shared display. */
static int
is_dndtest(Window w)
{
	char *name = NULL;
	int match = 0;

	if (XFetchName(dpy_b, w, &name) && name) {
		match = strstr(name, "dndtest") != NULL;
		XFree(name);
	}
	if (!match) {
		XTextProperty tp;
		if (XGetWMName(dpy_b, w, &tp) && tp.value) {
			match = strstr((char *)tp.value, "dndtest") != NULL;
			XFree(tp.value);
		}
	}
	return match;
}

/* Recursively search the window tree for ludica's XDND-aware dndtest window.
 * Recursion (rather than just root's children) survives a window manager
 * reparenting the top-level window into a decoration frame. */
static Window
search_tree(Window w)
{
	if (w != win_b && is_aware(w) && is_dndtest(w))
		return w;

	Window r, parent, *kids = NULL;
	unsigned n = 0;
	Window found = 0;
	if (XQueryTree(dpy_b, w, &r, &parent, &kids, &n)) {
		for (unsigned i = 0; i < n && !found; i++)
			if (kids[i] != win_b)
				found = search_tree(kids[i]);
		if (kids)
			XFree(kids);
	}
	return found;
}

static Window
find_target(void)
{
	return search_tree(DefaultRootWindow(dpy_b));
}

static void
b_send(Window to, Atom msg, long d1, long d2, long d3, long d4)
{
	XClientMessageEvent m;
	memset(&m, 0, sizeof(m));
	m.type = ClientMessage;
	m.display = dpy_b;
	m.window = to;
	m.message_type = msg;
	m.format = 32;
	m.data.l[0] = (long)win_b;
	m.data.l[1] = d1;
	m.data.l[2] = d2;
	m.data.l[3] = d3;
	m.data.l[4] = d4;
	XSendEvent(dpy_b, to, False, NoEventMask, (XEvent *)&m);
	XFlush(dpy_b);
}

/* Send the next INCR chunk (or the terminating zero-length write) to the
 * requestor, driven by PropertyDelete. */
static void
b_out_step(void)
{
	size_t chunk = b_chunk_max();
	size_t rem = b_offer_len - b_out_sent;
	size_t n = rem < chunk ? rem : chunk;

	XChangeProperty(dpy_b, b_out_req, b_out_prop, b_offer_target, 8,
			PropModeReplace, b_offer_data + b_out_sent, (int)n);
	b_out_sent += n;
	XFlush(dpy_b);

	if (n == 0) {
		XSelectInput(dpy_b, b_out_req, NoEventMask);
		b_out_active = 0;
	}
}

/* Serve the target's request for our XdndSelection data. */
static void
b_serve(const XSelectionRequestEvent *req)
{
	XSelectionEvent notify;
	memset(&notify, 0, sizeof(notify));
	notify.type = SelectionNotify;
	notify.display = req->display;
	notify.requestor = req->requestor;
	notify.selection = req->selection;
	notify.target = req->target;
	notify.time = req->time;
	notify.property = None;

	if (req->selection == xa_selection && req->target == b_offer_target) {
		Atom prop = req->property != None ? req->property : req->target;
		if (b_offer_len <= b_chunk_max()) {
			XChangeProperty(dpy_b, req->requestor, prop, b_offer_target,
					8, PropModeReplace, b_offer_data,
					(int)b_offer_len);
			notify.property = prop;
		} else if (!b_out_active) {
			long hint = (long)b_offer_len;
			XChangeProperty(dpy_b, req->requestor, prop, xa_incr, 32,
					PropModeReplace, (unsigned char *)&hint, 1);
			XSelectInput(dpy_b, req->requestor, PropertyChangeMask);
			b_out_active = 1;
			b_out_req = req->requestor;
			b_out_prop = prop;
			b_out_sent = 0;
			notify.property = prop;
		}
	}
	XSendEvent(dpy_b, req->requestor, False, 0, (XEvent *)&notify);
	XFlush(dpy_b);
}

static void
client_b_pump(void)
{
	while (XPending(dpy_b)) {
		XEvent e;
		XNextEvent(dpy_b, &e);
		if (e.type == SelectionRequest) {
			b_serve(&e.xselectionrequest);
		} else if (e.type == PropertyNotify) {
			if (b_out_active && e.xproperty.window == b_out_req &&
			    e.xproperty.atom == b_out_prop &&
			    e.xproperty.state == PropertyDelete)
				b_out_step();
		} else if (e.type == ClientMessage) {
			if (e.xclient.message_type == xa_status) {
				b_status_seen = 1;
				b_status_accept = (e.xclient.data.l[1] & 1) != 0;
			} else if (e.xclient.message_type == xa_finished) {
				b_finished_seen = 1;
			}
		}
	}
}

/* ---- self-test state machine ---- */

enum { ST_START, ST_WAIT_STATUS, ST_WAIT_FINISH, ST_VERIFY, ST_DONE };
static int state = ST_START;
static int state_frame;
static int phase; /* 0 = file list (single-shot), 1 = image (INCR) */

#define WAIT_LIMIT 300

static void
fail(const char *msg)
{
	printf("FAIL %s\n", msg);
	fflush(stdout);
	failures++;
	state = ST_DONE;
}

/* Offer `type` on XdndSelection and start a drag over the target: own the
 * selection, then send XdndEnter and one XdndPosition. */
static void
begin_drag(Atom type, const unsigned char *data, size_t len)
{
	b_offer_target = type;
	b_offer_data = data;
	b_offer_len = len;
	XSetSelectionOwner(dpy_b, xa_selection, win_b, CurrentTime);
	XSync(dpy_b, False);
	got_drop = 0;

	b_send(target, xa_enter, (long)(5U << 24), (long)type, 0, 0);

	int rx, ry;
	Window child;
	XTranslateCoordinates(dpy_b, target, DefaultRootWindow(dpy_b),
			      10, 10, &rx, &ry, &child);
	b_send(target, xa_position, 0, ((long)rx << 16) | (ry & 0xffff),
	       CurrentTime, (long)xa_action_copy);
	b_status_seen = 0;
}

static void
selftest_step(void)
{
	client_b_pump();

	switch (state) {
	case ST_START:
		if (frames < 3)
			return; /* let ludica map its window and set XdndAware */
		target = find_target();
		if (!target) {
			fail("could not find XDND-aware target window");
			return;
		}
		phase = 0;
		begin_drag(xa_uri_list, (const unsigned char *)DROP_URI_LIST,
			   strlen(DROP_URI_LIST));
		state = ST_WAIT_STATUS;
		state_frame = frames;
		break;

	case ST_WAIT_STATUS:
		if (b_status_seen) {
			if (!b_status_accept) {
				fail("target refused the drop");
				return;
			}
			b_finished_seen = 0;
			b_send(target, xa_drop, 0, CurrentTime, 0, 0);
			state = ST_WAIT_FINISH;
			state_frame = frames;
		} else if (frames - state_frame > WAIT_LIMIT) {
			fail("no XdndStatus from target");
		}
		break;

	case ST_WAIT_FINISH:
		/* The drop has been delivered to the app once got_drop is set;
		 * XdndFinished confirms the source side saw completion. */
		if (got_drop && b_finished_seen) {
			state = ST_VERIFY;
		} else if (frames - state_frame > WAIT_LIMIT) {
			fail("drop did not complete");
		}
		break;

	case ST_VERIFY:
		if (phase == 0) {
			int ok = !strcmp(got_format, LUD_CLIPBOARD_URI_LIST) &&
				 got_paths_n == EXPECT_N;
			for (int i = 0; ok && i < EXPECT_N; i++)
				ok = !strcmp(got_paths[i], expect_paths[i]);
			printf("%-6s drop-files format=%s files=%d/%d at (%d,%d)\n",
			       ok ? "PASS" : "FAIL", got_format, got_paths_n,
			       EXPECT_N, got_x, got_y);
			fflush(stdout);
			if (!ok)
				failures++;

			/* Phase 1: a large image drop, forcing INCR. */
			phase = 1;
			begin_drag(xa_png, img_blob, IMG_LEN);
			state = ST_WAIT_STATUS;
			state_frame = frames;
		} else {
			int ok = !strcmp(got_format, LUD_CLIPBOARD_PNG) &&
				 got_data_len == IMG_LEN &&
				 got_data_sum == checksum(img_blob, IMG_LEN);
			printf("%-6s drop-image format=%s len=%zu/%d\n",
			       ok ? "PASS" : "FAIL", got_format, got_data_len,
			       IMG_LEN);
			fflush(stdout);
			if (!ok)
				failures++;
			state = ST_DONE;
		}
		break;

	case ST_DONE:
		printf("selftest: %d failure(s)\n", failures);
		fflush(stdout);
		lud_quit();
		break;
	}
}

/* ---- ludica callbacks ---- */

static void
init(void)
{
	selftest = lud_get_config("selftest") != NULL;
	if (selftest) {
		for (size_t i = 0; i < IMG_LEN; i++)
			img_blob[i] = (unsigned char)(i * 37 + 11);
		if (client_b_init() != 0) {
			printf("FAIL: second X connection unavailable\n");
			failures++;
			lud_quit();
		}
	} else {
		printf("dndtest: drag files onto the window (ESC to quit)\n");
		fflush(stdout);
	}
}

static void
frame(float dt)
{
	(void)dt;
	frames++;
	if (selftest)
		selftest_step();
	else if (lud_key_down(LUD_KEY_ESCAPE))
		lud_quit();
}

static void
cleanup(void)
{
	if (dpy_b) {
		XDestroyWindow(dpy_b, win_b);
		XCloseDisplay(dpy_b);
		dpy_b = NULL;
	}
	for (int i = 0; i < got_paths_n; i++)
		free(got_paths[i]);
}

int
main(int argc, char **argv)
{
	lud_run(&(lud_desc_t){
		.app_name = "dndtest",
		.width = 320, .height = 200,
		.init = init,
		.frame = frame,
		.cleanup = cleanup,
		.event = on_event,
		.argc = argc, .argv = argv,
	});
	return failures ? 1 : 0;
}
