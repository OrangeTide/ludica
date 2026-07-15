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

/* ---- Drag source: not yet wired (would use DoDragDrop / IDropSource) ---- */

int
lud_drag_data(const char *format, const void *data, size_t len)
{
	(void)format; (void)data; (void)len;
	return LUD_ERR;
}

int
lud_drag_files(const char *const *paths, int count)
{
	(void)paths; (void)count;
	return LUD_ERR;
}

int
lud_drag_multi(const lud_clip_item_t *items, int count)
{
	(void)items; (void)count;
	return LUD_ERR;
}
