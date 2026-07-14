# Introduction

This manual covers the game engine built on the `ludica` rendering framework.
The engine provides a portal-based 3D rendering system with dual GLES2/GLES3
support.

# Getting Started

## Prerequisites

- GCC or Clang (C11 and C++17)
- EGL and GLESv2 development libraries
- X11 development libraries (Linux)
- GNU Make

## Building

```sh
make
```

Executables are placed in `_out/<triplet>/bin/`. For example on x86-64 Linux:

```sh
_out/x86_64-linux-gnu/bin/hero
```

Build modes:

```sh
make              # default
make RELEASE=1    # optimized (-O2, LTO)
make DEBUG=1      # debug symbols (-Og -g)
```

# Architecture

## Build System

The project uses a modular `module.mk` build system. Each source directory
contains a `module.mk` file that declares its targets and dependencies.
The top-level `src/module.mk` lists subdirectories and common flags.

A library module:

```makefile
LIBRARIES += mylib
mylib_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
mylib_SRCS  = source.c
mylib_EXPORTED_CPPFLAGS = -I$(mylib_DIR)include
```

An executable module:

```makefile
EXECUTABLES += myapp
myapp_DIR  := $(dir $(lastword $(MAKEFILE_LIST)))
myapp_SRCS  = main.c
myapp_LIBS = mylib
```

Per-target variables: `_SRCS`, `_CFLAGS`, `_CXXFLAGS`, `_CPPFLAGS`,
`_LDFLAGS`, `_LDLIBS`, `_LIBS` (internal library deps), `_EXPORTED_*`
(flags passed to dependents). Platform-specific suffixes like `.Linux` or
`.Windows_NT` are appended automatically.

## ludica Framework

`ludica` is the core rendering and platform abstraction library. It provides:

- **Platform layer**: Window creation, EGL context, event loop (X11 on Linux,
  Win32 on Windows)
- **Shader management**: Compile, link, and apply GLSL ES shaders
- **Mesh rendering**: Static and dynamic vertex buffer objects
- **Texture loading**: Image loading and GPU texture management
- **Sprite batching**: Efficient 2D sprite rendering
- **Bitmap fonts**: Fixed-width font rendering from texture atlases
- **Input handling**: Keyboard and mouse input via callbacks or polling

### Application Lifecycle

An ludica application implements callback functions and passes them via
a descriptor to `lud_run()`:

```c
#include <ludica.h>

static void init(void) { /* one-time setup after GL context ready */ }
static void frame(float dt) { /* called each frame */ }
static void cleanup(void) { /* teardown */ }

int main(int argc, char **argv) {
    return lud_run(&(lud_desc_t){
        .app_name = "My App",
        .width = 800, .height = 600,
        .init = init,
        .frame = frame,
        .cleanup = cleanup,
    });
}
```

`lud_run()` owns the main loop. On WASM it never returns.

### Shaders

Shaders are compiled from GLSL ES source strings via a descriptor:

```c
ludica_shader_t shader = lud_make_shader(&(ludica_shader_desc_t){
    .vert_src = vertex_src,
    .frag_src = fragment_src,
    .attrs = { "a_position", "a_texcoord" },
    .num_attrs = 2,
});
lud_apply_shader(shader);
lud_uniform_mat4(shader, "u_mvp", matrix_data);
```

### Meshes

Vertex data is uploaded to GPU via mesh descriptors:

```c
ludica_mesh_t mesh = lud_make_mesh(&(ludica_mesh_desc_t){
    .vertices = verts,
    .vertex_count = n,
    .vertex_stride = sizeof(Vertex),
    .layout = {
        { .size = 3, .offset = 0 },                       /* position */
        { .size = 2, .offset = offsetof(Vertex, u) },     /* texcoord */
    },
    .num_attrs = 2,
    .usage = LUD_USAGE_STATIC,
    .primitive = LUD_PRIM_TRIANGLES,
});
lud_draw(mesh);
lud_draw_range(mesh, first, count);
```

A mesh created with `LUD_USAGE_DYNAMIC` or `LUD_USAGE_STREAM` can be
rewritten in place rather than destroyed and recreated each frame:

```c
lud_update_mesh(mesh, 0, n, verts);            /* replace vertex data */
lud_update_mesh_indices(mesh, 0, n, indices);  /* replace uint16 indices */
```

Updates that fit the current allocation patch it directly; an update
extending past the end grows the buffer (a growing update replaces the
whole buffer, so data before `first_*` becomes undefined). The draw
count grows to cover the highest element written but never shrinks, so
use `lud_draw_range` to draw fewer. `lud_update_mesh_indices` can also
promote a non-indexed mesh to indexed.

On a GLES3 context a mesh can be drawn many times in a single call:

```c
lud_draw_instanced(mesh, instance_count);  /* GLES3 only */
```

The vertex shader distinguishes instances with the built-in
`gl_InstanceID` (for example, indexing a uniform array of transforms),
which is how decoration kits and other repeated geometry avoid one draw
call per copy. On a GLES2 context this logs a warning once and draws
nothing.

### Render State

For 3D drawing, control depth testing and face culling through ludica
rather than raw GL. These wrappers keep the GLES2/GLES3/WebGL backend
differences inside the framework, so application code never includes
`<GLES2/gl2.h>`. State is global and persists until changed.

```c
lud_depth_test(1);                /* enable the depth test */
lud_depth_func(LUD_DEPTH_LESS);   /* LESS (default), LEQUAL, or ALWAYS */
lud_cull(LUD_CULL_BACK);          /* NONE, BACK, or FRONT */
lud_front_face(LUD_WINDING_CCW);  /* CCW (default) or CW front faces */

/* ... draw opaque 3D meshes ... */

lud_depth_mask(0);                /* stop the translucent pass writing depth */
lud_blend(LUD_BLEND_ADD);         /* NONE, ALPHA, or ADD (additive) */

/* ... draw translucent/additive meshes ... */

lud_depth_mask(1);                /* restore depth writes */
lud_depth_test(0);                /* restore for a 2D/HUD overlay */
lud_cull(LUD_CULL_NONE);
lud_blend(LUD_BLEND_NONE);

lud_flush();                      /* flush queued commands (no buffer swap) */
```

`lud_blend` is for custom mesh draws. The sprite batch sets its own
alpha blend on `lud_sprite_begin` and clears it on `lud_sprite_end`,
so 2D sprite drawing needs no blend call.

For a translucent pass, keep the depth test on but turn depth writes
off with `lud_depth_mask(0)` so transparent surfaces are still occluded
by solid geometry without hiding each other; restore with
`lud_depth_mask(1)` before the next opaque pass.

### Scissor and Pixel Readback

`lud_scissor(x, y, w, h)` clips drawing to a rectangle (window pixels,
origin bottom-left, like `lud_viewport`); `lud_scissor_off()` removes
the clip. Use it for split-screen, sub-viewports, or UI panels.

`lud_read_pixels(x, y, w, h, rgba)` reads back the color buffer as
RGBA8. `(x, y)` is the top-left of the region in window pixels with a
top-left origin (the same convention as `lud_mouse_pos`), and rows come
back top-first. This is the basis for 3D mouse picking: render each
pickable object in a unique color, then read the pixel under the cursor
and map the color back to an object id.

```c
unsigned char id[4];
int mx, my;
lud_mouse_pos(&mx, &my);
lud_read_pixels(mx, my, 1, 1, id);   /* color id of object under cursor */
```

GLES can read back only the color buffer, not depth, so picking is
color-id based rather than depth-unproject based.

### Render Targets (render-to-texture)

A render target is an off-screen framebuffer backed by a color texture
you can sample or read back: post-processing, reflections, dynamic
textures, or color-id picking that stays off the visible screen. Set
`depth` when the pass needs the depth test (rendering 3D geometry).

```c
lud_target_t rt = lud_make_render_target(&(lud_target_desc_t){
    .width = 512, .height = 512,
    .format = LUD_PIXFMT_RGBA8,
    .min_filter = LUD_FILTER_LINEAR,
    .mag_filter = LUD_FILTER_LINEAR,
    .depth = 1,                       /* attach a depth buffer */
});

lud_bind_render_target(rt);           /* viewport set to 512x512 */
lud_clear(0, 0, 0, 1);
/* ... draw the scene ... */
lud_bind_render_target((lud_target_t){0});  /* back to the window */

/* sample the result */
lud_texture_t tex = lud_render_target_texture(rt);
lud_bind_texture(tex, 0);
/* ... draw a fullscreen quad with a post-process shader ... */

lud_destroy_render_target(rt);        /* also frees the color texture */
```

`lud_bind_render_target` sets the viewport to the bound surface, and
`lud_read_pixels` accounts for the bound target's height, so reading
back from a target works the same as from the window. The color
attachment is an ordinary `lud_texture_t`; it is owned by the target
and freed when the target is destroyed.

### Textures

```c
ludica_texture_t tex = lud_load_texture("image.png",
    LUD_FILTER_NEAREST, LUD_FILTER_NEAREST);
lud_bind_texture(tex, 0);
```

### Deferred Destruction

`lud_destroy_mesh` and `lud_destroy_texture` delete the GPU resource right
away. When streaming, a resource is sometimes retired in the same frame a
draw earlier in that frame still references it; freeing the handle
immediately would invalidate it underneath that draw. The deferred
variants queue the delete until the end of the frame instead:

```c
lud_destroy_mesh_deferred(cell->mesh);
lud_destroy_texture_deferred(cell->lightmap);
```

ludica runs the queued deletions automatically after the frame callback
returns, once all of the frame's draws have been issued, and flushes any
remainder at shutdown. Use the deferred form when unloading content during
a live frame; the immediate form is fine during init or teardown.

### Sprite Batching

For 2D rendering, ludica provides an immediate-mode sprite batch:

```c
lud_sprite_begin(0, 0, screen_w, screen_h);
lud_sprite_draw(tex, dx, dy, dw, dh, sx, sy, sw, sh);
ludica_sprite_rect(x, y, w, h, r, g, b, a);
lud_sprite_end();
```

### Bitmap Fonts

```c
ludica_font_t font = lud_make_default_font();
lud_sprite_begin(0, 0, screen_w, screen_h);
lud_draw_text(font, x, y, scale, "Hello");
lud_sprite_end();
```

### Input

Events are delivered via the `event` callback in the descriptor:

```c
static int on_event(const lud_event_t *ev) {
    switch (ev->type) {
    case LUD_EV_KEY_DOWN: /* ev->key.code */ break;
    case LUD_EV_MOUSE_MOVE: /* ev->mouse.x, ev->mouse.y */ break;
    }
    return 0;
}
```

Polled state is also available:

```c
if (lud_key_down(LUD_KEY_W)) { /* W held */ }
lud_mouse_pos(&mx, &my);
float axis = lud_gamepad_axis(0, 0);
```

Analog stick axes pass through a dead zone (default 0.15 of full
deflection) so a stick at rest reads exactly zero. Values past the
threshold are rescaled so the stick still reaches 1.0, leaving no jump at
the edge. Tune it globally:

```c
lud_gamepad_set_deadzone(0.20f);   /* clamped to [0, 0.95] */
float dz = lud_gamepad_deadzone();
```

### Action Bindings

Actions decouple game logic from physical keys. Instead of checking
`lud_key_down(LUD_KEY_W)` directly, you create a named action, bind one or
more keys to it, and poll the action each frame. This lets players rebind
controls and lets the same action respond to both keyboard and gamepad input.

```c
/* Create actions (typically once in init) */
lud_action_t act_forward = lud_make_action("forward");
lud_action_t act_jump    = lud_make_action("jump");

/* Bind keys — multiple bindings per action are allowed */
lud_bind_key(LUD_KEY_W, act_forward);
lud_bind_key(LUD_KEY_UP, act_forward);
lud_bind_gamepad_button(0, 0, act_jump);  /* pad 0, button 0 */
```

Actions are updated automatically each frame before the `frame` callback.
Three queries are available:

| Function                | Returns true when                        |
|-------------------------|------------------------------------------|
| `lud_action_down(a)`    | Action is held this frame                |
| `lud_action_pressed(a)` | Action went down this frame (edge)       |
| `lud_action_released(a)`| Action went up this frame (edge)         |

Use `lud_action_down()` for continuous movement and `lud_action_pressed()`
for one-shot triggers like toggling a mode:

```c
static void frame(float dt) {
    if (lud_action_down(act_forward))
        move_player(dt);
    if (lud_action_pressed(act_jump))
        start_jump();
}
```

Other action functions:

- `lud_find_action(name)` — look up an existing action by name (returns
  `{0}` if not found)
- `lud_unbind_action(a)` — remove all bindings from an action

Actions do not require an `event` callback — they work entirely through
polling. This makes it straightforward to eliminate event-driven input
handling in favor of a pure poll-based frame loop.

### Clipboard

Ludica reads and writes the system clipboard as UTF-8 text. The simple
path is synchronous:

```c
/* Write text (e.g. on a copy shortcut) */
lud_clipboard_set_text("hello, clipboard");

/* Read text (e.g. on a paste shortcut). Caller owns the string. */
char *text = lud_clipboard_get_text();
if (text) {
    insert_at_cursor(text);
    free(text);
}
```

`lud_clipboard_get_text()` returns a malloc'd, NUL-terminated string that
the caller must `free()`. It returns `NULL` when the clipboard is empty,
holds no text, or the owning application does not answer within a short
timeout. `lud_clipboard_set_text()` returns 0 on success. On X11 the
clipboard contents are served only while the app runs, so they are lost on
exit unless a clipboard manager copies them (standard X11 behavior).

For UIs that must not stall, a non-blocking read delivers its result
through a callback during a later frame's event processing:

```c
static void on_paste(const char *format, void *data, size_t len, void *user) {
    (void)format; (void)len;
    if (data)
        insert_at_cursor((const char *)data);  /* data freed after return */
}

/* Kick off a read; returns immediately. */
lud_clipboard_get_async(LUD_CLIPBOARD_TEXT, on_paste, NULL);
```

The callback always fires, even on failure or timeout, in which case
`data` is `NULL` and `len` is 0. The `data` buffer is owned by ludica and
freed once the callback returns, so copy anything you need to keep. Only
one asynchronous request may be in flight at a time; a second one started
while the first is pending fails immediately with a `NULL` callback.

The `format` argument (`LUD_CLIPBOARD_TEXT` is `"text/plain;charset=utf-8"`)
exists so non-text targets such as images or file lists can be added later
without changing the API. Today only text is implemented.

Platform notes: X11 uses the `CLIPBOARD` selection (Ctrl+C / Ctrl+V), not
the middle-click `PRIMARY` selection. Windows uses `CF_UNICODETEXT`. The
Emscripten backend is a stub because the browser clipboard is asynchronous
and gated behind a user gesture and permission prompt.

### Fonts

Ludica provides bitmap fonts rendered through the sprite batch system.
A built-in 8x8 font is available with no file loading:

```c
lud_font_t font = lud_make_default_font();
```

Custom fonts can be created from a texture atlas where glyphs are arranged
in a grid:

```c
lud_font_t font = lud_make_font(atlas_tex,
    chars_wide,    /* columns in the atlas */
    glyph_w, glyph_h,  /* cell size in pixels */
    first_char);   /* ASCII code of first glyph (typically 32) */
```

All text drawing must occur between `lud_sprite_begin()` / `lud_sprite_end()`:

```c
lud_sprite_begin(0, 0, screen_w, screen_h);
lud_draw_text(font, x, y, scale, "Hello");
lud_draw_text_centered(font, center_x, y, scale, "Centered");
lud_draw_text_wrapped(font, x, y, scale, max_width, line_spacing,
                      "Long text that wraps...");
lud_sprite_end();
```

`lud_text_width()` measures text width in pixels without drawing, useful
for layout calculations.

### Animation

Frame-based sprite animation player for flipbook-style sprite sheets:

```c
lud_anim_t anim;
lud_anim_init(&anim, 0, 5, 8.0f, 1);  /* frames 0-5, 8 FPS, looping */
```

Each frame, advance the timer and read the current frame index:

```c
lud_anim_update(&anim, dt);
int frame = lud_anim_frame(&anim);
/* Use frame index to compute spritesheet source rect */
int src_x = (frame % cols) * cell_w;
int src_y = (frame / cols) * cell_h;
```

To switch animations without resetting if the parameters match (useful in
a state machine where the same animation may be requested each frame):

```c
/* Returns 1 if the animation changed, 0 if already playing this one */
lud_anim_play(&anim, 0, 3, 12.0f, 1);
```

For one-shot animations (e.g., death, attack), set looping to 0 and check
completion:

```c
lud_anim_init(&anim, 0, 7, 10.0f, 0);  /* one-shot */
/* ... later ... */
if (lud_anim_finished(&anim)) {
    /* animation complete, transition to next state */
}
```

### Audio

Ludica includes a 16-channel PCM audio mixer with support for PCM16,
PCM8, and IMA ADPCM sample formats. It uses miniaudio for the platform
audio device.

Initialize the audio system (typically in `init()`):

```c
lud_audio_init();
```

Play a sample on a channel (0-15):

```c
static int16_t samples[44100];  /* 1 second at 44100 Hz */

lud_audio_play(0, &(lud_audio_desc_t){
    .data = samples,
    .length = 44100,
    .volume_l = 255,
    .volume_r = 255,
    .pitch = 256,       /* 8.8 fixed-point: 256 = normal speed */
    .format = LUD_AUDIO_PCM16,
});
```

Pitch is 8.8 fixed-point: 256 = normal speed, 512 = double speed, 128 =
half speed. Volume is 0-255 per channel.

Looping samples specify a loop region:

```c
lud_audio_play(1, &(lud_audio_desc_t){
    .data = music_data,
    .length = total_frames,
    .loop_start = intro_end,
    .loop_length = loop_frames,  /* non-zero enables looping */
    .volume_l = 200, .volume_r = 200,
    .pitch = 256,
    .format = LUD_AUDIO_PCM16,
});
```

Other operations:

```c
lud_audio_stop(0);                  /* stop channel 0 */
lud_audio_set_master(128, 128);     /* half master volume */
lud_audio_shutdown();               /* shut down (in cleanup) */
```

#### Audio Capture

The mixer output can be captured to a WAV file:

```c
lud_audio_capture_start();
/* ... play audio, advance frames ... */
lud_audio_capture_stop("output.wav");  /* writes 44100 Hz stereo WAV */
```

This is also available via the automation command `CAPAUDIO START` /
`CAPAUDIO STOP`.

#### Sample Formats

| Format | Description |
|--------|-------------|
| `LUD_AUDIO_PCM16` | Signed 16-bit, native endian |
| `LUD_AUDIO_PCM8` | Signed 8-bit (expanded to 16-bit internally) |
| `LUD_AUDIO_ADPCM` | IMA ADPCM, 4-bit, high nibble first |

### Fullscreen

Toggle fullscreen mode at runtime:

```c
lud_set_fullscreen(1);          /* enter fullscreen */
lud_set_fullscreen(0);          /* return to windowed */
int fs = lud_is_fullscreen();   /* query current state */
```

Fullscreen can also be requested at startup via the descriptor:

```c
lud_run(&(lud_desc_t){
    .fullscreen = 1,
    /* ... */
});
```

### Loading Progress

For applications with non-trivial loading times, ludica provides a
progress bar that draws and swaps a frame immediately:

```c
void lud_draw_progress(int step, int total, const char *label);
```

Call this from `init()` or from `frame()` during a loading phase.
It clears the screen, draws a centered progress bar with an optional
text label, and swaps buffers.  The application controls the pacing —
ludica draws whatever step/total you give it.

A typical synchronous loading pattern:

```c
static void init(void) {
    enum { LOAD_SHADERS, LOAD_TEX1, LOAD_TEX2, LOAD_MAP, LOAD_DONE };
    lud_draw_progress(0, LOAD_DONE, "Compiling shaders...");
    create_shaders();
    lud_draw_progress(1, LOAD_DONE, "Loading textures...");
    load_texture_set_1();
    lud_draw_progress(2, LOAD_DONE, "Loading textures...");
    load_texture_set_2();
    lud_draw_progress(3, LOAD_DONE, "Building map...");
    build_map();
    lud_draw_progress(LOAD_DONE, LOAD_DONE, "Ready");
}
```

Each call paints a frame to the screen, so the user sees the bar
advance between heavy operations.  No threads, no callbacks — the
application decides what work runs at each step.

### Arena Allocator

A linear bump allocator for fast, bulk-freeable scratch memory: job
scratch data, per-frame temporaries, procedural-generation buffers, and
anything else discarded all at once. Allocation is a pointer bump, so
there is no per-allocation free; reset or free the whole arena instead.

```c
#include <ludica_arena.h>  /* also pulled in by ludica.h */

lud_arena_t a;
lud_arena_init(&a, 1 << 20);          /* 1 MiB backing buffer */

float *verts = lud_arena_alloc(&a, n * sizeof *verts);
if (!verts) { /* arena full -- it does not grow */ }

lud_arena_reset(&a);                  /* reclaim everything, keep the buffer */
lud_arena_free(&a);                   /* release the buffer */
```

Each handout is aligned for any standard type. `lud_arena_alloc` returns
NULL when the arena is full (it does not grow) or when asked for zero
bytes. The struct is public: `a.off` is bytes used, `a.cap - a.off` is
bytes free.

# Automation

Ludica includes a TCP automation server and an MCP bridge for AI agent
integration and scripted testing. See `doc/manual/automation.md` for the
full protocol reference and MCP tool documentation.

## Quick Start

Launch any ludica game with automation enabled:

```sh
_out/x86_64-linux-gnu/bin/hero --auto-port 4000 --paused --fixed-dt
```

| Flag | Description |
|------|-------------|
| `--auto-port PORT` | Enable TCP automation on PORT |
| `--auto-file FILE` | Replay commands from a text file |
| `--capture-dir DIR` | Output directory for captures |
| `--paused` | Start frozen, wait for STEP commands |
| `--fixed-dt` | Constant 1/60s timestep for determinism |

## Making Games Automation-Friendly

Register named actions so agents can discover inputs semantically:

```c
lud_action_t a_jump = lud_make_action("jump");
lud_bind_key(LUD_KEY_SPACE, a_jump);
```

Register state variables so agents can query game state:

```c
static int score, level;
static const char *scene = "intro";

lud_auto_register_int("score", &score);
lud_auto_register_int("level", &level);
lud_auto_register_str("scene", &scene);
```

Variables must remain valid until shutdown. They are read via the
`QUERY VAR <name>` command or the MCP `query` tool.

## MCP Server

The `ludica-mcp-bridge` tool bridges MCP JSON-RPC (stdio) to the
`ludica-launcher` TCP protocol:

```sh
ludica-mcp-bridge [--port PORT]
```

It is built by `make` and output to `_out/<triplet>/bin/ludica-mcp-bridge`.
Configure it in `.mcp.json` at the project root:

```json
{
  "mcpServers": {
    "ludica": {
      "type": "stdio",
      "command": "_out/x86_64-linux-gnu/bin/ludica-mcp-bridge",
      "env": { "LUDICA_MCP_PORT": "4000" }
    }
  }
}
```

# Hero: Portal Rendering Engine

The `hero` program implements a portal-based 3D rendering engine.

## World Structure

The world is composed of **sectors** — convex polygons in the XZ plane. Each
sector has a floor height and ceiling height that define its vertical extent.

Each edge of a sector is either a **wall** (solid, rendered with a texture or
color) or a **portal** (an opening leading to an adjacent sector, rendered
by recursively drawing the connected sector).

## Rendering

The renderer processes sectors recursively:

1. Start from the sector containing the player
2. For each wall in the current sector, emit triangles (textured or colored)
3. For each portal, recurse into the connected sector
4. A depth limit prevents infinite recursion

Vertices are grouped by texture into draw groups, allowing efficient
multi-texture rendering within a single sector.

## Controls

| Key            | Action                    |
|----------------|---------------------------|
| W / Up         | Move forward              |
| S / Down       | Move backward             |
| A / Left       | Turn left                 |
| D / Right      | Turn right                |
| Q              | Strafe left               |
| E              | Strafe right              |
| Home           | Fly up                    |
| End            | Fly down                  |
| Page Up        | Look up                   |
| Page Down      | Look down                 |
| T              | Toggle texture/color mode |
| = / -          | Adjust FOV                |
| F11            | Toggle fullscreen         |
| Escape         | Quit                      |

# Appendix

## Third-Party Libraries

- **HandmadeMath** (v1.13.0) — Single-header C math library for vectors,
  matrices, and quaternions
- **stb_image** — Single-header image loading library
- **Dear ImGui** — Immediate-mode GUI library
- **miniaudio** — Single-header audio playback/capture library
- **jsmn** — Minimal header-only JSON parser (used by ludica-mcp)
