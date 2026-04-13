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

### Textures

```c
ludica_texture_t tex = lud_load_texture("image.png",
    LUD_FILTER_NEAREST, LUD_FILTER_NEAREST);
lud_bind_texture(tex, 0);
```

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
