# Introduction

This manual covers the game engine built on the `initgl` rendering framework.
The engine provides a portal-based 3D rendering system using OpenGL ES 2.0.

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

## initgl Framework

`initgl` is the core rendering and platform abstraction library. It provides:

- **Platform layer**: Window creation, EGL context, event loop (X11 on Linux,
  Win32 on Windows)
- **Shader management**: Compile, link, and apply GLSL ES shaders
- **Mesh rendering**: Static and dynamic vertex buffer objects
- **Texture loading**: Image loading and GPU texture management
- **Sprite batching**: Efficient 2D sprite rendering
- **Bitmap fonts**: Fixed-width font rendering from texture atlases
- **Input handling**: Keyboard and mouse input via callbacks or polling

### Application Lifecycle

An initgl application implements callback functions and passes them via
a descriptor to `initgl_run()`:

```c
#include <initgl.h>

static void init(void) { /* one-time setup after GL context ready */ }
static void frame(float dt) { /* called each frame */ }
static void cleanup(void) { /* teardown */ }

int main(int argc, char **argv) {
    return initgl_run(&(initgl_desc_t){
        .app_name = "My App",
        .width = 800, .height = 600,
        .init = init,
        .frame = frame,
        .cleanup = cleanup,
    });
}
```

`initgl_run()` owns the main loop. On WASM it never returns.

### Shaders

Shaders are compiled from GLSL ES source strings via a descriptor:

```c
initgl_shader_t shader = initgl_make_shader(&(initgl_shader_desc_t){
    .vert_src = vertex_src,
    .frag_src = fragment_src,
    .attrs = { "a_position", "a_texcoord" },
    .num_attrs = 2,
});
initgl_apply_shader(shader);
initgl_uniform_mat4(shader, "u_mvp", matrix_data);
```

### Meshes

Vertex data is uploaded to GPU via mesh descriptors:

```c
initgl_mesh_t mesh = initgl_make_mesh(&(initgl_mesh_desc_t){
    .vertices = verts,
    .vertex_count = n,
    .vertex_stride = sizeof(Vertex),
    .layout = {
        { .size = 3, .offset = 0 },                       /* position */
        { .size = 2, .offset = offsetof(Vertex, u) },     /* texcoord */
    },
    .num_attrs = 2,
    .usage = INITGL_USAGE_STATIC,
    .primitive = INITGL_PRIM_TRIANGLES,
});
initgl_draw(mesh);
initgl_draw_range(mesh, first, count);
```

### Textures

```c
initgl_texture_t tex = initgl_load_texture("image.png",
    INITGL_FILTER_NEAREST, INITGL_FILTER_NEAREST);
initgl_bind_texture(tex, 0);
```

### Sprite Batching

For 2D rendering, initgl provides an immediate-mode sprite batch:

```c
initgl_sprite_begin(0, 0, screen_w, screen_h);
initgl_sprite_draw(tex, dx, dy, dw, dh, sx, sy, sw, sh);
initgl_sprite_rect(x, y, w, h, r, g, b, a);
initgl_sprite_end();
```

### Bitmap Fonts

```c
initgl_font_t font = initgl_make_default_font();
initgl_sprite_begin(0, 0, screen_w, screen_h);
initgl_draw_text(font, x, y, scale, "Hello");
initgl_sprite_end();
```

### Input

Events are delivered via the `event` callback in the descriptor:

```c
static int on_event(const initgl_event_t *ev) {
    switch (ev->type) {
    case INITGL_EV_KEY_DOWN: /* ev->key.code */ break;
    case INITGL_EV_MOUSE_MOVE: /* ev->mouse.x, ev->mouse.y */ break;
    }
    return 0;
}
```

Polled state is also available:

```c
if (initgl_key_down(INITGL_KEY_W)) { /* W held */ }
initgl_mouse_pos(&mx, &my);
float axis = initgl_gamepad_axis(0, 0);
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

| Key        | Action              |
|------------|---------------------|
| W / S      | Move forward / back |
| A / D      | Turn left / right   |
| Q / E      | Strafe left / right |
| Page Up    | Look up             |
| Page Down  | Look down           |
| T          | Toggle texture/color mode |
| Escape     | Quit                |

# Appendix

## Third-Party Libraries

- **HandmadeMath** (v1.13.0) — Single-header C math library for vectors,
  matrices, and quaternions
- **stb_image** — Single-header image loading library
- **Dear ImGui** — Immediate-mode GUI library
- **miniaudio** — Single-header audio playback/capture library
