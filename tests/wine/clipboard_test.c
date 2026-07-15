/*
 * clipboard_test.c — Windows clipboard round-trip test.
 *
 * Exercises win32/clipboard.c in-process against the real Win32 clipboard.
 * Built for Windows and run under Wine on Linux (see run.sh), so the whole
 * clipboard path can be checked from a Linux dev box without a Windows host.
 *
 * Made by a machine. PUBLIC DOMAIN (CC0-1.0)
 */
#include <ludica.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails;
#define CHECK(cond, name) do { \
	int _c = (cond); printf("%-6s %s\n", _c ? "PASS" : "FAIL", name); \
	if (!_c) fails++; } while (0)

int
main(void)
{
	/* text (UTF-8 round trip through CF_UNICODETEXT) */
	lud_clipboard_set_text("hello \xE2\x98\xBA world");
	char *t = lud_clipboard_get_text();
	CHECK(t && !strcmp(t, "hello \xE2\x98\xBA world"), "text");
	free(t);

	/* binary bytes with embedded NUL under a registered format */
	unsigned char blob[300];
	for (int i = 0; i < 300; i++) blob[i] = (unsigned char)(i * 7 + 3);
	lud_clipboard_set_data(LUD_CLIPBOARD_PNG, blob, sizeof blob);
	size_t n = 0;
	unsigned char *g = lud_clipboard_get_data(LUD_CLIPBOARD_PNG, &n);
	CHECK(g && n == sizeof blob && !memcmp(g, blob, sizeof blob), "png-bytes");
	free(g);

	/* files via CF_HDROP (space + non-ASCII in the paths) */
	const char *paths[] = { "C:\\tmp\\a b.txt", "C:\\x\\\xC3\xA9.png" };
	lud_clipboard_set_files(paths, 2);
	char **f = lud_clipboard_get_files();
	int c = 0; while (f && f[c]) c++;
	int fok = f && c == 2 && !strcmp(f[0], paths[0]) && !strcmp(f[1], paths[1]);
	CHECK(fok, "files");
	if (f) { for (int i = 0; i < c; i++) free(f[i]); free(f); }

	/* uri-list read derived from the HDROP */
	char **uf = lud_clipboard_get_files();  /* still set */
	size_t ulen = 0;
	char *ul = lud_clipboard_get_data(LUD_CLIPBOARD_URI_LIST, &ulen);
	CHECK(ul && strstr(ul, "a%20b.txt") != NULL, "uri-list-from-hdrop");
	free(ul);
	if (uf) { for (int i = 0; uf[i]; i++) free(uf[i]); free(uf); }

	/* rich text: HTML fragment survives the CF_HTML header round trip */
	const char *html = "<b>bold</b> \xE2\x98\xBA";
	lud_clipboard_set_data(LUD_CLIPBOARD_HTML, html, strlen(html));
	char *h = lud_clipboard_get_data(LUD_CLIPBOARD_HTML, NULL);
	CHECK(h && !strcmp(h, html), "html-cf_html");
	free(h);

	/* set_html offers html + plain fallback (multi) */
	lud_clipboard_set_html("<i>hi</i>", "hi");
	char *hh = lud_clipboard_get_html();
	char *hp = lud_clipboard_get_text();
	CHECK(hh && !strcmp(hh, "<i>hi</i>"), "set_html-html");
	CHECK(hp && !strcmp(hp, "hi"), "set_html-plain");
	free(hh); free(hp);

	/* multi: two formats at once, both readable */
	lud_clip_item_t items[] = {
		{ LUD_CLIPBOARD_TEXT, "txt", 3 },
		{ LUD_CLIPBOARD_PNG, "\x01\x00\x02\x00", 4 },
	};
	lud_clipboard_set_multi(items, 2);
	char *mt = lud_clipboard_get_text();
	size_t mn = 0;
	unsigned char *mg = lud_clipboard_get_data(LUD_CLIPBOARD_PNG, &mn);
	CHECK(mt && !strcmp(mt, "txt"), "multi-text");
	CHECK(mg && mn == 4 && mg[1] == 0 && mg[3] == 0, "multi-png-nul");
	free(mt); free(mg);

	printf("%d failure(s)\n", fails);
	return fails ? 1 : 0;
}
