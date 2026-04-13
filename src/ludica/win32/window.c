#ifndef UNICODE
# define UNICODE
# define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <timeapi.h>
#include <tchar.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>

#include "ludica.h"
#include "log.h"

/**********************************************************************/

#define EGL_PLATFORM_ANGLE_ANGLE                           0x3202
#define EGL_PLATFORM_ANGLE_TYPE_ANGLE                      0x3203
#define EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE               0x3450

#ifndef LUD_MAX_WINDOWS
#define LUD_MAX_WINDOWS 4
#endif

#define LUD_WAIT_THRESHOLD_MS 3

#define lud_egl_check() do { \
		GLenum e = eglGetError(); \
		if (e != EGL_SUCCESS) {\
			log_error("%s:%d:%s():EGL error = 0x%04x\n", __FILE__, __LINE__, __func__, e); \
			abort(); \
		} \
	} while(0)

#if 0
enum {
	LUD_WM_APP_INITDONE = WM_APP, /* unblock rendering thread */
	LUD_WM_APP_PAUSE, /* pauses rendering */
	LUD_WM_APP_CONTINUE, /* continues rendering */
	LUD_WM_APP_STOP, /* exit rendering thread */
	LUD_WM_APP_RESIZE, /* resize viewport */
};
#endif

struct win_info {
	BOOL alive;
	BOOL bFullscreen;
	HWND hWnd;
	EGLDisplay eglDisplay;
	EGLContext eglContext;
	EGLSurface surface;
	struct window_callback_functions callback;
	// TODO: INT config_min_width, config_min_height; /* restrict window resize if a mininum size is set */
};

/**********************************************************************/

static int current_index = LUD_ERR;
static struct win_info window[LUD_MAX_WINDOWS];
static int default_width = 640, default_height = 480;
static LONGLONG qpcFrequency;
static BOOL fQpc = FALSE;      // use QueryPerformanceCounters if TRUE
static BOOL dirty_flag = TRUE;
static const DWORD fps = 25;
static DWORD tFrame;
static LONGLONG tNext;
int terminate_flag;

/**********************************************************************/

static void errormsg(const char *m, ...)
{
	static char buf[1024];
	va_list ap;
	va_start(ap, m);
	snprintf(buf, sizeof(buf), m, ap);
	va_end(ap);
	MessageBoxA(NULL, buf, "Error", MB_OK);
}

/* find next unused window */
static int
find_next_window(void)
{
	int i;
	for (i = 0; i < LUD_MAX_WINDOWS; i++) {
		if (window[i].alive == FALSE) {
			return i;
		}
	}

	return LUD_ERR;
}

/* find index of matching window handle */
static int
find_win(HWND hWnd) {
	int i;
	for (i = 0; i < LUD_MAX_WINDOWS; i++) {
		if (window[i].alive == TRUE && window[i].hWnd == hWnd) {
			return i;
		}
	}

	return LUD_ERR;
}

static void
render_pause(void)
{
	// TODO: PostThreadMessage(_nRenderThreadID, TINYLIB_WM_APP_PAUSE, TRUE, 0);
}

static void
render_continue(void)
{
	// TODO: PostThreadMessage(_nRenderThreadID, TINYLIB_WM_APP_CONTINUE, FALSE, 0);
}

int
window_select(int num)
{
	if (num < 0 || num > LUD_MAX_WINDOWS) {
		return LUD_ERR;
	}

	/* skip if we've cached */
	if (current_index == num) {
		return LUD_OK;
	}

	struct win_info *info = &window[num];
	if (info->alive == FALSE) {
		return LUD_ERR;
	}
	current_index = num;
	if (info->surface && info->eglContext) {
		eglMakeCurrent(info->eglDisplay, info->surface, info->surface, info->eglContext);
		log_debug("Selected window #%d context", num);
	} else {
		eglMakeCurrent(info->eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		log_debug("No Selected window context");
	}

	return LUD_OK;
}

static LRESULT CALLBACK
MainWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
	int cur = find_win(hWnd);
	struct win_info *info = cur == LUD_ERR ? NULL : &window[cur];
	switch (Msg) {

#if 0 // TODO: support a render thread
	case LUD_WM_APP_INITDONE:
		/* render thread is ready */
		if (wParam) { /* full screen window? */
			ShowWindow(hWnd, SW_MAXIMIZE);
		} else {
			ShowWindow(hWnd, SW_SHOW);
		}

		/* take the focus */
		SetForegroundWindow(hWnd);
		SetFocus(hWnd);

		return 1; /* handled */
#endif

#if 0 // TODO: implement this
	case WM_GETMINMAXINFO:
		/* restrict window resize if a mininum size is set */
		if (window[cur].config_min_height > 0 && window[cur].config_min_width > 0) {
			MINMAXINFO *pInfo = (MINMAXINFO *)lParam;
			pInfo->ptMinTrackSize.x = window[cur].config_min_width;
			pInfo->ptMinTrackSize.y = window[cur].config_min_height;
		}
		break;
#endif

	case WM_SIZE:

		/* pause rendering on minimize */
		switch (wParam) {
		case SIZE_MINIMIZED:
			/* pause rendering while minimized */
			render_pause();
			break;

		case SIZE_RESTORED:
			/* tell the render thread to resize viewport */
			// TODO: PostThreadMessage(_nRenderThreadID, LUD_WM_APP_RESIZE, 0, 0);

			/* unblock render thread */
			render_continue();
			break;
		}

		break;

	case WM_ENTERMENULOOP:
		/* suspend while a menu is accessed */
		if (!IsIconic(hWnd)) {
			render_pause();
		}
		break;

	case WM_EXITMENULOOP:
		/* restore from the menu triggered suspend */
		if (!IsIconic(hWnd)) {
			render_continue();
		}
		break;

	case WM_KEYDOWN:
		// TODO: do this
		break;

	case WM_CLOSE:
		// TODO: save window size and position

		/* terminate render thread */
		// TODO: PostThreadMessage(_nRenderThreadID, LUD_WM_APP_STOP, 0, 0);
		// TODO: WaitForSingleObject(_hRenderThread, 5000);

		if (info && info->eglDisplay != EGL_NO_DISPLAY) {
			if (current_index == cur) {
				eglMakeCurrent(info->eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
			}
			if (info->eglContext != EGL_NO_CONTEXT) {
				eglDestroyContext(info->eglDisplay, info->eglContext);
				info->eglContext = EGL_NO_CONTEXT;
			}
			if (info->surface != EGL_NO_SURFACE) {
				eglDestroySurface(info->eglDisplay, info->surface);
				info->surface = EGL_NO_SURFACE;
			}
			eglTerminate(info->eglDisplay);
		}
		DestroyWindow(hWnd);
		return 0;

	case WM_DESTROY:
		log_debug("Good-Bye!");
		terminate_flag = TRUE;
		PostQuitMessage(0);
		break;
	}

	return DefWindowProc(hWnd, Msg, wParam, lParam);
}

int
window_new(const struct window_callback_functions *callback)
{
	const char *title = "TODO"; // TODO: configure this
	static ATOM wndcls;
	static HINSTANCE hInstance;

	if (!hInstance) {
		hInstance = GetModuleHandle(NULL);
	}

	if (!wndcls) {
		WNDCLASSEX wcx = { 0 };

		wcx.cbSize = sizeof(wcx);
		wcx.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
		wcx.lpfnWndProc = MainWndProc;
		wcx.cbClsExtra = 0;
		wcx.cbWndExtra = 0;
		wcx.hInstance = hInstance;
		wcx.hIcon = LoadIcon(NULL, IDI_APPLICATION);
		wcx.hCursor = LoadCursor(NULL, IDC_ARROW);
		wcx.hbrBackground = GetStockObject(WHITE_BRUSH);
		wcx.lpszMenuName = NULL;
		wcx.lpszClassName = TEXT(__FILE__);
		wcx.hIconSm = LoadImage(hInstance, MAKEINTRESOURCE(5),
					IMAGE_ICON,
					GetSystemMetrics(SM_CXSMICON),
					GetSystemMetrics(SM_CYSMICON),
					LR_DEFAULTCOLOR);

		wndcls = RegisterClassEx(&wcx);
		if (!wndcls) {
			errormsg("RegisterClassEx() failed: Cannot register window class.");
			return LUD_ERR;
		}
	}

	int win_index = find_next_window();

	if (win_index == LUD_ERR) {
		errormsg("Too many open windows.");
		return LUD_ERR;
	}

#ifdef UNICODE
        static WCHAR title_buf[256];
        int len;
        len = MultiByteToWideChar(CP_UTF8, 0, title, -1, title_buf, sizeof(title_buf) / sizeof(*title_buf));
        if (!len) {
		title_buf[0] = 0; // ERROR
		errormsg("Invalidate window title string");
		return LUD_ERR;
	}
#else
        char *title_buf = title; // pass it through
	// TODO: utf8 to local encoding
#endif

	RECT rect = { 0, 0, default_width, default_height };
	int style = WS_BORDER | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME;
	AdjustWindowRect(&rect, style, FALSE);

	HWND hwnd = CreateWindow(MAKEINTATOM(wndcls), title_buf,
		WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT,
		rect.right - rect.left, rect.bottom - rect.top,
		NULL, NULL, hInstance, NULL);

	if (hwnd == NULL) {
		errormsg("CreateWindow() failed: Cannot create OpenGL window.");
		return LUD_ERR;
	}

	HDC hdc = GetDC(hwnd);

	/* add the new window to the list */
	window[win_index] = (struct win_info){ .hWnd = hwnd, .callback = *callback };
	struct win_info *info = &window[win_index];

	// TODO: query extensions of eglGetDisplay(EGL_NO_DISPLAY)

	// Create EGL display connection
	EGLDisplay eglDisplay = eglGetDisplay(hdc);
	if (eglDisplay == EGL_NO_DISPLAY) {
		errormsg("Unable to find EGL display");
		info->alive = FALSE;
		DestroyWindow(hwnd);
		return LUD_ERR;
	}

	// Initialize EGL for this display, returns EGL version
	EGLint major, minor;
	eglInitialize(eglDisplay, &major, &minor);
	eglBindAPI(EGL_OPENGL_ES_API);
	log_debug("EGL %u.%u version: %s (%s)", major, minor,
		eglQueryString(eglDisplay, EGL_VERSION),
		eglQueryString(eglDisplay, EGL_VENDOR));
	lud_egl_check();

	EGLint configAttributes[] = {
		EGL_BUFFER_SIZE, 0,
		EGL_RED_SIZE, 5,
		EGL_GREEN_SIZE, 6,
		EGL_BLUE_SIZE, 5,
		EGL_ALPHA_SIZE, 0,
		EGL_COLOR_BUFFER_TYPE, EGL_RGB_BUFFER,
		EGL_DEPTH_SIZE, 24,
		EGL_LEVEL, 0,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_SAMPLE_BUFFERS, 0,
		EGL_SAMPLES, 0,
		EGL_STENCIL_SIZE, 0,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_TRANSPARENT_TYPE, EGL_NONE,
		EGL_TRANSPARENT_RED_VALUE, EGL_DONT_CARE,
		EGL_TRANSPARENT_GREEN_VALUE, EGL_DONT_CARE,
		EGL_TRANSPARENT_BLUE_VALUE, EGL_DONT_CARE,
		EGL_CONFIG_CAVEAT, EGL_DONT_CARE,
		EGL_CONFIG_ID, EGL_DONT_CARE,
		EGL_MAX_SWAP_INTERVAL, EGL_DONT_CARE,
		EGL_MIN_SWAP_INTERVAL, EGL_DONT_CARE,
		EGL_NATIVE_RENDERABLE, EGL_DONT_CARE,
		EGL_NATIVE_VISUAL_TYPE, EGL_DONT_CARE,
		// EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE,
		EGL_NONE
	};

	EGLint numConfigs;
	EGLConfig windowConfig;
	eglChooseConfig(eglDisplay, configAttributes, &windowConfig, 1, &numConfigs);

	EGLint surfaceAttributes[] = { EGL_NONE };
	EGLSurface eglSurface = eglCreateWindowSurface(eglDisplay, windowConfig, hwnd, surfaceAttributes);

	lud_egl_check();

	EGLint contextAttributes[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext eglContext = eglCreateContext(eglDisplay, windowConfig, NULL, contextAttributes);

	if (eglContext == EGL_NO_CONTEXT) {
		errormsg("Unable to create EGL context");
		info->alive = FALSE;
		eglTerminate(eglDisplay);
		DestroyWindow(hwnd);
		return LUD_ERR;
	}

	info->alive = TRUE;
	info->surface = eglSurface;
	info->eglContext = eglContext;
	info->eglDisplay = eglDisplay;

	/* select the window - see also window_select() */
	eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
	current_index = win_index;

	lud_egl_check();

	log_debug("win_index=%d context=%p", win_index, eglContext);

	// TODO: move GL context creation to this thread
	// _hRenderThread = (HANDLE)_beginthreadex(NULL, 0, RenderMain, &args, 0, &_nRenderThreadID);

	// TODO: clean up old window and old context

#if 1 // TODO: remove this once we have implemented a render thread
	ShowWindow(hwnd, SW_SHOW);

	/* take the focus */
	SetForegroundWindow(hwnd);
	SetFocus(hwnd);
#endif

	return win_index;
}

int
display_init(void)
{
        if (QueryPerformanceFrequency((LARGE_INTEGER *) &qpcFrequency)) {
            fQpc = TRUE;
        } else {
            fQpc = FALSE;
        }

        /* initialize tNext so that it is < tCurrent in the first iteration of the loop */
        if (fQpc) {
                tFrame = qpcFrequency / fps;
                QueryPerformanceCounter((LARGE_INTEGER *) &tNext);
        } else {
                tFrame = 1000 / fps;
                tNext = timeGetTime(); // TODO: handle wrap around
        }

	return LUD_OK;
}

void
display_done(void)
{
	unsigned i;

	for (i = 0; i < LUD_MAX_WINDOWS; i++) {
		struct win_info *info = &window[i];
		if (info->alive == TRUE) {
			log_debug("TODO: clean up window %d", i);

			if (info->eglDisplay != EGL_NO_DISPLAY) {
				eglMakeCurrent(info->eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

				eglDestroyContext(info->eglDisplay, info->eglContext);
				info->eglContext = EGL_NO_CONTEXT;

				eglDestroySurface(info->eglDisplay, info->surface);
				info->surface = EGL_NO_SURFACE;

				eglTerminate(info->eglDisplay);
				info->eglDisplay = EGL_NO_DISPLAY;
			}
		}
	}
}

void
process_events(void)
{
	MSG uMsg;
        LONGLONG tCurrent;

        if (PeekMessage(&uMsg, NULL, 0, 0, PM_REMOVE) > 0) {
                log_debug("MSG %d", uMsg.message);
                TranslateMessage(&uMsg);
                DispatchMessage(&uMsg);
                if (uMsg.message == WM_QUIT) {
                        terminate_flag = TRUE;
                        return;
                }
        } else {
                if (fQpc) {
                        QueryPerformanceCounter((LARGE_INTEGER *) &tCurrent);
                } else {
                        tCurrent = timeGetTime();
                }

                /* render when the frame time has elapsed */
		if (tCurrent >= tNext) {

                        dirty_flag = TRUE;

                        /* move forward to next frame */
                        tNext += tFrame;

                        /* drop a frames if we are behind the current frame */
                        if (tNext < tCurrent) {
                                tNext = tCurrent + tFrame;
                        }
                } else {
			/* try to sleep to near the end of the frame */
			LONGLONG tRemaining = tNext - tCurrent ;
			if (fQpc) {
				tRemaining /= qpcFrequency;
			}

			if (tRemaining >= LUD_WAIT_THRESHOLD_MS) {
				MsgWaitForMultipleObjects(0, NULL, FALSE, tRemaining, QS_ALLEVENTS);
			}
		}
        }
}

void
paint_all(void)
{
	unsigned i;
	int old_index = current_index;

        if (!dirty_flag) {
                return;
        }
        dirty_flag = FALSE;

	for (i = 0; i < LUD_MAX_WINDOWS; i++) {
		struct win_info *info = &window[i];
		window_select(i);
		if (info->alive == TRUE && info->eglDisplay != EGL_NO_DISPLAY) {
			if (window_select(i) == LUD_OK) {
				if (info->callback.paint) {
					info->callback.paint();
				}
				eglSwapBuffers(info->eglDisplay, info->surface);
			}
		}
	}
	window_select(old_index);
}

int
lookup_key(const char *s)
{
	const struct { BYTE code; char name[15]; } codes[] = {
		{ VK_LEFT, "Left" },
		{ VK_UP, "Up" },
		{ VK_RIGHT, "Right" },
		{ VK_DOWN, "Down" },
		{ VK_OEM_4, "[{" }, // TODO: use the X11 name
		{ VK_OEM_5, "\\|" }, // TODO: use the X11 name
		{ VK_OEM_6, "]}" }, // TODO: use the X11 name
	};

	unsigned i;
	for (i = 0; i < sizeof(codes) / sizeof(*codes); i++) {
		// TODO: strcasecmp()
		if (!strcmp(codes[i].name, s)) {
			return codes[i].code;
		}
	}

	/* take a guess at single letter codes - not always correct! */
	if (s[0] || !s[1]) {
		return toupper(s[0]);
	}

	return -1;
}

int WINAPI
wWinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PWSTR pCmdLine, int nCmdShow)
{
	(void)hInstance;
	(void)prevInstance;
	(void)pCmdLine;
	(void)nCmdShow;

	// TODO: load and initialize libraries

	extern int main(int argc, char *argv[]);
	char *argv[] = { "TODO" }; // TODO: implement this
	return main(1, argv);
}
