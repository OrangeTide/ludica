# Clipboard and Drag-and-Drop Notes

Status and design notes for ludica's data-transfer support: the clipboard
(`ludica_input.h`) and drag-and-drop across the X11, Windows, and Emscripten
backends. See also `windows-port.md` and `platform-gles.md`.

## Status

Both the clipboard and drag-and-drop are implemented on all three backends,
within what each platform allows. The table summarizes what works where.

| Capability | X11 | Windows | Emscripten (browser) |
|------------|-----|---------|----------------------|
| Clipboard write (text/binary/multi) | yes | yes | yes (dispatched to `navigator.clipboard`) |
| Clipboard sync read | yes | yes | no (browser has no sync read; returns NULL) |
| Clipboard async read (`get_async`) | yes | yes | yes (`readText` / `read`) |
| Files on clipboard (`set_files`/`get_files`) | yes (`text/uri-list`) | yes (`CF_HDROP`) | no (no filesystem paths) |
| Large payloads | yes (`INCR`) | yes (OS-native) | yes (OS-native) |
| Rich text (HTML) | yes | yes (`HTML Format`) | yes (`text/html`) |
| Drop target (receive) | yes (XDND) | yes (OLE `IDropTarget`) | yes (DOM `drop`) |
| Drag source (send) | yes (XDND, non-blocking) | yes (`DoDragDrop`, modal) | no (cannot start a drag programmatically) |

The unchecked cells are honest platform limits, not missing work. A browser has
no synchronous clipboard read, exposes no filesystem paths, and cannot begin an
HTML5 drag except from a real user gesture on a draggable element.

## Design

The public API is one surface for all backends: synchronous and asynchronous
clipboard get/set (typed by a MIME-ish format string, with `set_multi` offering
several formats at once), and drag-and-drop that delivers `LUD_EV_DROP` for
drops and ends a started drag with `LUD_EV_DRAG_END`. Each backend maps that
onto its native machinery.

- **X11** (`platform_x11.c`) uses the `CLIPBOARD` selection and the XDND
  protocol, both riding the same selection-transfer code with `INCR` chunking
  for payloads larger than a single request. An X error handler swallows
  `BadWindow`/`BadMatch` so a requestor vanishing mid-transfer cannot crash the
  app. The drag source is non-blocking: it owns `XdndSelection`, grabs the
  pointer, and tracks the target from the poll loop.

- **Windows** (`platform_win32.c`, `win32/clipboard.c`, `win32/dragdrop.c`) maps
  each format to a native clipboard type (`CF_UNICODETEXT`, `CF_HDROP`,
  `HTML Format`, or a registered format for images and other bytes). The clipboard
  lives in its own GL-independent translation unit so it can be tested without
  EGL. The drop target is an OLE `IDropTarget`; the drag source builds an
  `IDataObject` and calls `DoDragDrop`. Note that `DoDragDrop` is modal: unlike
  X11, the Windows drag source blocks until the drop or cancel, then fires
  `LUD_EV_DRAG_END`. App code that waits for that event stays portable.

- **Emscripten** (`platform_emscripten.c`) dispatches writes to
  `navigator.clipboard` and maps async reads onto its `readText` / `read`
  promises, marshaling results back to C through `ccall` trampolines (the link
  exports `ccall`). The drop target listens for DOM `dragover`/`drop` on the
  canvas; a dropped file arrives as its bytes under its MIME type, not a path.

### Shared decoders on Windows

`win32/clipboard.c` owns the format mapping (`lud__win32_clip_cf`), the
data-to-`HGLOBAL` builders (`lud__win32_clip_build`), and the `HGLOBAL`-to-bytes
decoders (`lud__win32_clip_decode`). The clipboard and the drop/drag targets in
`win32/dragdrop.c` all go through these, so there is one place that knows how
each MIME type is encoded. `platform_win32.c` exposes only the app `HWND` and the
OLE register/unregister bracket.

### Drop-buffer ring

Delivered `LUD_EV_DROP` bytes are owned by the platform and pointed at by the
event; the app must copy anything it keeps. All three backends rotate these
buffers through an 8-slot ring. A single shared buffer is a use-after-free when
two drops land before the event queue is drained: the second frees the first
drop's buffer while its event is still queued. XDND and OLE serialize drops
across frames so it is unlikely there, but the browser dispatches DOM drops (and
async file reads) freely, and a burst test reproduced the corruption before the
ring. With the ring, a slot is reused only many drops later, well after its
event has been dispatched. The format string is a static constant on X11 and
Windows, so only the data buffer is owned.

## Testing

- **X11**: `samples/cliptest`, `samples/dndtest`, `samples/dragtest`
  (`--selftest`) each open a second X connection in-process to drive the real
  protocol as another client. They cover text, binary, files, `INCR` in both
  directions, and the drop-target and drag-source handshakes.
- **Windows**: `tests/wine/` cross-compiles the clipboard and drag-and-drop
  translation units with mingw-w64 and runs them under Wine, so the whole path
  is validated from Linux with no Windows host. The drop target and drag
  `IDataObject` are driven in-process with a mock `IDataObject`.
- **Emscripten**: `samples/webclip` is the manual browser harness (keys copy and
  paste, drops report their payload; also prints to the console). It was driven
  in headless Chromium with SwiftShader WebGL2 through chromedriver and CDP,
  confirming the clipboard round-trip and the drop target, including a burst of
  several drops in one frame to exercise the ring.

## History

Built up over several sessions: X11 first (clipboard, `INCR`, non-text targets,
multi-target, rich text, then XDND both directions), then Windows (clipboard
split into its own TU and validated under Wine, drop target, drag source), then
Emscripten (clipboard writes and async reads, drop target). The drop-buffer ring
was found by the browser burst test and applied back to all three backends.
