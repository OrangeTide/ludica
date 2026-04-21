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
- Meshes: `lud_make_mesh()`, `lud_draw()`, `lud_draw_range()`, `lud_destroy_mesh()`
- Textures: `lud_make_texture()`, `lud_load_texture()`, `lud_bind_texture()`, `lud_update_texture()`, `lud_destroy_texture()`
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

- Load: `lud_load_vfont("data/fonts/dejavu-sans")` — appends `.slugfont` or `.msdffont`
- Draw: `lud_vfont_begin()` / `lud_vfont_draw()` / `lud_vfont_end()`
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

Ludica games can be observed and controlled by AI agents via a
long-lived launcher daemon (`ludica-mcp`) and an MCP stdio bridge
(`ludica-mcp-bridge`). Start the launcher once per session:

```sh
LUDICA_MCP_ALLOWEXEC=$(echo _out/*/bin/* | tr ' ' ':') ludica-mcp &
```

The launcher owns spawned game processes, captures stdout/stderr,
proxies each game's control fd, and collects crash cores.
`ludica-mcp-bridge` adapts the launcher's line protocol to MCP
JSON-RPC on stdio and is pre-configured in `.claude/settings.json`.
Use the `/ludica-mcp` skill for tool usage and workflow recipes.
See `doc/manual/ludica-mcp.md` for the full protocol reference.

To make a game automation-friendly, register actions and state variables:

```c
lud_action_t a = lud_make_action("jump");
lud_bind_key(a, LUD_KEY_SPACE);
lud_auto_register_int("score", &score);
```
