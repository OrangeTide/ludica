/*
 * win_stub.c — the few platform hooks the win32 clipboard/DnD TUs import.
 *
 * The clipboard code passes the app window to OpenClipboard as the owner.
 * The test has no window, and passing NULL is valid (OpenClipboard(NULL)
 * associates the clipboard with the current task), so this returns NULL.
 * The drop target logs registration failures through lud_log, never hit here.
 * These are the only symbols the clipboard/DnD TUs import from platform_win32.c
 * and log.c, which is why they can be tested without EGL or the window loop.
 *
 * Made by a machine. PUBLIC DOMAIN (CC0-1.0)
 */
#include <windows.h>

HWND
lud__win32_window(void)
{
	return NULL;
}

void lud_log(const char *fmt, ...) { (void)fmt; }
void lud_err(const char *fmt, ...) { (void)fmt; }
