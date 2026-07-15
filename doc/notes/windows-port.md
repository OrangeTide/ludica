# Windows Port Notes

Status and design notes for the ludica Windows backend. See also
`platform-gles.md` for the GLES2/GLES3 architecture these notes assume.

## Status

`src/ludica/platform_win32.c` brings the Windows backend up to the current
platform interface. It mirrors `platform_x11.c` and implements the five
entry points that `app.c` drives:

| Function | Behavior |
|----------|----------|
| `lud__platform_init` | Window plus EGL (ANGLE) context; GLES2 or GLES3 from `desc.gles_version` |
| `lud__platform_poll_events` | `PeekMessage` / `TranslateMessage` / `DispatchMessage` pump |
| `lud__platform_swap` | `eglSwapBuffers` |
| `lud__platform_shutdown` | Tear down EGL, window, and window class |
| `lud__platform_set_fullscreen` | Borderless-on-monitor toggle with saved placement |

A `WndProc` translates input into `lud__event_push`: keyboard through a
`VK` to `lud_keycode` table, text through `WM_CHAR` with UTF-16 surrogate
assembly, mouse move, buttons, and both wheel axes, plus resize and focus.
The file uses the wide (`-W`) Win32 API explicitly so it does not depend on
the `UNICODE` compile macro. The backend converts application text from
UTF-8 to UTF-16 at the boundary. The clipboard and drag-and-drop live in their
own translation units, `win32/clipboard.c` and `win32/dragdrop.c`, which
`platform_win32.c` reaches only through the `HWND` accessor and the OLE
register/unregister bracket. Because those units depend on no GL, they are
validated under Wine (see below).

## GL strategy: ANGLE, not a runtime loader

Windows `opengl32.dll` exports only OpenGL 1.1, so GLES entry points must
come from somewhere else. There were two options.

1. ANGLE / EGL. Link `libEGL` and `libGLESv2` (ANGLE) the same way the
   Linux backend links EGL and GLES. GL symbols resolve at link time, real
   GLES shaders run unchanged, and there is no desktop-GL compatibility
   risk. The cost is shipping the ANGLE DLLs with each application.

2. Native WGL plus a runtime function-pointer loader. Create a desktop GL
   context through WGL, load entry points at runtime, and rely on the
   driver's `GL_ARB_ES2_compatibility` and `GL_ARB_ES3_compatibility` to
   accept ludica's `#version 100` and `#version 300 es` shaders. This
   avoids redistributable DLLs but adds shader-compatibility risk and more
   code. A suitable CC0 loader exists in the birdie project
   (`src/birdie-gui/bd_gl.c`), which the WGL path could lift.

The port uses ANGLE. The birdie loader fits only the WGL path, so it does
not apply here.

## What is verified, and what is not

The full core plus `platform_win32.c`, `win32/clipboard.c`, and
`win32/dragdrop.c` cross-compile clean with `x86_64-w64-mingw32-gcc` against the
vendored EGL and GLES headers in `src/ludica/include`.

The clipboard and drag-and-drop are also run and validated under Wine. Since
`win32/clipboard.c` and `win32/dragdrop.c` depend only on the Win32 clipboard,
shell, and OLE APIs (not EGL), `tests/wine/` cross-compiles them with mingw and
runs them under Wine: the clipboard round-trip (9/9) and the drag-and-drop tests
(12/12) pass, covering text, files (`CF_HDROP`), HTML (the `CF_HTML` header),
images, multi-format copies, the OLE `IDropTarget`, and the drag `IDataObject`.

The EGL/GL/window path in `platform_win32.c` is not yet linked or run. Linking
it needs an ANGLE SDK, that is the `libEGL` and `libGLESv2` import libraries,
placed in `src/ludica/win32/win32libs/` (absent from the repository). Running
needs a Windows host. Final validation of that path must happen there.

## Remaining work

The clipboard and drag-and-drop are done and Wine-validated; what remains is the
GL/window path on a real Windows host:

1. Provide an ANGLE SDK in `src/ludica/win32/win32libs/` (import libraries
   plus the runtime DLLs to ship).
2. Build on Windows (MSYS2 or mingw), link, and run a sample.
3. Shake out runtime issues that are not visible from a compile check. The
   most likely suspects are the EGL-from-`HDC` display path and the
   fullscreen toggle.

## Build wiring

`ludica_SRCS.Windows_NT` in `src/ludica/module.mk` selects `platform_win32.c`,
`win32/clipboard.c`, and `win32/dragdrop.c`. The exported Windows libraries
gained `-lws2_32` for `automation.c` (winsock) and `-lole32 -luuid` for the OLE
drag-and-drop in `win32/dragdrop.c`.

The build no longer includes the older `src/ludica/win32/window.c`, which
predates the `app.c` platform interface. It is the Windows twin of the
also-dead `src/ludica/unix/ludica.c` and stays in the tree unbuilt.
