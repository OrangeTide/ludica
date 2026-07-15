# Project: ludica

A lightweight cross-platform OpenGL ES 2.0/3.0 rendering framework for
game development experiments.

## Build

```sh
make                # default build
make RELEASE=1      # optimized (-O2, LTO)
make DEBUG=1        # debug symbols
make clean          # remove build artifacts
```

Output goes to `_out/<triplet>/bin/` (e.g. `_out/x86_64-linux-gnu/bin/hero`).

The build system is [modular-make](https://github.com/OrangeTide/modular-make).
Each `module.mk` declares targets via `EXECUTABLES`, `LIBRARIES`, and
`SUBDIRS`. See the GNUmakefile header or `doc/manual/` for full details.

## Directory Layout

- `src/ludica/` — Core library: platform, shaders, meshes, textures, sprites, fonts
  - `src/ludica/include/` — Public headers (`ludica.h`, `ludica_gfx.h`, `ludica_input.h`, `ludica_font.h`, `ludica_vfont.h`, `ludica_slug.h`, `ludica_anim.h`, `ludica_audio.h`, `ludica_auto.h`)
  - `src/ludica/platform_x11.c` — Linux/X11 backend
  - `src/ludica/win32/` — Windows backend
- `src/thirdparty/` — Header-only deps (stb_image, stb_ds, miniaudio)
- `src/include/` — Shared headers (HandmadeMath.h v1.13.0)
- `src/imgui/` — Dear ImGui integration
- `samples/hero/` — Portal-based 3D engine
- `samples/demo01_retrocrt/` — Palette framebuffer + CRT post-processing
- `samples/demo02_multiscroll/` — Parallax scrolling + sprite batching
- `samples/demo03_text_dialogs/` — Font rendering + dialog box UI
- `samples/demo04_sprites/` — Sprite rendering demo
- `samples/demo08_picking/` — 3D color-id picking via offscreen render target (automatable; `--selftest`)
- `samples/cliptest/` — clipboard copy/paste demo and cross-client round-trip self-test, including large-payload INCR transfers (`--selftest`)
- `samples/dndtest/` — drag-and-drop (XDND) drop-target demo and self-test; synthesizes a drag source over a second X connection (`--selftest`)
- `samples/dragtest/` — drag-and-drop (XDND) drag-source demo and self-test; drives a synthetic drop target over a second X connection (`--selftest`)
- `samples/demo05_audio/` — Multi-channel audio mixer demo
- `samples/lilpc/` — 286 XT PC emulator with CGA display
- `samples/tridrop/` — Triangle drop demo
- `samples/ansiview/` — ANSI art viewer
- `doc/manual/` — Manual (markdown, build with `make` in that directory)
- `doc/notes/` — R&D notes
- `doc/game-ideas/` — Game concept notes

## ludica Quick Reference

### Application Entry Point

```c
#include <ludica.h>
lud_run(&(lud_desc_t){
    .app_name = "name", .width = 800, .height = 600,
    .gles_version = 2,  /* 2 (default) or 3 */
    .init = init, .frame = frame, .cleanup = cleanup,
    .event = on_event,
});
```

`frame(float dt)` is called each frame. `event(const lud_event_t *ev)`
receives input events (key, mouse, gamepad, resize, focus).

### Graphics API (`ludica_gfx.h`)

- Shaders: `lud_make_shader()`, `lud_apply_shader()`, `lud_uniform_*()`, `lud_destroy_shader()`
- Meshes: `lud_make_mesh()`, `lud_update_mesh()`/`lud_update_mesh_indices()` (in-place, growable; DYNAMIC/STREAM usage), `lud_draw()`, `lud_draw_range()`, `lud_draw_instanced()` (GLES3; vary by `gl_InstanceID`), `lud_destroy_mesh()`
- Render state: `lud_depth_test()`, `lud_depth_func()`, `lud_depth_mask()`, `lud_cull()`, `lud_front_face()`, `lud_blend()`, `lud_scissor()`/`lud_scissor_off()`, `lud_read_pixels()` (RGBA, for color-id picking), `lud_flush()` — for 3D drawing; keeps apps off raw `<GLES2/gl2.h>`
- Textures: `lud_make_texture()`, `lud_load_texture()`, `lud_bind_texture()`, `lud_update_texture()`, `lud_destroy_texture()`
- Deferred destroy: `lud_destroy_mesh_deferred()`, `lud_destroy_texture_deferred()` — delete at end of frame (safe to retire a resource a draw earlier this frame still uses); ludica flushes the queue each frame
- Render targets: `lud_make_render_target()`, `lud_bind_render_target()`, `lud_render_target_texture()`, `lud_destroy_render_target()` — offscreen render-to-texture (post-processing, color-id picking)
- Sprites: `lud_sprite_begin()`, `lud_sprite_draw()`, `lud_sprite_end()`
- Fonts: `lud_make_default_font()`, `lud_draw_text()`
- Framebuffer: `lud_make_framebuffer()`, palette + lock/unlock + blit
- Loading: `lud_draw_progress()` — progress bar with swap, for use during init

### Audio API (`ludica_audio.h`)

- `lud_audio_init()`, `lud_audio_shutdown()`
- `lud_audio_play()`, `lud_audio_stop()`, `lud_audio_set_master()`
- Capture: `lud_audio_capture_start()`, `lud_audio_capture_stop()`

### Animation API (`ludica_anim.h`)

- `lud_anim_init()`, `lud_anim_play()`, `lud_anim_update()`
- `lud_anim_frame()`, `lud_anim_finished()`

### Arena Allocator (`ludica_arena.h`)

Linear bump allocator over one fixed buffer; no per-allocation free,
reset or free the whole thing at once. For job scratch, per-frame
temporaries, and procgen buffers. Aggregated into `ludica.h`.

- `lud_arena_init(&a, size)`, `lud_arena_alloc(&a, size)` (aligned, NULL when full), `lud_arena_reset(&a)`, `lud_arena_free(&a)`

### GLES Version Architecture

Ludica supports both GLES2 and GLES3 in the same codebase. Each program
chooses its version at startup via `lud_desc_t.gles_version`:

- **GLES2** (default, `gles_version = 0` or `2`): Maximum portability.
  Targets Pi Zero, Pi 3, older mobile, WebGL 1. Shaders use `#version 100`
  with `attribute`/`varying`/`texture2D`/`gl_FragColor`.
- **GLES3** (`gles_version = 3`): Enables sRGB textures, float textures,
  MRT, instancing, VAOs, `textureLod`, etc. Equivalent to WebGL 2.
  Shaders use `#version 300 es` with `in`/`out`/`texture()`.

GLES3 contexts accept GLES2 shaders (`#version 100`), so ludica's
built-in shaders (sprite, framebuffer) work on both versions unchanged.
Programs that request GLES3 can use either shader syntax.

Query at runtime: `lud_gles_version()` returns 2 or 3.

### Vector Fonts (`ludica_vfont.h`)

Resolution-independent text with automatic backend selection:
GLES3 uses Slug (GPU Bezier evaluation), GLES2 uses SDF (distance field atlas).

- Load: `lud_load_vfont("assets/fonts/dejavu-sans")` — appends `.slugfont` or `.msdffont`
- Draw: `lud_vfont_begin()` / `lud_vfont_draw(font, &pen, size, r,g,b,a, text)` / `lud_vfont_end()`
- Pen: `lud_pen_t pen = { x, y };` — caller-owned, draw advances pen->x
- Metrics: `lud_vfont_ascender()`, `lud_vfont_line_height()`, `lud_vfont_newline()`
- Layout: `lud_vfont_line_break()` — word-wrap at max width
- Clip: `lud_vfont_set_clip()` / `lud_vfont_clear_clip()` — GL scissor, session state
- Measure: `lud_vfont_text_width()`
- Convert: `font2slug input.ttf -o output.slugfont`, `font2msdf input.ttf -o output.msdffont`
- Override backend: `LUD_VFONT_BACKEND=slug|msdf` environment variable

Multiple fonts can be loaded simultaneously — each `lud_vfont_t` handle
tracks its own backend. See `doc/manual/vector-fonts.md` for details.

### Key Conventions

- Textures default to `GL_CLAMP_TO_EDGE`; use `fract()` in shader for tiling
- Shader attribute locations are bound by index (layout[i] ↔ attrs[i])
- Sprite batch is Y-down; 3D rendering uses Y-up
- HandmadeMath functions take degrees, not radians
- Resource handles are `{ unsigned id; }` structs; id==0 means invalid

### Input (`ludica_input.h`)

Key constants: `LUD_KEY_A`..`LUD_KEY_Z`, `LUD_KEY_ESCAPE`,
`LUD_KEY_PAGE_UP`, `LUD_KEY_PAGE_DOWN`, etc.

Polled: `lud_key_down()`, `lud_mouse_pos()`, `lud_mouse_button_down()`,
`lud_gamepad_axis()`, `lud_gamepad_button_down()`.

Gamepad axes have a rescaled dead zone (default 0.15), tunable via
`lud_gamepad_set_deadzone()` / `lud_gamepad_deadzone()`.

Clipboard: `lud_clipboard_get_text()` / `lud_clipboard_set_text()` for
synchronous UTF-8 text (get returns a malloc'd string the caller frees;
NULL on empty/timeout). `lud_clipboard_get_data()` / `lud_clipboard_set_data(format,
data, len)` for byte-exact binary (images via `LUD_CLIPBOARD_PNG`, or any MIME
target). `lud_clipboard_get_files()` / `lud_clipboard_set_files(paths, count)`
for file lists (`text/uri-list`, handles `file://` percent-encoding).
`lud_clipboard_set_multi(items, count)` offers several formats at once (e.g.
image/png + text fallback); reads stay per-format via get_data/get_text.
`lud_clipboard_set_html(html, plain)` / `lud_clipboard_get_html()` for rich text
(HTML, offers a plain-text fallback); `LUD_CLIPBOARD_HTML`/`_RTF` constants.
`lud_clipboard_get_async(format, cb, user)` reads without blocking and delivers
via callback during event processing; one request at a time. Large payloads use
the X11 INCR protocol automatically. X11 serves text/images/files; Windows maps
formats to native clipboard types (CF_UNICODETEXT/CF_HDROP/HTML Format/registered),
compile-verified but not yet run; Emscripten is a stub (browser clipboard is
permission-gated).

Drag and drop: the window is an XDND drop target AND drag source. Drops arrive
as a `LUD_EV_DROP` event (`ev->drop.format`, `.data`, `.len`, `.x`, `.y`; data
owned by ludica, valid only during the callback). `lud_parse_uri_list(data,
len)` decodes a dropped/copied `text/uri-list` into a NULL-terminated path
array. To start a drag out of the window, call `lud_drag_data(format, data,
len)`, `lud_drag_files(paths, count)`, or `lud_drag_multi(items, count)` (several
formats at once) when a drag gesture begins (mouse button held); non-blocking,
ends with a `LUD_EV_DRAG_END` event (`ev->drag_end.accepted`).
X11 only (reuses the clipboard selection + INCR machinery); Windows/Emscripten
do not implement drag-and-drop yet.

## Adding a New Sample Program

1. Create `samples/myapp/` with source files
2. Create `samples/myapp/module.mk`:
   ```makefile
   EXECUTABLES += myapp
   myapp_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
   myapp_SRCS  = myapp.c
   myapp_LIBS = ludica
   ```
3. Add `myapp` to `SUBDIRS` in `samples/module.mk`

## Automation & MCP

**Never bypass the MCP tool layer.** Do not connect to the launcher
directly via `nc`, `netcat`, or piping into the bridge binary. Only
use `mcp__ludica__*` tools. If the tools aren't loading, tell the
user and stop. Do not attempt workarounds.

Ludica games can be observed and controlled by AI agents via a
long-lived launcher daemon (`ludica-launcher`) and an MCP stdio bridge
(`ludica-mcp-bridge`). Start the launcher once per session:

```sh
LUDICA_MCP_ALLOWEXEC=$(echo _out/*/bin/* | tr ' ' ':') \
    _out/x86_64-linux-gnu/bin/ludica-launcher --port=4000 &
```

The launcher owns spawned game processes, captures stdout/stderr,
proxies each game's control fd, and collects crash cores.
`ludica-mcp-bridge` adapts the launcher's line protocol to MCP
JSON-RPC on stdio and is configured in `.mcp.json`.
Use the `/ludica-mcp` skill for tool usage and workflow recipes.
See `doc/manual/ludica-mcp.md` for the full protocol reference.

To make a game automation-friendly, register actions and state variables:

```c
lud_action_t a = lud_make_action("jump");
lud_bind_key(a, LUD_KEY_SPACE);
lud_auto_register_int("score", &score);
```
