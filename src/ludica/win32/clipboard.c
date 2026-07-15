/*
 * win32/clipboard.c — Windows clipboard for ludica.
 *
 * Split out of platform_win32.c so it depends only on the Win32 clipboard/shell
 * APIs (not EGL or the window loop), which keeps it independently testable
 * (see the Wine round-trip test).  The app window comes from platform_win32.c
 * via lud__win32_window().
 *
 * Each ludica MIME format maps to a native clipboard format and encoding:
 * CF_UNICODETEXT for text, CF_HDROP for files, the registered "HTML Format"
 * with its CF_HTML offset header for HTML, and a registered format holding raw
 * bytes for images (PNG), RTF, and anything else.  Win32 handles large
 * transfers and multiple formats natively, so there is no INCR equivalent.
 *
 * Made by a machine. PUBLIC DOMAIN (CC0-1.0)
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>   /* DragQueryFileW */
#include <shlobj.h>     /* DROPFILES (in shellapi.h on MSVC, shlobj.h on mingw) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ludica_internal.h"
#include "win32.h"

/* ---- Clipboard (CF_UNICODETEXT) ---- */

char *
lud_clipboard_get_text(void)
{
	char *out = NULL;

	if (!OpenClipboard(lud__win32_window()))
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

	if (!OpenClipboard(lud__win32_window())) {
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

/* ---- Non-text targets ----
 *
 * Win32 handles large transfers and multiple formats natively (several formats
 * between EmptyClipboard and CloseClipboard), so there is no INCR equivalent.
 * Each ludica MIME format maps to a Windows clipboard format and an encoding:
 * text -> CF_UNICODETEXT, files -> CF_HDROP, HTML -> the "HTML Format" registered
 * format with its CF_HTML header, and everything else -> a registered format
 * holding the raw bytes.
 *
 * NOTE: compile-verified only.  The Windows backend has never been linked or
 * run (needs an ANGLE SDK and a Windows host); validate there before relying on
 * this. */

enum win_clip_kind { WC_TEXT, WC_HDROP, WC_HTML, WC_BYTES };

static UINT
win_clip_format(const char *fmt, enum win_clip_kind *kind)
{
	if (!fmt || !strcmp(fmt, LUD_CLIPBOARD_TEXT) || !strcmp(fmt, "text/plain")) {
		*kind = WC_TEXT;
		return CF_UNICODETEXT;
	}
	if (!strcmp(fmt, LUD_CLIPBOARD_URI_LIST)) {
		*kind = WC_HDROP;
		return CF_HDROP;
	}
	if (!strcmp(fmt, LUD_CLIPBOARD_HTML)) {
		*kind = WC_HTML;
		return RegisterClipboardFormatA("HTML Format");
	}
	if (!strcmp(fmt, LUD_CLIPBOARD_RTF)) {
		*kind = WC_BYTES;
		return RegisterClipboardFormatA("Rich Text Format");
	}
	if (!strcmp(fmt, LUD_CLIPBOARD_PNG)) {
		*kind = WC_BYTES;
		return RegisterClipboardFormatA("PNG");
	}
	*kind = WC_BYTES;
	return RegisterClipboardFormatA(fmt);
}

/* ---- HGLOBAL builders (data -> movable global for SetClipboardData) ---- */

static HGLOBAL
hg_bytes(const void *data, size_t len)
{
	HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, len ? len : 1);
	if (!h)
		return NULL;
	void *dst = GlobalLock(h);
	if (!dst) {
		GlobalFree(h);
		return NULL;
	}
	if (len)
		memcpy(dst, data, len);
	GlobalUnlock(h);
	return h;
}

static HGLOBAL
hg_text(const void *data, size_t len)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, (const char *)data, (int)len,
				    NULL, 0);
	HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (size_t)(n + 1) * sizeof(WCHAR));
	if (!h)
		return NULL;
	WCHAR *w = GlobalLock(h);
	if (!w) {
		GlobalFree(h);
		return NULL;
	}
	MultiByteToWideChar(CP_UTF8, 0, (const char *)data, (int)len, w, n);
	w[n] = 0;
	GlobalUnlock(h);
	return h;
}

static HGLOBAL
hg_hdrop_from_paths(const char *const *paths, int count)
{
	int total = 0;
	for (int i = 0; i < count; i++)
		if (paths[i])
			total += MultiByteToWideChar(CP_UTF8, 0, paths[i], -1,
						     NULL, 0);
	total += 1; /* extra NUL terminating the whole list */

	HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE,
				sizeof(DROPFILES) + (size_t)total * sizeof(WCHAR));
	if (!h)
		return NULL;
	DROPFILES *df = GlobalLock(h);
	if (!df) {
		GlobalFree(h);
		return NULL;
	}
	memset(df, 0, sizeof(*df));
	df->pFiles = sizeof(DROPFILES);
	df->fWide = TRUE;

	WCHAR *w = (WCHAR *)((char *)df + sizeof(DROPFILES));
	int off = 0;
	for (int i = 0; i < count; i++)
		if (paths[i])
			off += MultiByteToWideChar(CP_UTF8, 0, paths[i], -1,
						   w + off, total - off);
	w[off] = 0;
	GlobalUnlock(h);
	return h;
}

static HGLOBAL
hg_hdrop_from_urilist(const void *data, size_t len)
{
	char **paths = lud_parse_uri_list(data, len);
	if (!paths)
		return NULL;
	int count = 0;
	while (paths[count])
		count++;
	HGLOBAL h = hg_hdrop_from_paths((const char *const *)paths, count);
	for (int i = 0; i < count; i++)
		free(paths[i]);
	free(paths);
	return h;
}

/* Wrap an HTML fragment in the CF_HTML format (Version/offset header plus the
 * fragment markers).  Offsets are byte positions into the whole buffer; the
 * header's numeric fields are fixed ten-digit widths so its length is stable. */
static HGLOBAL
hg_html(const void *data, size_t len)
{
	static const char pre[] = "<html><body>\r\n<!--StartFragment-->";
	static const char post[] = "<!--EndFragment-->\r\n</body></html>";
	static const char fmt[] =
		"Version:0.9\r\nStartHTML:%010d\r\nEndHTML:%010d\r\n"
		"StartFragment:%010d\r\nEndFragment:%010d\r\n";

	char hdr[192];
	int hdr_len = snprintf(hdr, sizeof(hdr), fmt, 0, 0, 0, 0);
	int start_html = hdr_len;
	int start_frag = hdr_len + (int)(sizeof(pre) - 1);
	int end_frag = start_frag + (int)len;
	int end_html = end_frag + (int)(sizeof(post) - 1);
	snprintf(hdr, sizeof(hdr), fmt, start_html, end_html, start_frag, end_frag);

	HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (size_t)end_html + 1);
	if (!h)
		return NULL;
	char *b = GlobalLock(h);
	if (!b) {
		GlobalFree(h);
		return NULL;
	}
	memcpy(b, hdr, hdr_len);
	memcpy(b + hdr_len, pre, sizeof(pre) - 1);
	memcpy(b + start_frag, data, len);
	memcpy(b + end_frag, post, sizeof(post) - 1);
	b[end_html] = 0;
	GlobalUnlock(h);
	return h;
}

static HGLOBAL
win_build(const char *fmt, const void *data, size_t len, UINT *cf_out)
{
	enum win_clip_kind k;
	*cf_out = win_clip_format(fmt, &k);
	switch (k) {
	case WC_TEXT:  return hg_text(data, len);
	case WC_HDROP: return hg_hdrop_from_urilist(data, len);
	case WC_HTML:  return hg_html(data, len);
	default:       return hg_bytes(data, len);
	}
}

/* ---- Decoders (clipboard handle -> malloc'd bytes) ---- */

static void *
w16_to_u8(const WCHAR *w, size_t *len_out)
{
	int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
	if (n <= 0)
		return NULL;
	char *out = malloc(n);
	if (out) {
		WideCharToMultiByte(CP_UTF8, 0, w, -1, out, n, NULL, NULL);
		if (len_out)
			*len_out = (size_t)(n - 1); /* exclude the NUL */
	}
	return out;
}

/* Extract the fragment bytes from a CF_HTML buffer using its StartFragment /
 * EndFragment offsets. */
static void *
cf_html_fragment(const char *buf, size_t sz, size_t *len_out)
{
	char *tmp = malloc(sz + 1);
	if (!tmp)
		return NULL;
	memcpy(tmp, buf, sz);
	tmp[sz] = 0;

	const char *p = strstr(tmp, "StartFragment:");
	const char *q = strstr(tmp, "EndFragment:");
	long sf = p ? atol(p + 14) : -1;
	long ef = q ? atol(q + 12) : -1;

	void *out = NULL;
	if (sf >= 0 && ef >= sf && (size_t)ef <= sz) {
		size_t flen = (size_t)(ef - sf);
		out = malloc(flen + 1);
		if (out) {
			memcpy(out, tmp + sf, flen);
			((char *)out)[flen] = 0;
			if (len_out)
				*len_out = flen;
		}
	}
	free(tmp);
	return out;
}

/* Read a CF_HDROP into a NULL-terminated array of malloc'd UTF-8 paths. */
static char **
hdrop_paths(HDROP hd)
{
	UINT count = DragQueryFileW(hd, 0xFFFFFFFF, NULL, 0);
	char **list = malloc(((size_t)count + 1) * sizeof(*list));
	if (!list)
		return NULL;
	UINT n = 0;
	for (UINT i = 0; i < count; i++) {
		UINT wlen = DragQueryFileW(hd, i, NULL, 0);
		WCHAR *w = malloc(((size_t)wlen + 1) * sizeof(WCHAR));
		if (!w)
			continue;
		DragQueryFileW(hd, i, w, wlen + 1);
		int u8 = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
		char *p = malloc(u8 > 0 ? (size_t)u8 : 1);
		if (p) {
			WideCharToMultiByte(CP_UTF8, 0, w, -1, p, u8, NULL, NULL);
			list[n++] = p;
		}
		free(w);
	}
	list[n] = NULL;
	return list;
}

/* ---- Shared with dragdrop.c (see win32.h) ---- */

UINT
lud__win32_clip_cf(const char *format)
{
	enum win_clip_kind k;
	return win_clip_format(format, &k);
}

void *
lud__win32_clip_decode(const char *format, HANDLE h, size_t *len_out)
{
	if (len_out)
		*len_out = 0;
	if (!h)
		return NULL;

	enum win_clip_kind k;
	win_clip_format(format, &k);
	void *out = NULL;
	size_t olen = 0;
	if (k == WC_HDROP) {
		char **paths = hdrop_paths((HDROP)h);
		if (paths) {
			int count = 0;
			while (paths[count])
				count++;
			out = lud__uri_list_encode((const char *const *)paths,
						   count, &olen);
			for (int i = 0; i < count; i++)
				free(paths[i]);
			free(paths);
		}
	} else {
		SIZE_T sz = GlobalSize(h);
		const void *src = GlobalLock(h);
		if (src) {
			if (k == WC_TEXT)
				out = w16_to_u8((const WCHAR *)src, &olen);
			else if (k == WC_HTML)
				out = cf_html_fragment((const char *)src, sz, &olen);
			else {
				out = malloc(sz ? sz : 1);
				if (out) {
					memcpy(out, src, sz);
					olen = sz;
				}
			}
			GlobalUnlock(h);
		}
	}

	if (out && len_out)
		*len_out = olen;
	return out;
}

/* ---- Public API ---- */

void *
lud_clipboard_get_data(const char *format, size_t *len_out)
{
	if (len_out)
		*len_out = 0;

	if (!OpenClipboard(lud__win32_window()))
		return NULL;

	HANDLE h = GetClipboardData(lud__win32_clip_cf(format));
	void *out = lud__win32_clip_decode(format, h, len_out);

	CloseClipboard();
	return out;
}

void
lud_clipboard_get_async(const char *format, lud_clipboard_cb cb, void *user)
{
	if (!cb)
		return;
	if (!format)
		format = LUD_CLIPBOARD_TEXT;
	/* Win32 clipboard reads are synchronous; share the sync path. */
	size_t len = 0;
	void *data = lud_clipboard_get_data(format, &len);
	cb(format, data, len, user);
	free(data);
}

int
lud_clipboard_set_data(const char *format, const void *data, size_t len)
{
	UINT cf;
	HGLOBAL h = win_build(format, data, len, &cf);
	if (!h)
		return LUD_ERR;
	if (!OpenClipboard(lud__win32_window())) {
		GlobalFree(h);
		return LUD_ERR;
	}
	EmptyClipboard();
	if (!SetClipboardData(cf, h)) {
		CloseClipboard();
		GlobalFree(h);
		return LUD_ERR;
	}
	CloseClipboard();
	return LUD_OK;
}

int
lud_clipboard_set_multi(const lud_clip_item_t *items, int count)
{
	if (!items || count <= 0)
		return LUD_ERR;
	if (!OpenClipboard(lud__win32_window()))
		return LUD_ERR;
	EmptyClipboard();

	int ok = 1;
	for (int i = 0; i < count && ok; i++) {
		UINT cf;
		HGLOBAL h = win_build(items[i].format, items[i].data,
				      items[i].len, &cf);
		if (!h || !SetClipboardData(cf, h)) {
			if (h)
				GlobalFree(h);
			ok = 0;
		}
	}

	CloseClipboard();
	return ok ? LUD_OK : LUD_ERR;
}

int
lud_clipboard_set_files(const char *const *paths, int count)
{
	if (!paths || count <= 0)
		return LUD_ERR;
	HGLOBAL h = hg_hdrop_from_paths(paths, count);
	if (!h)
		return LUD_ERR;
	if (!OpenClipboard(lud__win32_window())) {
		GlobalFree(h);
		return LUD_ERR;
	}
	EmptyClipboard();
	if (!SetClipboardData(CF_HDROP, h)) {
		CloseClipboard();
		GlobalFree(h);
		return LUD_ERR;
	}
	CloseClipboard();
	return LUD_OK;
}

char **
lud_clipboard_get_files(void)
{
	if (!OpenClipboard(lud__win32_window()))
		return NULL;
	HANDLE h = GetClipboardData(CF_HDROP);
	char **list = h ? hdrop_paths((HDROP)h) : NULL;
	CloseClipboard();
	if (list && !list[0]) { /* empty list: treat as none */
		free(list);
		return NULL;
	}
	return list;
}
