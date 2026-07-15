# Windows clipboard test (via Wine)

`clipboard_test.c` drives `src/ludica/win32/clipboard.c` in-process against the
real Win32 clipboard and checks each format round-trips: UTF-8 text, binary
bytes with embedded NULs, file lists (CF_HDROP), the uri-list view derived from
a drop, HTML through the CF_HTML offset header, `set_html` with a plain-text
fallback, and a multi-format copy.

It builds for Windows with mingw-w64 and runs under Wine, so the whole clipboard
path can be validated from a Linux dev box with no Windows host.

## Run

```sh
sh tests/wine/run.sh
```

Requires `x86_64-w64-mingw32-gcc` and `wine` on `PATH`. If either is missing the
script prints `SKIP:` and exits 0. Expected output is nine `PASS` lines and
`0 failure(s)`.

## Why it links so little

The clipboard TU depends only on the Win32 clipboard/shell APIs plus one hook,
`lud__win32_window()`, satisfied here by `win_stub.c` returning NULL
(`OpenClipboard(NULL)` is valid). No EGL, GLES, or window loop is pulled in.
`input.c` supplies the shared `lud_clipboard_set_html`/`get_html` and uri-list
helpers, and needs nothing beyond the clipboard TU.
