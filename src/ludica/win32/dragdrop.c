/*
 * win32/dragdrop.c — Windows drag-and-drop for ludica.
 *
 * Drop target: the window registers an OLE IDropTarget (RegisterDragDrop), so
 * a drag from any application can drop onto it. When the drop lands, the data
 * is pulled from the source IDataObject and decoded with the same format
 * mapping as the clipboard (win32/clipboard.c), then delivered to the app as a
 * LUD_EV_DROP event. This is the Windows twin of the XDND drop target in
 * platform_x11.c.
 *
 * Drop delivery mirrors the X11 lifetime: the decoded bytes are owned here and
 * stay valid for one frame. lud__win32_dnd_frame_reset(), called at the top of
 * each poll, frees the previous frame's buffer.
 *
 * Drag source (lud_drag_*) is not wired yet. It would build an IDataObject and
 * an IDropSource and call DoDragDrop; the stubs below return LUD_ERR until then.
 *
 * Made by a machine. PUBLIC DOMAIN (CC0-1.0)
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ole2.h>
#include <shlobj.h>   /* SHCreateStdEnumFmtEtc */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ludica_internal.h"
#include "win32.h"

/* Formats we accept on a drop, most specific first. Each maps to a native
 * clipboard format via lud__win32_clip_cf and is delivered under this MIME
 * name, matching the XDND drop target's advertised targets. */
static const char *const drop_candidates[] = {
	LUD_CLIPBOARD_URI_LIST, /* CF_HDROP: a file list */
	LUD_CLIPBOARD_PNG,      /* registered "PNG": an image */
	LUD_CLIPBOARD_HTML,     /* "HTML Format": rich text */
	LUD_CLIPBOARD_TEXT,     /* CF_UNICODETEXT: plain text */
};

/* Return the first candidate the data object can supply as an HGLOBAL, or NULL
 * if it offers none of them. */
static const char *
pick_format(IDataObject *pdo)
{
	for (size_t i = 0; i < sizeof(drop_candidates) / sizeof(drop_candidates[0]); i++) {
		FORMATETC fe;
		memset(&fe, 0, sizeof(fe));
		fe.cfFormat = (CLIPFORMAT)lud__win32_clip_cf(drop_candidates[i]);
		fe.dwAspect = DVASPECT_CONTENT;
		fe.lindex = -1;
		fe.tymed = TYMED_HGLOBAL;
		if (pdo->lpVtbl->QueryGetData(pdo, &fe) == S_OK)
			return drop_candidates[i];
	}
	return NULL;
}

/* ---- One-frame drop buffer ---- */

static void *g_drop_data; /* delivered to the app; freed next frame */

void
lud__win32_dnd_frame_reset(void)
{
	free(g_drop_data);
	g_drop_data = NULL;
}

/* Take ownership of `data` (may be NULL on decode failure) and hand it to the
 * app as a LUD_EV_DROP at the client-relative drop point. */
static void
deliver(HWND hwnd, const char *fmt, void *data, size_t len, POINTL screen_pt)
{
	free(g_drop_data);
	g_drop_data = data;
	if (!data)
		return;

	POINT p = { screen_pt.x, screen_pt.y };
	if (hwnd)
		ScreenToClient(hwnd, &p);

	lud_event_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = LUD_EV_DROP;
	ev.drop.format = fmt;
	ev.drop.data = data;
	ev.drop.len = len;
	ev.drop.x = p.x;
	ev.drop.y = p.y;
	lud__event_push(&ev);
}

/* ---- IDropTarget COM object (single window, single static instance) ---- */

typedef struct {
	IDropTarget iface; /* lpVtbl first: castable to IDropTarget * */
	LONG ref;
	HWND hwnd;
	const char *fmt; /* format chosen at DragEnter, used at Drop */
} drop_target;

static HRESULT STDMETHODCALLTYPE
dt_QueryInterface(IDropTarget *this, REFIID riid, void **ppv)
{
	if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDropTarget)) {
		*ppv = this;
		this->lpVtbl->AddRef(this);
		return S_OK;
	}
	*ppv = NULL;
	return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
dt_AddRef(IDropTarget *this)
{
	return (ULONG)InterlockedIncrement(&((drop_target *)this)->ref);
}

static ULONG STDMETHODCALLTYPE
dt_Release(IDropTarget *this)
{
	/* Static singleton: refcount is tracked but the object is never freed. */
	return (ULONG)InterlockedDecrement(&((drop_target *)this)->ref);
}

static HRESULT STDMETHODCALLTYPE
dt_DragEnter(IDropTarget *this, IDataObject *pdo, DWORD keys, POINTL pt, DWORD *eff)
{
	drop_target *dt = (drop_target *)this;
	(void)keys; (void)pt;
	dt->fmt = pick_format(pdo);
	*eff = dt->fmt ? DROPEFFECT_COPY : DROPEFFECT_NONE;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE
dt_DragOver(IDropTarget *this, DWORD keys, POINTL pt, DWORD *eff)
{
	drop_target *dt = (drop_target *)this;
	(void)keys; (void)pt;
	*eff = dt->fmt ? DROPEFFECT_COPY : DROPEFFECT_NONE;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE
dt_DragLeave(IDropTarget *this)
{
	((drop_target *)this)->fmt = NULL;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE
dt_Drop(IDropTarget *this, IDataObject *pdo, DWORD keys, POINTL pt, DWORD *eff)
{
	drop_target *dt = (drop_target *)this;
	(void)keys;
	const char *fmt = dt->fmt ? dt->fmt : pick_format(pdo);
	dt->fmt = NULL;
	if (!fmt) {
		*eff = DROPEFFECT_NONE;
		return S_OK;
	}

	FORMATETC fe;
	memset(&fe, 0, sizeof(fe));
	fe.cfFormat = (CLIPFORMAT)lud__win32_clip_cf(fmt);
	fe.dwAspect = DVASPECT_CONTENT;
	fe.lindex = -1;
	fe.tymed = TYMED_HGLOBAL;

	STGMEDIUM stg;
	memset(&stg, 0, sizeof(stg));
	if (pdo->lpVtbl->GetData(pdo, &fe, &stg) != S_OK) {
		*eff = DROPEFFECT_NONE;
		return S_OK;
	}

	size_t len = 0;
	void *data = lud__win32_clip_decode(fmt, stg.hGlobal, &len);
	ReleaseStgMedium(&stg);

	deliver(dt->hwnd, fmt, data, len, pt);
	*eff = data ? DROPEFFECT_COPY : DROPEFFECT_NONE;
	return S_OK;
}

static IDropTargetVtbl dt_vtbl = {
	dt_QueryInterface,
	dt_AddRef,
	dt_Release,
	dt_DragEnter,
	dt_DragOver,
	dt_DragLeave,
	dt_Drop,
};

static drop_target g_target = { { &dt_vtbl }, 1, NULL, NULL };

/* Internal accessor: the drop target singleton, for platform_win32.c to
 * register and for the Wine test to drive Drop() directly. */
IDropTarget *
lud__win32_drop_target(void)
{
	return (IDropTarget *)&g_target;
}

/* ---- Registration (called by platform_win32.c) ---- */

void
lud__win32_dnd_register(HWND hwnd)
{
	if (FAILED(OleInitialize(NULL))) {
		lud_log("OleInitialize failed; drag-and-drop disabled");
		return;
	}
	g_target.hwnd = hwnd;
	HRESULT hr = RegisterDragDrop(hwnd, lud__win32_drop_target());
	if (FAILED(hr))
		lud_log("RegisterDragDrop failed (0x%08lx)", (unsigned long)hr);
}

void
lud__win32_dnd_unregister(HWND hwnd)
{
	RevokeDragDrop(hwnd);
	free(g_drop_data);
	g_drop_data = NULL;
	g_target.hwnd = NULL;
	OleUninitialize();
}

/* ---- Drag source (DoDragDrop) ----
 *
 * Unlike the X11 drag source, which is non-blocking and driven from the poll
 * loop, DoDragDrop runs its own modal message loop: lud_drag_* blocks until the
 * user drops or cancels, then pushes LUD_EV_DRAG_END. The event still arrives,
 * so app code that waits for it stays portable; the app's frame loop is simply
 * paused for the duration of the drag, as it is for any Windows drag source.
 */

#define DRAG_MAX 8

/* Copy an HGLOBAL so a caller can own (and free) it independently. */
static HGLOBAL
dup_hglobal(HGLOBAL src)
{
	SIZE_T n = GlobalSize(src);
	HGLOBAL dst = GlobalAlloc(GMEM_MOVEABLE, n);
	if (dst) {
		void *s = GlobalLock(src), *d = GlobalLock(dst);
		if (s && d)
			memcpy(d, s, n);
		if (d) GlobalUnlock(dst);
		if (s) GlobalUnlock(src);
	}
	return dst;
}

/* IDataObject carrying the offered (format, medium) pairs. */
typedef struct {
	IDataObject iface; /* lpVtbl first */
	LONG ref;
	int n;
	FORMATETC fmt[DRAG_MAX];
	STGMEDIUM med[DRAG_MAX]; /* HGLOBALs owned here; GetData returns copies */
} drag_dataobj;

static int
do_find(drag_dataobj *o, const FORMATETC *fe)
{
	for (int i = 0; i < o->n; i++)
		if (o->fmt[i].cfFormat == fe->cfFormat &&
		    (fe->tymed & TYMED_HGLOBAL) &&
		    fe->dwAspect == DVASPECT_CONTENT)
			return i;
	return -1;
}

static HRESULT STDMETHODCALLTYPE
do_QueryInterface(IDataObject *this, REFIID riid, void **ppv)
{
	if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDataObject)) {
		*ppv = this;
		this->lpVtbl->AddRef(this);
		return S_OK;
	}
	*ppv = NULL;
	return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
do_AddRef(IDataObject *this)
{
	return (ULONG)InterlockedIncrement(&((drag_dataobj *)this)->ref);
}

static ULONG STDMETHODCALLTYPE
do_Release(IDataObject *this)
{
	drag_dataobj *o = (drag_dataobj *)this;
	LONG r = InterlockedDecrement(&o->ref);
	if (r == 0) {
		for (int i = 0; i < o->n; i++)
			ReleaseStgMedium(&o->med[i]);
		free(o);
	}
	return (ULONG)r;
}

static HRESULT STDMETHODCALLTYPE
do_GetData(IDataObject *this, FORMATETC *fe, STGMEDIUM *stg)
{
	drag_dataobj *o = (drag_dataobj *)this;
	int i = do_find(o, fe);
	if (i < 0)
		return DV_E_FORMATETC;
	memset(stg, 0, sizeof(*stg));
	stg->tymed = TYMED_HGLOBAL;
	stg->hGlobal = dup_hglobal(o->med[i].hGlobal);
	return stg->hGlobal ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE
do_QueryGetData(IDataObject *this, FORMATETC *fe)
{
	return do_find((drag_dataobj *)this, fe) >= 0 ? S_OK : DV_E_FORMATETC;
}

static HRESULT STDMETHODCALLTYPE
do_EnumFormatEtc(IDataObject *this, DWORD dir, IEnumFORMATETC **out)
{
	drag_dataobj *o = (drag_dataobj *)this;
	if (dir != DATADIR_GET)
		return E_NOTIMPL;
	return SHCreateStdEnumFmtEtc((UINT)o->n, o->fmt, out);
}

static HRESULT STDMETHODCALLTYPE
do_GetDataHere(IDataObject *this, FORMATETC *fe, STGMEDIUM *stg)
{ (void)this; (void)fe; (void)stg; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE
do_GetCanonicalFormatEtc(IDataObject *this, FORMATETC *in, FORMATETC *out)
{ (void)this; (void)in; if (out) memset(out, 0, sizeof(*out)); return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE
do_SetData(IDataObject *this, FORMATETC *fe, STGMEDIUM *stg, BOOL own)
{ (void)this; (void)fe; (void)stg; (void)own; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE
do_DAdvise(IDataObject *this, FORMATETC *fe, DWORD adv, IAdviseSink *sink, DWORD *conn)
{ (void)this; (void)fe; (void)adv; (void)sink; (void)conn; return OLE_E_ADVISENOTSUPPORTED; }
static HRESULT STDMETHODCALLTYPE
do_DUnadvise(IDataObject *this, DWORD conn)
{ (void)this; (void)conn; return OLE_E_ADVISENOTSUPPORTED; }
static HRESULT STDMETHODCALLTYPE
do_EnumDAdvise(IDataObject *this, IEnumSTATDATA **out)
{ (void)this; (void)out; return OLE_E_ADVISENOTSUPPORTED; }

static IDataObjectVtbl do_vtbl = {
	do_QueryInterface, do_AddRef, do_Release,
	do_GetData, do_GetDataHere, do_QueryGetData, do_GetCanonicalFormatEtc,
	do_SetData, do_EnumFormatEtc, do_DAdvise, do_DUnadvise, do_EnumDAdvise,
};

struct IDataObject *
lud__win32_make_dataobj(const lud_clip_item_t *items, int count)
{
	if (!items || count < 1 || count > DRAG_MAX)
		return NULL;

	drag_dataobj *o = calloc(1, sizeof(*o));
	if (!o)
		return NULL;
	o->iface.lpVtbl = &do_vtbl;
	o->ref = 1;

	for (int i = 0; i < count; i++) {
		UINT cf = 0;
		HGLOBAL h = lud__win32_clip_build(items[i].format, items[i].data,
						  items[i].len, &cf);
		if (!h) {
			o->iface.lpVtbl->Release(&o->iface);
			return NULL;
		}
		FORMATETC fe = { (CLIPFORMAT)cf, NULL, DVASPECT_CONTENT, -1,
				 TYMED_HGLOBAL };
		o->fmt[o->n] = fe;
		o->med[o->n].tymed = TYMED_HGLOBAL;
		o->med[o->n].hGlobal = h;
		o->med[o->n].pUnkForRelease = NULL;
		o->n++;
	}
	return &o->iface;
}

/* ---- IDropSource (stateless singleton) ---- */

static HRESULT STDMETHODCALLTYPE
ds_QueryInterface(IDropSource *this, REFIID riid, void **ppv)
{
	if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDropSource)) {
		*ppv = this;
		this->lpVtbl->AddRef(this);
		return S_OK;
	}
	*ppv = NULL;
	return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE ds_AddRef(IDropSource *this) { (void)this; return 2; }
static ULONG STDMETHODCALLTYPE ds_Release(IDropSource *this) { (void)this; return 1; }

static HRESULT STDMETHODCALLTYPE
ds_QueryContinueDrag(IDropSource *this, BOOL esc, DWORD keys)
{
	(void)this;
	if (esc)
		return DRAGDROP_S_CANCEL;
	if (!(keys & MK_LBUTTON))
		return DRAGDROP_S_DROP; /* button released: complete the drop */
	return S_OK;
}
static HRESULT STDMETHODCALLTYPE
ds_GiveFeedback(IDropSource *this, DWORD effect)
{
	(void)this; (void)effect;
	return DRAGDROP_S_USEDEFAULTCURSORS;
}

static IDropSourceVtbl ds_vtbl = {
	ds_QueryInterface, ds_AddRef, ds_Release,
	ds_QueryContinueDrag, ds_GiveFeedback,
};
static IDropSource g_source = { &ds_vtbl };

/* ---- Public drag-source API ---- */

int
lud_drag_multi(const lud_clip_item_t *items, int count)
{
	IDataObject *pdo = lud__win32_make_dataobj(items, count);
	if (!pdo)
		return LUD_ERR;

	DWORD effect = DROPEFFECT_NONE;
	HRESULT hr = DoDragDrop(pdo, &g_source, DROPEFFECT_COPY, &effect);
	pdo->lpVtbl->Release(pdo);

	int accepted = (hr == DRAGDROP_S_DROP && effect != DROPEFFECT_NONE);
	lud_event_t ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = LUD_EV_DRAG_END;
	ev.drag_end.accepted = accepted;
	lud__event_push(&ev);

	return (hr == DRAGDROP_S_DROP || hr == DRAGDROP_S_CANCEL) ? LUD_OK : LUD_ERR;
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
