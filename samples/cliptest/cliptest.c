/*
 * cliptest — clipboard copy/paste sample and self-test.
 *
 * Interactive: press C to copy a sample string to the clipboard, V to
 * paste (read it back and print it). Try C here then Ctrl+V in another
 * app, or copy in another app then press V here.
 *
 * Run --selftest to exercise a real cross-client round-trip in both
 * directions and exit non-zero on failure (used by 'make run-tests').
 *
 * The self-test opens a second, independent X11 connection in the same
 * process to act as "another application". This drives the actual X11
 * selection protocol (XConvertSelection -> SelectionRequest ->
 * SelectionNotify), not just ludica's owner-side shortcut, so both the
 * copy and paste paths are covered end to end.
 *
 * Made by a machine. PUBLIC DOMAIN (CC0-1.0)
 */

#include <ludica.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#define COPY_TEXT  "cliptest copy \xE2\x98\xBA line"   /* has a UTF-8 codepoint */
#define PASTE_TEXT "cliptest paste 12345"

static int selftest;
static int failures;
static int frames;

/* ---- interactive helpers ---- */

static void
do_copy(void)
{
	int rc = lud_clipboard_set_text(COPY_TEXT);
	printf("copy: set rc=%d text=[%s]\n", rc, COPY_TEXT);
	fflush(stdout);
}

static void
do_paste(void)
{
	char *s = lud_clipboard_get_text();
	printf("paste: text=[%s]\n", s ? s : "(empty)");
	fflush(stdout);
	free(s);
}

static int
on_event(const lud_event_t *ev)
{
	if (selftest)
		return 0;
	if (ev->type == LUD_EV_KEY_DOWN) {
		if (ev->key.keycode == LUD_KEY_C)
			do_copy();
		else if (ev->key.keycode == LUD_KEY_V)
			do_paste();
		else if (ev->key.keycode == LUD_KEY_ESCAPE)
			lud_quit();
	}
	return 0;
}

/* ---- second X client, acts as "another application" during selftest ---- */

static Display *dpy_b;
static Window   win_b;
static Atom     clip_b;     /* CLIPBOARD */
static Atom     utf8_b;     /* UTF8_STRING */
static Atom     targets_b;  /* TARGETS */
static Atom     prop_b;     /* our read destination property */

static const char *b_owned; /* non-NULL while client B owns the selection */
static int   b_got_notify;  /* SelectionNotify seen for our last request */
static Atom  b_notify_prop; /* property from that SelectionNotify */

static int
client_b_init(void)
{
	dpy_b = XOpenDisplay(NULL);
	if (!dpy_b)
		return -1;
	win_b = XCreateSimpleWindow(dpy_b, DefaultRootWindow(dpy_b),
				    0, 0, 1, 1, 0, 0, 0);
	XSelectInput(dpy_b, win_b, PropertyChangeMask);
	clip_b    = XInternAtom(dpy_b, "CLIPBOARD", False);
	utf8_b    = XInternAtom(dpy_b, "UTF8_STRING", False);
	targets_b = XInternAtom(dpy_b, "TARGETS", False);
	prop_b    = XInternAtom(dpy_b, "CLIPTEST_B", False);
	return 0;
}

/* Serve a request when B owns the selection (mirror of the code under test,
 * kept independent so the test does not lean on ludica internals). */
static void
client_b_serve(const XSelectionRequestEvent *req)
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

	if (b_owned && req->selection == clip_b) {
		Atom prop = req->property != None ? req->property : req->target;
		if (req->target == targets_b) {
			Atom t[] = { targets_b, utf8_b };
			XChangeProperty(dpy_b, req->requestor, prop, XA_ATOM, 32,
					PropModeReplace, (unsigned char *)t, 2);
			notify.property = prop;
		} else if (req->target == utf8_b) {
			XChangeProperty(dpy_b, req->requestor, prop, utf8_b, 8,
					PropModeReplace,
					(const unsigned char *)b_owned,
					(int)strlen(b_owned));
			notify.property = prop;
		}
	}
	XSendEvent(dpy_b, req->requestor, False, 0, (XEvent *)&notify);
	XFlush(dpy_b);
}

/* Pump B's events once (non-blocking). */
static void
client_b_pump(void)
{
	while (XPending(dpy_b)) {
		XEvent e;
		XNextEvent(dpy_b, &e);
		switch (e.type) {
		case SelectionRequest:
			client_b_serve(&e.xselectionrequest);
			break;
		case SelectionNotify:
			b_got_notify = 1;
			b_notify_prop = e.xselection.property;
			break;
		case SelectionClear:
			b_owned = NULL;
			break;
		}
	}
}

/* Read the property B filled after a completed request. Caller frees. */
static char *
client_b_read(void)
{
	Atom type;
	int fmt;
	unsigned long nitems, after;
	unsigned char *data = NULL;
	char *out = NULL;

	if (XGetWindowProperty(dpy_b, win_b, prop_b, 0, 65536, False,
			       AnyPropertyType, &type, &fmt, &nitems, &after,
			       &data) != Success)
		return NULL;
	if (data && type == utf8_b && fmt == 8) {
		out = malloc(nitems + 1);
		if (out) {
			memcpy(out, data, nitems);
			out[nitems] = 0;
		}
	}
	if (data)
		XFree(data);
	XDeleteProperty(dpy_b, win_b, prop_b);
	return out;
}

/* ---- self-test state machine ---- */

enum {
	ST_START,
	ST_COPY_WAIT,   /* B is reading what ludica copied */
	ST_PASTE_WAIT,  /* ludica is reading what B put on the clipboard */
	ST_DONE,
};

static int state = ST_START;
static int state_frame;               /* frame this state began */
static int paste_done;
static char paste_got[256];

#define WAIT_LIMIT 180                 /* generous: a few seconds at any fps */

static void
check(const char *label, const char *got, const char *want)
{
	int ok = got && !strcmp(got, want);
	if (!ok)
		failures++;
	printf("%-6s %s got=[%s] want=[%s]\n", ok ? "PASS" : "FAIL",
	       label, got ? got : "(null)", want);
	fflush(stdout);
}

/* async callback for the paste direction */
static void
on_paste_async(const char *format, void *data, size_t len, void *user)
{
	(void)format; (void)user;
	paste_done = 1;
	if (data && len < sizeof(paste_got)) {
		memcpy(paste_got, data, len);
		paste_got[len] = 0;
	} else {
		paste_got[0] = 0;
	}
}

static void
selftest_step(void)
{
	client_b_pump();

	switch (state) {
	case ST_START:
		/* Let the window map and settle for a couple of frames. */
		if (frames < 2)
			return;
		/* Copy direction: ludica sets, B reads back over the wire. */
		if (lud_clipboard_set_text(COPY_TEXT) != 0) {
			printf("FAIL copy: set_text failed\n");
			failures++;
			state = ST_DONE;
			return;
		}
		b_got_notify = 0;
		XConvertSelection(dpy_b, clip_b, utf8_b, prop_b, win_b,
				  CurrentTime);
		XFlush(dpy_b);
		state = ST_COPY_WAIT;
		state_frame = frames;
		break;

	case ST_COPY_WAIT:
		if (b_got_notify) {
			char *got = b_notify_prop != None ? client_b_read() : NULL;
			check("copy", got, COPY_TEXT);
			free(got);

			/* Paste direction: B owns, ludica reads it async. */
			b_owned = PASTE_TEXT;
			XSetSelectionOwner(dpy_b, clip_b, win_b, CurrentTime);
			XFlush(dpy_b);
			paste_done = 0;
			lud_clipboard_get_async(LUD_CLIPBOARD_TEXT,
						on_paste_async, NULL);
			state = ST_PASTE_WAIT;
			state_frame = frames;
		} else if (frames - state_frame > WAIT_LIMIT) {
			check("copy", NULL, COPY_TEXT);
			state = ST_DONE;
		}
		break;

	case ST_PASTE_WAIT:
		if (paste_done) {
			check("paste", paste_got, PASTE_TEXT);
			state = ST_DONE;
		} else if (frames - state_frame > WAIT_LIMIT) {
			check("paste", NULL, PASTE_TEXT);
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
		if (client_b_init() != 0) {
			printf("FAIL: second X connection unavailable\n");
			failures++;
			lud_quit();
		}
	} else {
		printf("cliptest: C=copy  V=paste  ESC=quit\n");
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
}

static void
cleanup(void)
{
	if (dpy_b) {
		XDestroyWindow(dpy_b, win_b);
		XCloseDisplay(dpy_b);
		dpy_b = NULL;
	}
}

int
main(int argc, char **argv)
{
	lud_run(&(lud_desc_t){
		.app_name = "cliptest",
		.width = 320, .height = 200,
		.init = init,
		.frame = frame,
		.cleanup = cleanup,
		.event = on_event,
		.argc = argc, .argv = argv,
	});
	return failures ? 1 : 0;
}
