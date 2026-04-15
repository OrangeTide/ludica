# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

Hero is a portal-based 3D engine sample built on the ludica framework. It
renders convex 2D sectors (polygons in the XZ plane) with walls, floors,
and ceilings. Sector sides are either solid walls or portals into adjacent
sectors; rendering recurses through portals up to `PORTAL_DEPTH` (10).

Ported from Jon Mayo's 2015 immediate-mode OpenGL prototype to ludica/GLES3.

## Build & Run

```sh
# from the repo root (ludica/)
make                            # build everything
make RELEASE=1                  # optimized build
_out/x86_64-linux-gnu/bin/hero  # run
```

Hero requires PBR texture assets in `assets/textures/` (diffuse, normal,
roughness, AO, height maps). Press **T** at runtime to toggle between
textured PBR and flat-color rendering.

## Architecture

**Single-file program** (`hero.c`, ~845 lines) plus one shader file.

### Coordinate System
- Sector vertices are 2D `(x, y)` mapped to world `(X, Z)`
- Floor/ceiling heights map to world `Y` (up)
- Sprite/HUD overlay is Y-down; 3D rendering is Y-up

### Key Data Structures
- `struct map_sector` — convex polygon with floor/ceil heights, per-side
  color indices, and portal destinations (`SECTOR_NONE` = solid wall)
- `struct sector_render` — GPU mesh + draw groups (sub-ranges sharing a texture)
- `struct texture_set` — 5-map PBR bundle (diffuse, normal, roughness, AO, height)
- `struct game_state` — player position/facing/pitch, FOV, texture toggle, time
- `struct world` — sectors, their render data, and texture array

### Rendering Pipeline
1. `build_sector_mesh()` — at init, tessellates each sector into a single
   mesh with draw groups ordered: floor (tex 0), ceiling (tex 1), walls (tex 2+)
2. `draw_sector_recursive()` — each frame, walks portals depth-first with a
   visited bitset (`sector_drawn[]`) to prevent infinite loops
3. Two shaders share one vertex stage (`portal_vert`):
   - `portal_textured` — Cook-Torrance PBR with parallax occlusion mapping,
     point light at player position with torch flicker
   - `portal_colored` — flat vertex colors from `wall_colors[]` table

### Shader System
Shaders live in `shaders/portal.glsl` using ludica's `@vs`/`@fs`/`@common`
section format. The build generates `shaders/portal.c` (listed as
`hero_GENERATED_SRCS` in `module.mk`), which exports `portal_vert`,
`portal_textured_frag`, and `portal_colored_frag` as C string constants.

### Vertex Format
`pos(3) + uv(2) + normal(3) + tangent(3) + color(3)` = 14 floats per vertex.
Attribute layout indices match the order in `lud_make_shader()`.

### Camera
Fixed horizontal FOV (default 80, adjustable 50-120) with vertical FOV
derived from aspect ratio. View matrix: translate, yaw (Y), pitch (-X).
Projection uses `HMM_Perspective()` with near=0.125, far=1000.

### Map Data
Currently hardcoded as `test_sectors[]` — two connected 4-sided sectors
with a portal on side 3/side 3. Player spawns at center of sector 1.

## Controls
W/S/arrows: forward/back, A/D: turn, Q/E: strafe, PgUp/PgDn: look,
Home/End: fly, +/-: FOV, T: toggle textures, F11: fullscreen, Esc: quit.
