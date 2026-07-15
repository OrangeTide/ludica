/*
 * win_stub.c — the one platform hook win32/clipboard.c needs.
 *
 * The clipboard code passes the app window to OpenClipboard as the owner.
 * The test has no window, and passing NULL is valid (OpenClipboard(NULL)
 * associates the clipboard with the current task), so this returns NULL.
 * This is the only symbol the clipboard TU imports from platform_win32.c,
 * which is why the clipboard can be tested without EGL or the window loop.
 *
 * Made by a machine. PUBLIC DOMAIN (CC0-1.0)
 */
#include <windows.h>

HWND
lud__win32_window(void)
{
	return NULL;
}
