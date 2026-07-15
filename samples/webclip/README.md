# webclip — clipboard + drag-and-drop manual test

An interactive harness for the clipboard and drop-target APIs. It builds on every
backend, but its purpose is the browser: on Emscripten the clipboard is
asynchronous and gesture/permission gated and drops arrive as DOM events, so the
paths need a human at a real browser to confirm. On X11 and Windows the same keys
exercise the native clipboard and XDND/OLE drop paths.

## Controls

- `C` — copy a text string (with a counter and a non-ASCII glyph)
- `V` — paste text via `lud_clipboard_get_async` (result arrives asynchronously)
- `M` — copy two formats at once (`text/html` + `text/plain`) via `set_multi`
- `I` — copy a 1x1 PNG via `set_data(LUD_CLIPBOARD_PNG, ...)`
- `ESC` — quit
- Drag text or a file onto the window to see a `LUD_EV_DROP`

The last action, paste result, and drop are shown on the canvas and also printed
to stdout (the browser console), so results are visible without reading the
canvas.

## Run in a browser (the point of this sample)

```sh
make webclip CC=emcc CXX=em++ AR=emar
emrun _out/wasm32-unknown-emscripten/bin/webclip.html
```

`emrun` serves the page and opens it; any static web server works too (a
`file://` load will not, browsers block wasm and the clipboard there). Open the
browser console to watch the log.

What to expect in the browser:

- `C`/`M`/`I` write to the system clipboard. The first clipboard use may prompt
  for permission. Paste into another app to confirm.
- `V` triggers `navigator.clipboard.readText`; the browser may prompt, then the
  pasted text appears. Reads need the page to have focus and a prior gesture.
- Dropping a file reports its bytes under its MIME type (for example
  `image/png`), not a path, because the browser hides filesystem paths. Dropping
  selected text reports `text/plain` or `text/html`.

## Run on desktop

`make webclip` then run `_out/<triplet>/bin/webclip`. Here `V` reads the X11 or
Windows clipboard synchronously through the async shim, drops of files decode to
real paths (`text/uri-list`), and copies land on the native clipboard.
