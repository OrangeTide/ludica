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
#include <limits.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#define COPY_TEXT  "cliptest copy \xE2\x98\xBA line"   /* has a UTF-8 codepoint */
#define PASTE_TEXT "cliptest paste 12345"

/* A payload well over a single X property's max request size, to force the
 * INCR incremental-transfer path in both directions.  Plain ASCII so a chunk
 * boundary never splits a multi-byte codepoint. */
#define BIG_LEN 200000
static char *big_text;

static char *
make_big_text(void)
{
	char *s = malloc(BIG_LEN + 1);
	if (!s)
		return NULL;
	for (size_t i = 0; i < BIG_LEN; i++)
		s[i] = (char)('A' + (i % 26));
	s[BIG_LEN] = 0;
	return s;
}

/* A small binary blob standing in for image bytes.  It deliberately contains
 * NUL bytes so the round trip proves the path is byte-accurate, not string
 * based.  Kept under the INCR threshold to exercise the plain single-property
 * path for a non-text target. */
#define IMG_LEN 5000
static unsigned char img_blob[IMG_LEN];

static void
make_img_blob(void)
{
	for (size_t i = 0; i < IMG_LEN; i++)
		img_blob[i] = (unsigned char)(i * 37 + 11);
}

/* File paths for the uri-list round trip.  Includes a space and a non-ASCII
 * byte to exercise percent-encoding and decoding. */
static const char *const test_paths[] = {
	"/tmp/a file.txt",
	"/home/u/\xC3\xA9vj.png",   /* 'e' with acute accent, UTF-8 */
	"/x/y",
};
#define TEST_PATHS_N ((int)(sizeof(test_paths) / sizeof(test_paths[0])))

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
static Atom     incr_b;     /* INCR */
static Atom     png_b;      /* image/png */
static Atom     prop_b;     /* our read destination property */

static const char *b_owned; /* non-NULL while client B owns the selection */
static size_t      b_owned_len;
static int   b_got_notify;  /* SelectionNotify (non-INCR) seen; ready to read */
static Atom  b_notify_prop; /* property from that SelectionNotify */

/* B as requestor, receiving an INCR transfer from ludica. */
static int      b_in_active;
static int      b_in_done;
static unsigned char *b_in_buf;
static size_t   b_in_len, b_in_cap;

/* B as owner, sending an INCR transfer to ludica. */
static int      b_out_active;
static Window   b_out_requestor;
static Atom     b_out_prop;
static size_t   b_out_sent;

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
	XSelectInput(dpy_b, win_b, PropertyChangeMask);
	clip_b    = XInternAtom(dpy_b, "CLIPBOARD", False);
	utf8_b    = XInternAtom(dpy_b, "UTF8_STRING", False);
	targets_b = XInternAtom(dpy_b, "TARGETS", False);
	incr_b    = XInternAtom(dpy_b, "INCR", False);
	png_b     = XInternAtom(dpy_b, "image/png", False);
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
			if (b_owned_len <= b_chunk_max()) {
				XChangeProperty(dpy_b, req->requestor, prop, utf8_b, 8,
						PropModeReplace,
						(const unsigned char *)b_owned,
						(int)b_owned_len);
				notify.property = prop;
			} else if (!b_out_active) {
				long hint = (long)b_owned_len;
				XChangeProperty(dpy_b, req->requestor, prop, incr_b, 32,
						PropModeReplace,
						(unsigned char *)&hint, 1);
				XSelectInput(dpy_b, req->requestor, PropertyChangeMask);
				b_out_active = 1;
				b_out_requestor = req->requestor;
				b_out_prop = prop;
				b_out_sent = 0;
				notify.property = prop;
			}
		}
	}
	XSendEvent(dpy_b, req->requestor, False, 0, (XEvent *)&notify);
	XFlush(dpy_b);
}

/* Send the next INCR chunk to the requestor (or the terminating zero-length
 * write), driven by PropertyDelete on the requestor's window. */
static void
b_out_step(void)
{
	size_t chunk = b_chunk_max();
	size_t remaining = b_owned_len - b_out_sent;
	size_t n = remaining < chunk ? remaining : chunk;

	XChangeProperty(dpy_b, b_out_requestor, b_out_prop, utf8_b, 8,
			PropModeReplace,
			(const unsigned char *)b_owned + b_out_sent, (int)n);
	b_out_sent += n;
	XFlush(dpy_b);

	if (n == 0) {
		XSelectInput(dpy_b, b_out_requestor, NoEventMask);
		b_out_active = 0;
		b_out_requestor = 0;
		b_out_prop = 0;
		b_out_sent = 0;
	}
}

/* Probe the type of B's read destination property without fetching data. */
static Atom
b_probe_type(void)
{
	Atom type;
	int fmt;
	unsigned long nitems, after;
	unsigned char *p = NULL;

	if (XGetWindowProperty(dpy_b, win_b, prop_b, 0, 0, False,
			       AnyPropertyType, &type, &fmt, &nitems, &after,
			       &p) != Success)
		return None;
	if (p)
		XFree(p);
	return type;
}

/* Consume one incoming INCR chunk (B as requestor); zero-length ends it. */
static void
b_in_step(void)
{
	Atom type;
	int fmt;
	unsigned long nitems, after;
	unsigned char *p = NULL;

	if (XGetWindowProperty(dpy_b, win_b, prop_b, 0, LONG_MAX, True,
			       AnyPropertyType, &type, &fmt, &nitems, &after,
			       &p) != Success) {
		b_in_active = 0;
		b_in_done = 1;
		return;
	}
	size_t chunk = (size_t)nitems * (fmt / 8);
	if (chunk == 0) {
		if (p)
			XFree(p);
		b_in_active = 0;
		b_in_done = 1;
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
			b_notify_prop = e.xselection.property;
			if (b_notify_prop != None && b_probe_type() == incr_b) {
				/* Large reply: start an incremental receive. */
				free(b_in_buf);
				b_in_buf = NULL;
				b_in_len = 0;
				b_in_cap = 0;
				b_in_active = 1;
				b_in_done = 0;
				XDeleteProperty(dpy_b, win_b, prop_b);
				XFlush(dpy_b);
			} else {
				b_got_notify = 1;
			}
			break;
		case PropertyNotify:
			if (b_in_active && e.xproperty.atom == prop_b &&
			    e.xproperty.state == PropertyNewValue)
				b_in_step();
			else if (b_out_active &&
				 e.xproperty.window == b_out_requestor &&
				 e.xproperty.atom == b_out_prop &&
				 e.xproperty.state == PropertyDelete)
				b_out_step();
			break;
		case SelectionClear:
			b_owned = NULL;
			b_owned_len = 0;
			break;
		}
	}
}

/* Read the property B filled after a completed single-shot request, for any
 * 8-bit target (text or binary). Caller frees; *len_out gets the byte count. */
static unsigned char *
client_b_read(size_t *len_out)
{
	Atom type;
	int fmt;
	unsigned long nitems, after;
	unsigned char *data = NULL;
	unsigned char *out = NULL;

	if (len_out)
		*len_out = 0;
	if (XGetWindowProperty(dpy_b, win_b, prop_b, 0, 65536, False,
			       AnyPropertyType, &type, &fmt, &nitems, &after,
			       &data) != Success)
		return NULL;
	if (data && fmt == 8) {
		out = malloc(nitems + 1);
		if (out) {
			memcpy(out, data, nitems);
			out[nitems] = 0;
			if (len_out)
				*len_out = nitems;
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
	ST_COPY_WAIT,       /* B is reading what ludica copied (small) */
	ST_PASTE_WAIT,      /* ludica is reading what B copied (small) */
	ST_BIG_COPY_WAIT,   /* B is reading a large ludica payload via INCR */
	ST_BIG_PASTE_WAIT,  /* ludica is reading a large B payload via INCR */
	ST_IMG_COPY_WAIT,   /* B is reading a binary image/png payload */
	ST_DONE,
};

static int state = ST_START;
static int state_frame;               /* frame this state began */
static int paste_done;
static char *paste_buf;               /* async paste result (caller-sized) */

#define WAIT_LIMIT 300                 /* generous; INCR needs several frames */

static void
check(const char *label, const char *got, const char *want)
{
	int ok = got && !strcmp(got, want);
	if (!ok)
		failures++;
	/* Keep large payloads out of the log: report length, not content. */
	if (strlen(want) > 64) {
		printf("%-6s %s got_len=%zu want_len=%zu\n", ok ? "PASS" : "FAIL",
		       label, got ? strlen(got) : 0, strlen(want));
	} else {
		printf("%-6s %s got=[%s] want=[%s]\n", ok ? "PASS" : "FAIL",
		       label, got ? got : "(null)", want);
	}
	fflush(stdout);
}

/* Byte-exact comparison for binary payloads (NUL-safe). */
static void
check_bytes(const char *label, const unsigned char *got, size_t got_len,
	    const unsigned char *want, size_t want_len)
{
	int ok = got && got_len == want_len && !memcmp(got, want, want_len);
	if (!ok)
		failures++;
	printf("%-6s %s got_len=%zu want_len=%zu\n", ok ? "PASS" : "FAIL",
	       label, got ? got_len : 0, want_len);
	fflush(stdout);
}

/* In-process round trip for the file-list helpers: set paths, read them back,
 * and confirm each decodes to the original.  Uses ludica's owner-side path. */
static void
check_files(void)
{
	if (lud_clipboard_set_files(test_paths, TEST_PATHS_N) != 0) {
		printf("FAIL   files set_files failed\n");
		failures++;
		return;
	}
	char **got = lud_clipboard_get_files();
	int n = 0;
	if (got)
		while (got[n])
			n++;

	int ok = got && n == TEST_PATHS_N;
	for (int i = 0; ok && i < n; i++)
		ok = !strcmp(got[i], test_paths[i]);
	if (!ok)
		failures++;
	printf("%-6s files got=%d want=%d\n", ok ? "PASS" : "FAIL",
	       n, TEST_PATHS_N);
	fflush(stdout);

	if (got) {
		for (int i = 0; i < n; i++)
			free(got[i]);
		free(got);
	}
}

/* async callback for the paste direction */
static void
on_paste_async(const char *format, void *data, size_t len, void *user)
{
	(void)format; (void)user;
	paste_done = 1;
	free(paste_buf);
	paste_buf = NULL;
	if (data && len) {
		paste_buf = malloc(len + 1);
		if (paste_buf) {
			memcpy(paste_buf, data, len);
			paste_buf[len] = 0;
		}
	}
}

/* Take B's most recent read result (INCR or single-shot). Caller frees;
 * *len_out gets the byte count. */
static unsigned char *
b_take_read(size_t *len_out)
{
	if (b_in_done) {
		unsigned char *out = b_in_buf;
		if (len_out)
			*len_out = b_in_len;
		b_in_buf = NULL;
		b_in_len = 0;
		b_in_cap = 0;
		b_in_done = 0;
		return out;
	}
	if (b_notify_prop == None) {
		if (len_out)
			*len_out = 0;
		return NULL;
	}
	return client_b_read(len_out);
}

/* B requests `target` (CLIPBOARD) from ludica (copy direction). */
static void
begin_copy_read(Atom target)
{
	b_got_notify = 0;
	b_in_done = 0;
	b_notify_prop = None;
	XConvertSelection(dpy_b, clip_b, target, prop_b, win_b, CurrentTime);
	XFlush(dpy_b);
}

/* B takes ownership, then ludica reads it async (paste direction).  XSync,
 * not XFlush, so the server commits the ownership change before ludica queries
 * the owner; otherwise its async-get shortcut may still see itself as owner. */
static void
begin_paste_read(const char *text)
{
	b_owned = text;
	b_owned_len = strlen(text);
	XSetSelectionOwner(dpy_b, clip_b, win_b, CurrentTime);
	XSync(dpy_b, False);
	paste_done = 0;
	lud_clipboard_get_async(LUD_CLIPBOARD_TEXT, on_paste_async, NULL);
}

/* A copy-direction read is complete once a single-shot notify or an INCR
 * transfer has landed. */
static int
copy_read_ready(void)
{
	return b_got_notify || b_in_done;
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
		begin_copy_read(utf8_b);
		state = ST_COPY_WAIT;
		state_frame = frames;
		break;

	case ST_COPY_WAIT:
		if (copy_read_ready()) {
			char *got = (char *)b_take_read(NULL);
			check("copy", got, COPY_TEXT);
			free(got);

			begin_paste_read(PASTE_TEXT);
			state = ST_PASTE_WAIT;
			state_frame = frames;
		} else if (frames - state_frame > WAIT_LIMIT) {
			check("copy", NULL, COPY_TEXT);
			state = ST_DONE;
		}
		break;

	case ST_PASTE_WAIT:
		if (paste_done) {
			check("paste", paste_buf, PASTE_TEXT);
			/* Large copy: forces the outgoing INCR path in ludica. */
			if (lud_clipboard_set_text(big_text) != 0) {
				printf("FAIL big-copy: set_text failed\n");
				failures++;
				state = ST_DONE;
				return;
			}
			begin_copy_read(utf8_b);
			state = ST_BIG_COPY_WAIT;
			state_frame = frames;
		} else if (frames - state_frame > WAIT_LIMIT) {
			check("paste", NULL, PASTE_TEXT);
			state = ST_DONE;
		}
		break;

	case ST_BIG_COPY_WAIT:
		if (copy_read_ready()) {
			char *got = (char *)b_take_read(NULL);
			check("big-copy", got, big_text);
			free(got);

			/* Large paste: forces the incoming INCR path in ludica. */
			begin_paste_read(big_text);
			state = ST_BIG_PASTE_WAIT;
			state_frame = frames;
		} else if (frames - state_frame > WAIT_LIMIT) {
			check("big-copy", NULL, big_text);
			state = ST_DONE;
		}
		break;

	case ST_BIG_PASTE_WAIT:
		if (paste_done) {
			check("big-paste", paste_buf, big_text);
			/* Binary target: ludica owns image/png, B reads it. */
			if (lud_clipboard_set_data(LUD_CLIPBOARD_PNG,
						   img_blob, IMG_LEN) != 0) {
				printf("FAIL img-copy: set_data failed\n");
				failures++;
				state = ST_DONE;
				return;
			}
			begin_copy_read(png_b);
			state = ST_IMG_COPY_WAIT;
			state_frame = frames;
		} else if (frames - state_frame > WAIT_LIMIT) {
			check("big-paste", NULL, big_text);
			state = ST_DONE;
		}
		break;

	case ST_IMG_COPY_WAIT:
		if (copy_read_ready()) {
			size_t got_len = 0;
			unsigned char *got = b_take_read(&got_len);
			check_bytes("img-copy", got, got_len, img_blob, IMG_LEN);
			free(got);

			/* File-list helpers (in-process round trip). */
			check_files();
			state = ST_DONE;
		} else if (frames - state_frame > WAIT_LIMIT) {
			check_bytes("img-copy", NULL, 0, img_blob, IMG_LEN);
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
		make_img_blob();
		big_text = make_big_text();
		if (!big_text) {
			printf("FAIL: out of memory building test payload\n");
			failures++;
			lud_quit();
			return;
		}
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
	free(big_text);
	big_text = NULL;
	free(b_in_buf);
	b_in_buf = NULL;
	free(paste_buf);
	paste_buf = NULL;
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
