/* webclip — manual clipboard + drag-and-drop test.
 *
 * A tiny interactive harness for the clipboard and drop-target APIs. It runs on
 * every backend, but its reason to exist is the browser: on Emscripten the
 * clipboard is asynchronous and gesture/permission gated and drops arrive as
 * DOM events, so the paths need a human at a real browser to confirm. Keys copy
 * to the clipboard and kick off an async paste; dropping text or a file onto the
 * window reports what arrived. Everything is also printed to stdout (the browser
 * console) so results are visible without reading the canvas.
 *
 *   [C] copy text     [V] paste (async)   [M] copy multi (text + html)
 *   [I] copy a 1x1 PNG                     [ESC] quit
 *   drag text or a file onto the window to see a LUD_EV_DROP
 *
 * Made by a machine. PUBLIC DOMAIN (CC0-1.0)
 */

#include <ludica.h>
#include <ludica_gfx.h>
#include <ludica_font.h>
#include <ludica_input.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define WIN_W 760
#define WIN_H 420

/* A 1x1 red PNG, so [I] puts a real image on the clipboard. */
static const unsigned char png_1x1[] = {
	137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82,
	0, 0, 0, 1, 0, 0, 0, 1, 1, 3, 0, 0, 0, 37, 219, 86,
	202, 0, 0, 0, 32, 99, 72, 82, 77, 0, 0, 122, 38, 0, 0, 128,
	132, 0, 0, 250, 0, 0, 0, 128, 232, 0, 0, 117, 48, 0, 0, 234,
	96, 0, 0, 58, 152, 0, 0, 23, 112, 156, 186, 81, 60, 0, 0, 0,
	6, 80, 76, 84, 69, 255, 0, 0, 255, 255, 255, 65, 29, 52, 17, 0,
	0, 0, 1, 98, 75, 71, 68, 1, 255, 2, 45, 222, 0, 0, 0, 7,
	116, 73, 77, 69, 7, 234, 7, 15, 5, 28, 54, 190, 78, 84, 45, 0,
	0, 0, 10, 73, 68, 65, 84, 8, 215, 99, 96, 0, 0, 0, 2, 0,
	1, 226, 33, 188, 51, 0, 0, 0, 0, 73, 69, 78, 68, 174, 66, 96,
	130,
};

static lud_font_t font;
static int copy_counter;
static char line_action[160] = "action: (none yet)";
static char line_paste[256]  = "paste : (press V)";
static char line_drop[256]   = "drop  : (drag something in)";

/* Shorten arbitrary bytes to a one-line, printable snippet. */
static void
snippet(char *out, size_t outsz, const void *data, size_t len)
{
	const unsigned char *p = data;
	size_t n = 0;
	if (!p) { snprintf(out, outsz, "(null)"); return; }
	for (size_t i = 0; i < len && n + 2 < outsz && n < 48; i++) {
		unsigned char c = p[i];
		out[n++] = (c >= 32 && c < 127) ? (char)c : '.';
	}
	out[n] = 0;
}

/* Async paste result (see [V]). data is valid only during this call. */
static void
on_paste(const char *format, void *data, size_t len, void *user)
{
	(void)user;
	char snip[64];
	snippet(snip, sizeof(snip), data, len);
	if (data)
		snprintf(line_paste, sizeof(line_paste),
			 "paste : %s, %zu bytes: \"%s\"", format, len, snip);
	else
		snprintf(line_paste, sizeof(line_paste),
			 "paste : %s -> empty/denied", format);
	printf("%s\n", line_paste);
	fflush(stdout);
}

static void
do_copy_text(void)
{
	char buf[96];
	snprintf(buf, sizeof(buf), "ludica clipboard test \xE2\x98\xBA #%d", ++copy_counter);
	int ok = lud_clipboard_set_text(buf) == LUD_OK;
	snprintf(line_action, sizeof(line_action),
		 "action: copy text \"%s\" -> %s", buf, ok ? "ok" : "ERR");
	printf("%s\n", line_action);
	fflush(stdout);
}

static void
do_copy_multi(void)
{
	lud_clip_item_t items[] = {
		{ LUD_CLIPBOARD_HTML, "<b>ludica</b> rich text", 23 },
		{ LUD_CLIPBOARD_TEXT, "ludica plain text", 17 },
	};
	int ok = lud_clipboard_set_multi(items, 2) == LUD_OK;
	snprintf(line_action, sizeof(line_action),
		 "action: copy multi (html + text) -> %s", ok ? "ok" : "ERR");
	printf("%s\n", line_action);
	fflush(stdout);
}

static void
do_copy_png(void)
{
	int ok = lud_clipboard_set_data(LUD_CLIPBOARD_PNG, png_1x1,
				        sizeof(png_1x1)) == LUD_OK;
	snprintf(line_action, sizeof(line_action),
		 "action: copy 1x1 PNG (%zu bytes) -> %s",
		 sizeof(png_1x1), ok ? "ok" : "ERR");
	printf("%s\n", line_action);
	fflush(stdout);
}

static void
do_paste(void)
{
	snprintf(line_paste, sizeof(line_paste), "paste : (waiting...)");
	lud_clipboard_get_async(LUD_CLIPBOARD_TEXT, on_paste, NULL);
}

static int
on_event(const lud_event_t *ev)
{
	if (ev->type == LUD_EV_KEY_DOWN) {
		switch (ev->key.keycode) {
		case LUD_KEY_C: do_copy_text();  break;
		case LUD_KEY_V: do_paste();      break;
		case LUD_KEY_M: do_copy_multi(); break;
		case LUD_KEY_I: do_copy_png();   break;
		case LUD_KEY_ESCAPE: lud_quit(); break;
		default: break;
		}
		return 1;
	}

	if (ev->type == LUD_EV_DROP) {
		char snip[64];
		snippet(snip, sizeof(snip), ev->drop.data, ev->drop.len);
		snprintf(line_drop, sizeof(line_drop),
			 "drop  : %s, %zu bytes at (%d,%d): \"%s\"",
			 ev->drop.format, ev->drop.len, ev->drop.x, ev->drop.y, snip);
		printf("%s\n", line_drop);

		/* A file list (X11/Windows) decodes to paths; a browser file drop
		 * arrives as raw bytes under its MIME type instead. */
		if (!strcmp(ev->drop.format, LUD_CLIPBOARD_URI_LIST)) {
			char **files = lud_parse_uri_list(ev->drop.data, ev->drop.len);
			for (int i = 0; files && files[i]; i++)
				printf("  file[%d]: %s\n", i, files[i]);
			if (files) {
				for (int i = 0; files[i]; i++)
					free(files[i]);
				free(files);
			}
		}
		fflush(stdout);
		return 1;
	}

	return 0;
}

static void
init(void)
{
	font = lud_make_default_font();
	printf("webclip ready: C copy, V paste, M multi, I png, drop to test.\n");
	fflush(stdout);
}

static void
frame(float dt)
{
	(void)dt;
	const char *lines[] = {
		"ludica clipboard + drag-and-drop test",
		"",
		"[C] copy text   [V] paste (async)   [M] copy multi",
		"[I] copy 1x1 PNG                     [ESC] quit",
		"drag text or a file onto this window",
		"",
		line_action,
		line_paste,
		line_drop,
	};
	int n = (int)(sizeof(lines) / sizeof(lines[0]));

	lud_viewport(0, 0, lud_width(), lud_height());
	lud_clear(0.10f, 0.12f, 0.16f, 1.0f);

	lud_sprite_begin(0, 0, WIN_W, WIN_H);
	for (int i = 0; i < n; i++)
		if (lines[i][0])
			lud_draw_text(font, 16.0f, 24.0f + i * 34.0f, 2.0f, lines[i]);
	lud_sprite_end();
}

static void
cleanup(void)
{
	lud_destroy_font(font);
}

int
main(int argc, char **argv)
{
	return lud_run(&(lud_desc_t){
		.app_name = "webclip",
		.width = WIN_W,
		.height = WIN_H,
		.init = init,
		.frame = frame,
		.event = on_event,
		.cleanup = cleanup,
		.argc = argc,
		.argv = argv,
	});
}
