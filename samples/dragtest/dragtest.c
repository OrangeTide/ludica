/*
 * dragtest — drag-and-drop (XDND) drag-source sample and self-test.
 *
 * Run --selftest to start a drag out of the window and drive it to completion
 * against a synthetic drop target, exiting non-zero on failure (used by
 * 'make run-tests').
 *
 * The self-test opens a second, independent X11 connection in the same process
 * to act as an XDND drop target. It advertises XdndAware and runs the target
 * half of the protocol (XdndEnter/Position/Status/Drop, the XdndSelection
 * transfer, XdndFinished). The ludica side warps the pointer over the target,
 * calls the drag API, and synthesizes the button release that ends the drag,
 * so the drag-source path runs end to end against an independent target.
 *
 * Made by a machine. PUBLIC DOMAIN (CC0-1.0)
 */

#include <ludica.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

static const char *const drag_paths[] = {
	"/tmp/drag one.txt",
	"/home/u/\xC3\xA9.png",   /* space and non-ASCII exercise encoding */
};
#define DRAG_N ((int)(sizeof(drag_paths) / sizeof(drag_paths[0])))

/* Large binary "image" payload, over one property's max size, to force INCR. */
#define IMG_LEN 200000
static unsigned char img_blob[IMG_LEN];

static unsigned long
checksum(const unsigned char *p, size_t n)
{
	unsigned long s = 1469598103934665603UL;
	for (size_t i = 0; i < n; i++)
		s = (s ^ p[i]) * 1099511628211UL;
	return s;
}

static int selftest;
static int failures;
static int frames;

/* ---- drag outcome recorded by the ludica event handler ---- */

static int got_drag_end;
static int drag_end_accepted;

static int
on_event(const lud_event_t *ev)
{
	if (ev->type == LUD_EV_DRAG_END) {
		got_drag_end = 1;
		drag_end_accepted = ev->drag_end.accepted;
		return 1;
	}
	return 0;
}

/* ---- second X client: an XDND drop target ---- */

static Display *dpy_b;
static Window   win_b;
static Atom     xa_aware, xa_enter, xa_position, xa_status, xa_leave, xa_drop,
		xa_finished, xa_selection, xa_action_copy, xa_incr,
		xa_uri_list, xa_png, prop_b;

static Window   b_src;        /* the drag source (ludica's window) */
static Atom     b_type;       /* type we agreed to receive */
static int      b_saw_position;

/* What B received from a completed transfer. */
static unsigned char *b_recv;
static size_t         b_recv_len;
static int            b_recv_done;

/* INCR receive state. */
static int            b_in_active;
static unsigned char *b_in_buf;
static size_t         b_in_len, b_in_cap;

static int
client_b_init(void)
{
	dpy_b = XOpenDisplay(NULL);
	if (!dpy_b)
		return -1;
	/* An override-redirect window so no window manager reparents or moves
	 * it: it stays at (0,0), undecorated, exactly where the pointer is
	 * warped, and topmost once raised. */
	win_b = XCreateSimpleWindow(dpy_b, DefaultRootWindow(dpy_b),
				    0, 0, 320, 240, 0, 0, 0);
	XSetWindowAttributes wa;
	wa.override_redirect = True;
	XChangeWindowAttributes(dpy_b, win_b, CWOverrideRedirect, &wa);
	XSelectInput(dpy_b, win_b, PropertyChangeMask);

	xa_aware       = XInternAtom(dpy_b, "XdndAware", False);
	xa_enter       = XInternAtom(dpy_b, "XdndEnter", False);
	xa_position    = XInternAtom(dpy_b, "XdndPosition", False);
	xa_status      = XInternAtom(dpy_b, "XdndStatus", False);
	xa_leave       = XInternAtom(dpy_b, "XdndLeave", False);
	xa_drop        = XInternAtom(dpy_b, "XdndDrop", False);
	xa_finished    = XInternAtom(dpy_b, "XdndFinished", False);
	xa_selection   = XInternAtom(dpy_b, "XdndSelection", False);
	xa_action_copy = XInternAtom(dpy_b, "XdndActionCopy", False);
	xa_incr        = XInternAtom(dpy_b, "INCR", False);
	xa_uri_list    = XInternAtom(dpy_b, "text/uri-list", False);
	xa_png         = XInternAtom(dpy_b, "image/png", False);
	prop_b         = XInternAtom(dpy_b, "DRAGTEST_B", False);

	Atom version = 5;
	XChangeProperty(dpy_b, win_b, xa_aware, XA_ATOM, 32, PropModeReplace,
			(unsigned char *)&version, 1);
	XMapRaised(dpy_b, win_b);
	XSync(dpy_b, False);
	return 0;
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

/* Deliver a completed transfer: keep the bytes, then finish the drop. */
static void
b_complete(unsigned char *data, size_t len, int ok)
{
	free(b_recv);
	b_recv = data;
	b_recv_len = len;
	b_recv_done = 1;
	if (b_src)
		b_send(b_src, xa_finished, ok ? 1 : 0, (long)xa_action_copy, 0, 0);
}

/* Consume one INCR chunk (zero-length ends the transfer). */
static void
b_in_step(void)
{
	Atom type;
	int fmt;
	unsigned long nitems, after;
	unsigned char *p = NULL;

	if (XGetWindowProperty(dpy_b, win_b, prop_b, 0, LONG_MAX, True,
			       AnyPropertyType, &type, &fmt, &nitems, &after,
			       &p) != Success)
		return;
	size_t chunk = (size_t)nitems * (fmt / 8);
	if (chunk == 0) {
		if (p)
			XFree(p);
		b_in_active = 0;
		unsigned char *out = b_in_buf;
		size_t len = b_in_len;
		b_in_buf = NULL;
		b_in_len = 0;
		b_in_cap = 0;
		b_complete(out, len, 1);
		return;
	}
	if (b_in_len + chunk + 1 > b_in_cap) {
		size_t ncap = b_in_cap ? b_in_cap : 4096;
		while (ncap < b_in_len + chunk + 1)
			ncap *= 2;
		b_in_buf = realloc(b_in_buf, ncap);
		b_in_cap = ncap;
	}
	memcpy(b_in_buf + b_in_len, p, chunk);
	b_in_len += chunk;
	b_in_buf[b_in_len] = 0;
	XFree(p);
}

/* SelectionNotify: read the offered property, single-shot or INCR. */
static void
b_read_selection(const XSelectionEvent *sel)
{
	if (sel->property == None) {
		b_complete(NULL, 0, 0);
		return;
	}

	Atom type;
	int fmt;
	unsigned long nitems, after;
	unsigned char *data = NULL;

	/* Probe the type without consuming the value. */
	if (XGetWindowProperty(dpy_b, win_b, prop_b, 0, 0, False, AnyPropertyType,
			       &type, &fmt, &nitems, &after, &data) != Success)
		return;
	if (data) {
		XFree(data);
		data = NULL;
	}
	if (type == xa_incr) {
		b_in_active = 1;
		free(b_in_buf);
		b_in_buf = NULL;
		b_in_len = 0;
		b_in_cap = 0;
		XDeleteProperty(dpy_b, win_b, prop_b);
		XFlush(dpy_b);
		return; /* chunks arrive via PropertyNotify */
	}

	if (XGetWindowProperty(dpy_b, win_b, prop_b, 0, INT_MAX / 4, True,
			       AnyPropertyType, &type, &fmt, &nitems, &after,
			       &data) != Success)
		return;
	size_t len = (size_t)nitems * (fmt / 8);
	unsigned char *out = malloc(len + 1);
	if (out) {
		memcpy(out, data, len);
		out[len] = 0;
	}
	if (data)
		XFree(data);
	b_complete(out, out ? len : 0, out != NULL);
}

static void
client_b_pump(void)
{
	while (XPending(dpy_b)) {
		XEvent e;
		XNextEvent(dpy_b, &e);
		switch (e.type) {
		case ClientMessage:
			if (e.xclient.message_type == xa_enter) {
				b_src = (Window)e.xclient.data.l[0];
				b_type = None;
				for (int i = 2; i <= 4; i++) {
					Atom t = (Atom)e.xclient.data.l[i];
					if (t == xa_uri_list || t == xa_png) {
						b_type = t;
						break;
					}
				}
			} else if (e.xclient.message_type == xa_position) {
				b_saw_position = 1;
				int accept = b_type != None;
				b_send((Window)e.xclient.data.l[0], xa_status,
				       accept ? 1 : 0, 0, 0,
				       accept ? (long)xa_action_copy : 0);
			} else if (e.xclient.message_type == xa_leave) {
				b_type = None;
			} else if (e.xclient.message_type == xa_drop) {
				if (b_type == None) {
					b_send((Window)e.xclient.data.l[0],
					       xa_finished, 0, 0, 0, 0);
				} else {
					XConvertSelection(dpy_b, xa_selection,
							  b_type, prop_b, win_b,
							  (Time)e.xclient.data.l[2]);
					XFlush(dpy_b);
				}
			}
			break;
		case SelectionNotify:
			b_read_selection(&e.xselection);
			break;
		case PropertyNotify:
			if (b_in_active && e.xproperty.atom == prop_b &&
			    e.xproperty.state == PropertyNewValue)
				b_in_step();
			break;
		}
	}
}

/* Synthesize the button release that ends ludica's drag, delivered to the
 * source window (which we learned from XdndEnter). */
static void
b_send_release(void)
{
	XEvent e;
	memset(&e, 0, sizeof(e));
	e.type = ButtonRelease;
	e.xbutton.display = dpy_b;
	e.xbutton.window = b_src;
	e.xbutton.root = DefaultRootWindow(dpy_b);
	e.xbutton.subwindow = None;
	e.xbutton.time = CurrentTime;
	e.xbutton.x = 10;
	e.xbutton.y = 10;
	e.xbutton.x_root = 50;
	e.xbutton.y_root = 50;
	e.xbutton.state = Button1Mask;
	e.xbutton.button = Button1;
	e.xbutton.same_screen = True;
	XSendEvent(dpy_b, b_src, False, ButtonReleaseMask, &e);
	XFlush(dpy_b);
}

/* ---- self-test state machine ---- */

enum { ST_START, ST_BEGIN, ST_WAIT_ACCEPT, ST_WAIT_DONE, ST_VERIFY, ST_DONE };
static int state = ST_START;
static int phase; /* 0 = files (single-shot), 1 = image (INCR) */

#define WAIT_LIMIT 300

/* Frame bookkeeping for per-state timeouts. */
static int state_frame;
static void state_frame_reset(void) { state_frame = frames; }
static int  waited_too_long(void) { return frames - state_frame > WAIT_LIMIT; }

static void
reset_transfer(void)
{
	free(b_recv);
	b_recv = NULL;
	b_recv_len = 0;
	b_recv_done = 0;
	b_saw_position = 0;
	got_drag_end = 0;
	drag_end_accepted = 0;
}

/* Put the pointer over B's window so the drag finds it as the target. */
static void
warp_over_target(void)
{
	XWarpPointer(dpy_b, None, win_b, 0, 0, 0, 0, 50, 50);
	XSync(dpy_b, False);
}

static void
fail(const char *msg)
{
	printf("FAIL %s\n", msg);
	fflush(stdout);
	failures++;
	state = ST_DONE;
}

static void
selftest_step(void)
{
	client_b_pump();

	switch (state) {
	case ST_START:
		if (frames < 3)
			return; /* let both windows map */
		warp_over_target();
		state = ST_BEGIN;
		break;

	case ST_BEGIN: {
		reset_transfer();
		int rc = phase == 0
			? lud_drag_files(drag_paths, DRAG_N)
			: lud_drag_data(LUD_CLIPBOARD_PNG, img_blob, IMG_LEN);
		if (rc != 0) {
			fail("lud_drag_* failed to start");
			return;
		}
		state = ST_WAIT_ACCEPT;
		state_frame_reset();
		break;
	}

	case ST_WAIT_ACCEPT:
		/* B replies XdndStatus during the same pump that sets
		 * b_saw_position, so the release we send next is queued after
		 * it and ludica processes accept before release. */
		if (b_saw_position) {
			b_send_release();
			state = ST_WAIT_DONE;
		} else if (waited_too_long()) {
			fail("target never saw XdndPosition");
		}
		break;

	case ST_WAIT_DONE:
		if (got_drag_end && b_recv_done) {
			state = ST_VERIFY;
		} else if (waited_too_long()) {
			fail("drag did not complete");
		}
		break;

	case ST_VERIFY:
		if (phase == 0) {
			char **files = lud_parse_uri_list(b_recv, b_recv_len);
			int n = 0;
			if (files)
				while (files[n])
					n++;
			int ok = drag_end_accepted && n == DRAG_N;
			for (int i = 0; ok && i < DRAG_N; i++)
				ok = !strcmp(files[i], drag_paths[i]);
			printf("%-6s drag-files accepted=%d files=%d/%d\n",
			       ok ? "PASS" : "FAIL", drag_end_accepted, n, DRAG_N);
			fflush(stdout);
			if (!ok)
				failures++;
			for (int i = 0; files && files[i]; i++)
				free(files[i]);
			free(files);

			phase = 1;
			warp_over_target();
			state = ST_BEGIN;
		} else {
			int ok = drag_end_accepted && b_recv_len == IMG_LEN &&
				 checksum(b_recv, b_recv_len) ==
					 checksum(img_blob, IMG_LEN);
			printf("%-6s drag-image accepted=%d len=%zu/%d\n",
			       ok ? "PASS" : "FAIL", drag_end_accepted,
			       b_recv_len, IMG_LEN);
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
		printf("dragtest: run with --selftest\n");
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
	free(b_recv);
	free(b_in_buf);
}

int
main(int argc, char **argv)
{
	lud_run(&(lud_desc_t){
		.app_name = "dragtest",
		.width = 320, .height = 200,
		.init = init,
		.frame = frame,
		.cleanup = cleanup,
		.event = on_event,
		.argc = argc, .argv = argv,
	});
	return failures ? 1 : 0;
}
