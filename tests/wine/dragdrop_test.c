/*
 * dragdrop_test.c — Windows drop-target round-trip test.
 *
 * Drives win32/dragdrop.c's IDropTarget the way OLE would, but in-process: it
 * builds a mock IDataObject offering a chosen clipboard format, then calls
 * DragEnter/Drop directly on the real drop target and checks the LUD_EV_DROP
 * event it pushes. This exercises format selection, the GetData pull, and the
 * shared decoders without a live drag from another window. Runs under Wine.
 *
 * Made by a machine. PUBLIC DOMAIN (CC0-1.0)
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ole2.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <ludica.h>

/* dragdrop.c hands the singleton drop target out through this internal hook. */
extern IDropTarget *lud__win32_drop_target(void);

/* ---- Captured event (our stub for lud__event_push) ---- */

static lud_event_t g_last;
static int g_have;

void
lud__event_push(const void *ev)
{
	memcpy(&g_last, ev, sizeof(g_last));
	g_have = 1;
}

/* ---- Helpers ---- */

static HGLOBAL
dup_hglobal(HGLOBAL src)
{
	SIZE_T n = GlobalSize(src);
	HGLOBAL dst = GlobalAlloc(GMEM_MOVEABLE, n);
	if (dst) {
		void *s = GlobalLock(src), *d = GlobalLock(dst);
		if (s && d)
			memcpy(d, s, n);
		GlobalUnlock(dst);
		GlobalUnlock(src);
	}
	return dst;
}

/* Build a CF_HDROP DROPFILES from wide paths (double-NUL terminated list). */
static HGLOBAL
make_hdrop(const WCHAR *const *paths, int count)
{
	SIZE_T chars = 1; /* final extra NUL */
	for (int i = 0; i < count; i++)
		chars += wcslen(paths[i]) + 1;
	SIZE_T bytes = sizeof(DROPFILES) + chars * sizeof(WCHAR);

	HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
	DROPFILES *df = GlobalLock(h);
	df->pFiles = sizeof(DROPFILES);
	df->pt.x = df->pt.y = 0;
	df->fNC = FALSE;
	df->fWide = TRUE;
	WCHAR *w = (WCHAR *)((char *)df + sizeof(DROPFILES));
	for (int i = 0; i < count; i++) {
		SIZE_T n = wcslen(paths[i]) + 1;
		memcpy(w, paths[i], n * sizeof(WCHAR));
		w += n;
	}
	*w = 0;
	GlobalUnlock(h);
	return h;
}

static HGLOBAL
make_wtext(const WCHAR *s)
{
	SIZE_T bytes = (wcslen(s) + 1) * sizeof(WCHAR);
	HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
	void *d = GlobalLock(h);
	memcpy(d, s, bytes);
	GlobalUnlock(h);
	return h;
}

/* ---- Mock IDataObject offering exactly one (cfFormat, HGLOBAL) ---- */

typedef struct {
	IDataObject iface;
	LONG ref;
	CLIPFORMAT cf;
	HGLOBAL h; /* owned by the mock; GetData returns copies */
} mock_dataobj;

static HRESULT STDMETHODCALLTYPE
mo_QueryInterface(IDataObject *this, REFIID riid, void **ppv)
{
	if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDataObject)) {
		*ppv = this;
		this->lpVtbl->AddRef(this);
		return S_OK;
	}
	*ppv = NULL;
	return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE mo_AddRef(IDataObject *this)
{ return (ULONG)InterlockedIncrement(&((mock_dataobj *)this)->ref); }
static ULONG STDMETHODCALLTYPE mo_Release(IDataObject *this)
{ return (ULONG)InterlockedDecrement(&((mock_dataobj *)this)->ref); }

static HRESULT STDMETHODCALLTYPE
mo_GetData(IDataObject *this, FORMATETC *fe, STGMEDIUM *stg)
{
	mock_dataobj *m = (mock_dataobj *)this;
	if (fe->cfFormat != m->cf || !(fe->tymed & TYMED_HGLOBAL))
		return DV_E_FORMATETC;
	memset(stg, 0, sizeof(*stg));
	stg->tymed = TYMED_HGLOBAL;
	stg->hGlobal = dup_hglobal(m->h); /* caller frees via ReleaseStgMedium */
	stg->pUnkForRelease = NULL;
	return stg->hGlobal ? S_OK : E_OUTOFMEMORY;
}
static HRESULT STDMETHODCALLTYPE
mo_QueryGetData(IDataObject *this, FORMATETC *fe)
{
	mock_dataobj *m = (mock_dataobj *)this;
	return (fe->cfFormat == m->cf && (fe->tymed & TYMED_HGLOBAL)) ? S_OK : S_FALSE;
}
static HRESULT STDMETHODCALLTYPE
mo_GetDataHere(IDataObject *this, FORMATETC *fe, STGMEDIUM *stg)
{ (void)this; (void)fe; (void)stg; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE
mo_GetCanonicalFormatEtc(IDataObject *this, FORMATETC *in, FORMATETC *out)
{ (void)this; (void)in; (void)out; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE
mo_SetData(IDataObject *this, FORMATETC *fe, STGMEDIUM *stg, BOOL own)
{ (void)this; (void)fe; (void)stg; (void)own; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE
mo_EnumFormatEtc(IDataObject *this, DWORD dir, IEnumFORMATETC **out)
{ (void)this; (void)dir; (void)out; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE
mo_DAdvise(IDataObject *this, FORMATETC *fe, DWORD adv, IAdviseSink *sink, DWORD *conn)
{ (void)this; (void)fe; (void)adv; (void)sink; (void)conn; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE
mo_DUnadvise(IDataObject *this, DWORD conn)
{ (void)this; (void)conn; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE
mo_EnumDAdvise(IDataObject *this, IEnumSTATDATA **out)
{ (void)this; (void)out; return E_NOTIMPL; }

static IDataObjectVtbl mo_vtbl = {
	mo_QueryInterface, mo_AddRef, mo_Release,
	mo_GetData, mo_GetDataHere, mo_QueryGetData, mo_GetCanonicalFormatEtc,
	mo_SetData, mo_EnumFormatEtc, mo_DAdvise, mo_DUnadvise, mo_EnumDAdvise,
};

static IDataObject *
mock_new(CLIPFORMAT cf, HGLOBAL h)
{
	mock_dataobj *m = calloc(1, sizeof(*m));
	m->iface.lpVtbl = &mo_vtbl;
	m->ref = 1;
	m->cf = cf;
	m->h = h;
	return (IDataObject *)m;
}

/* ---- Test harness ---- */

static int fails;
#define CHECK(cond, name) do { \
	int _c = (cond); printf("%-6s %s\n", _c ? "PASS" : "FAIL", name); \
	if (!_c) fails++; } while (0)

/* Push a data object through the real drop target and return the format the
 * app saw (or NULL if no drop event fired). Fills data/len from the event. */
static const char *
run_drop(IDataObject *pdo, const void **data, size_t *len)
{
	IDropTarget *dt = lud__win32_drop_target();
	POINTL pt = { 10, 20 };
	DWORD eff = DROPEFFECT_NONE;

	g_have = 0;
	dt->lpVtbl->DragEnter(dt, pdo, MK_LBUTTON, pt, &eff);
	dt->lpVtbl->Drop(dt, pdo, MK_LBUTTON, pt, &eff);
	if (!g_have)
		return NULL;
	if (data) *data = g_last.drop.data;
	if (len) *len = g_last.drop.len;
	return g_last.drop.format;
}

int
main(void)
{
	OleInitialize(NULL);

	UINT cf_hdrop = CF_HDROP;
	UINT cf_text = CF_UNICODETEXT;

	/* File drop -> text/uri-list, percent-encoded. */
	const WCHAR *paths[] = { L"C:\\tmp\\a b.txt", L"C:\\x\\file.png" };
	HGLOBAL hd = make_hdrop(paths, 2);
	IDataObject *pdo = mock_new((CLIPFORMAT)cf_hdrop, hd);
	const void *data = NULL; size_t len = 0;
	const char *fmt = run_drop(pdo, &data, &len);
	CHECK(fmt && !strcmp(fmt, LUD_CLIPBOARD_URI_LIST), "hdrop-format");
	CHECK(data && strstr((const char *)data, "a%20b.txt") != NULL, "hdrop-encoded");
	CHECK(data && strstr((const char *)data, "file.png") != NULL, "hdrop-second");
	CHECK(g_last.drop.x == 10 && g_last.drop.y == 20, "drop-point");
	pdo->lpVtbl->Release(pdo);
	GlobalFree(hd);

	/* Text drop -> UTF-8. */
	HGLOBAL ht = make_wtext(L"hi \x263A there");
	pdo = mock_new((CLIPFORMAT)cf_text, ht);
	data = NULL; len = 0;
	fmt = run_drop(pdo, &data, &len);
	CHECK(fmt && !strcmp(fmt, LUD_CLIPBOARD_TEXT), "text-format");
	CHECK(data && !strcmp((const char *)data, "hi \xE2\x98\xBA there"), "text-utf8");
	pdo->lpVtbl->Release(pdo);
	GlobalFree(ht);

	/* A data object offering nothing we handle: no drop event. */
	HGLOBAL hx = make_wtext(L"x");
	pdo = mock_new((CLIPFORMAT)RegisterClipboardFormatA("application/x-nope"), hx);
	fmt = run_drop(pdo, NULL, NULL);
	CHECK(fmt == NULL, "unknown-refused");
	pdo->lpVtbl->Release(pdo);
	GlobalFree(hx);

	OleUninitialize();
	printf("%d failure(s)\n", fails);
	return fails ? 1 : 0;
}
