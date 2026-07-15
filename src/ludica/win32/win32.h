/*
 * win32.h — internal hooks shared between the Windows platform TUs.
 *
 * These are ludica-private (the lud__ prefix), not part of the public API.
 * platform_win32.c owns the window and OLE lifecycle; clipboard.c owns the
 * format mapping and decoders; dragdrop.c reuses both for the drop target.
 *
 * Made by a machine. PUBLIC DOMAIN (CC0-1.0)
 */
#ifndef LUDICA_WIN32_H
#define LUDICA_WIN32_H

#include <windows.h>
#include <stddef.h>

/* The application window (platform_win32.c). NULL before init / after shutdown;
 * OpenClipboard(NULL) and a NULL owner are both valid. */
HWND lud__win32_window(void);

/* Map a ludica MIME format to its native clipboard format id (clipboard.c).
 * Registers the format if needed, so the result is stable within a process. */
UINT lud__win32_clip_cf(const char *format);

/* Decode a clipboard/DnD handle for `format` into malloc'd bytes (clipboard.c).
 * `h` comes from GetClipboardData or IDataObject::GetData: an HDROP for a file
 * list (decoded to a text/uri-list), an HGLOBAL of UTF-16 for text, a CF_HTML
 * buffer for HTML, or raw bytes otherwise. Returns NULL on failure; on success
 * sets *len_out (excluding any added NUL) when len_out is non-NULL. */
void *lud__win32_clip_decode(const char *format, HANDLE h, size_t *len_out);

/* Drop target (dragdrop.c). register/unregister bracket the OLE lifetime;
 * frame_reset frees the previous frame's delivered drop buffer and is called
 * at the top of each poll so LUD_EV_DROP data stays valid for one frame. */
void lud__win32_dnd_register(HWND hwnd);
void lud__win32_dnd_unregister(HWND hwnd);
void lud__win32_dnd_frame_reset(void);

#endif /* LUDICA_WIN32_H */
