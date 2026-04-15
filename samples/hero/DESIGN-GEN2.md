# Hero Gen2 Engine Design

Working document for the evolution of hero from a portal-sector demo into
a dungeon-delving game engine. The target experience is first-person
exploration of large interior spaces (dungeons, caves, towers, buildings)
and outdoor terrain, with seamless visibility through doors and openings
— no loading-screen transitions. Think Morrowind/Skyrim dungeon crawling,
with support for both authored and procedurally generated worlds.

This document is maintained alongside the code. Update it when design
decisions change or new constraints emerge.

---

## 1. Current State (Gen1)

### 1.1 Architecture Summary

Hero is a single-file C program (~845 lines) built on the ludica GLES3
framework. The world is composed of convex 2D sectors (polygons in the
XZ plane) extruded to floor/ceiling heights. Sector sides are either
solid walls or portals into adjacent sectors. Rendering recurses through
portals depth-first up to a configurable limit.

### 1.2 Coordinate System

- Sector vertices are 2D `(x, y)` in `struct sector_vertex`
- Mapped to world coordinates as `(X=x, Z=y)` with Y as the up axis
- Floor/ceiling heights define the Y extent of each sector
- Sprite/HUD overlay uses Y-down; 3D rendering uses Y-up

### 1.3 Data Structures

**Sector geometry:**

```c
struct sector_vertex { float x, y; };

struct map_sector {
    unsigned sector_number;
    float floor_height, ceil_height;
    unsigned num_sides;
    struct sector_vertex sides_xy[MAX_SIDES];      /* CCW winding */
    unsigned char color[MAX_SIDES];                /* wall color index */
    unsigned short destination_sector[MAX_SIDES];  /* SECTOR_NONE = wall */
};
```

Sectors are convex polygons with per-side portal or wall designation.
`SECTOR_NONE` (0xffff) marks a solid wall. Portals are bidirectional —
if sector A side 3 points to sector B, then sector B has a corresponding
side pointing back to sector A.

**Render data:**

```c
struct draw_group {
    int tex_index;  /* index into world textures */
    int first;      /* first vertex in mesh */
    int count;      /* vertex count */
};

struct sector_render {
    lud_mesh_t mesh;
    struct draw_group groups[MAX_DRAW_GROUPS];
    int num_groups;
};
```

Each sector has its own GPU mesh. Draw groups partition the mesh by
texture assignment: floor (tex 0), ceiling (tex 1), walls (tex 2+).

**PBR materials:**

```c
struct texture_set {
    lud_texture_t diffuse;    /* sRGB albedo */
    lud_texture_t normal;     /* tangent-space normal map */
    lud_texture_t roughness;  /* roughness map */
    lud_texture_t ao;         /* ambient occlusion */
    lud_texture_t height;     /* height map for parallax */
};
```

**World container:**

```c
struct world {
    const struct map_sector *sectors[MAX_SECTORS];
    struct sector_render render[MAX_SECTORS];
    unsigned num_sectors;
    struct texture_set textures[MAX_TEXTURES];
    unsigned num_textures;
};
```

**Player/game state:**

```c
struct game_state {
    float player_x, player_y;  /* sector XZ position */
    float player_z;            /* fly offset (Y axis) */
    float player_facing;       /* yaw in degrees */
    float player_height;       /* eye height above floor */
    float player_tilt;         /* pitch in degrees, clamped ±89° */
    unsigned player_sector;
    float hfov;                /* horizontal FOV, 50°–120° */
    bool use_textures;         /* runtime PBR/flat toggle */
    float time;                /* accumulated time for effects */
};
```

### 1.4 Vertex Format

14 floats per vertex (56 bytes):

| Attr | Name         | Size | Offset | Content                              |
|------|--------------|------|--------|--------------------------------------|
| 0    | a_position   | 3    | 0      | world XYZ                            |
| 1    | a_texcoord   | 2    | 12     | UV                                   |
| 2    | a_normal     | 3    | 20     | surface normal                       |
| 3    | a_tangent    | 3    | 32     | tangent (for TBN matrix)             |
| 4    | a_color      | 3    | 44     | vertex color (flat render fallback)  |

Attribute indices match the order in `lud_make_shader()`.

### 1.5 Shader System

Shaders are authored in `shaders/portal.glsl` using ludica's annotated
GLSL format:

- `@common` — shared declarations, prepended to all shaders
- `@vs <name>` — vertex shader, generates `const char <name>_vert[]`
- `@fs <name>` — fragment shader, generates `const char <name>_frag[]`
- `@end` — terminates a section

The build tool `tools/glsl2h` (awk-based) extracts sections and
generates a C source file with escaped string constants. Referenced in
`module.mk` as `hero_GENERATED_SRCS = shaders/portal.c`.

Current shaders:

- **portal_vert** — transforms position, builds TBN matrix from normal
  and tangent, computes tangent-space light and view vectors for the
  fragment shader. Supports a single point light.

- **portal_textured_frag** — full PBR pipeline:
  1. Parallax occlusion mapping (8–32 layers, height scale 0.04)
  2. Normal map sampling in tangent space
  3. Cook-Torrance specular BRDF (GGX distribution, Smith geometry,
     Fresnel-Schlick) with dielectric F0 = 0.04
  4. Lambertian diffuse with energy conservation
  5. Point light with inverse-square attenuation (1 + 0.35d + 0.44d²)
  6. Minimal ambient (0.00125, 0.005, 0.004) × albedo × AO
  7. Reinhard tone mapping
  8. Linear → sRGB gamma correction

- **portal_colored_frag** — passthrough vertex color, no lighting.

All shaders are GLES2 syntax (`#version 100`, `attribute`/`varying`/
`texture2D`/`gl_FragColor`) running on a GLES3 context (which accepts
both shader versions).

### 1.6 Mesh Building

`build_sector_mesh()` generates all geometry for one sector at init time:

1. **Floor** — triangle fan converted to triangle list. Normal (0,1,0),
   tangent (1,0,0). UV = sector (x,y) directly. Texture index 0.
2. **Ceiling** — same as floor with reversed winding. Normal (0,−1,0).
   Texture index 1.
3. **Walls** — each solid side (non-portal) becomes two triangles (quad).
   Wall tangent along the edge direction, normal perpendicular pointing
   inward. UV: U = distance along wall (0 to edge length), V = height
   (0 to wall_h). Texture index cycles through 2+.

Worst-case vertex count: `2 × (num_sides − 2) × 3 + num_sides × 6`.
Vertices are allocated, filled, uploaded via `lud_make_mesh()` with
`LUD_USAGE_STATIC`, then freed.

### 1.7 Rendering Pipeline

Per frame:

1. Process action toggles (texture mode, FOV, fullscreen, quit)
2. Update player position/rotation from input
3. Accumulate time for torch flicker
4. Set viewport, clear to black
5. Enable `GL_DEPTH_TEST` (less), `GL_CULL_FACE` (back)
6. Build MVP: projection × view
7. Select shader (textured or colored)
8. Set uniforms:
   - `u_mvp` — model-view-projection matrix
   - `u_light_pos`, `u_view_pos` — both set to player eye position
   - `u_light_color` — warm torch color modulated by 3-frequency flicker:
     `(2.8, 1.8, 0.9) × (1 + 0.08 sin(7.3t) + 0.05 sin(13.1t) + 0.03 sin(23.7t))`
   - `u_height_scale` — 0.04 (parallax depth)
   - Texture unit assignments 0–4
9. Clear `sector_drawn[]` visited bitset
10. `draw_sector_recursive(player_sector, PORTAL_DEPTH=10)`:
    - Skip if TTL exhausted, invalid sector, or already drawn
    - Mark sector as drawn
    - For each draw group: bind 5 PBR textures, `lud_draw_range()`
    - Recurse into portal destinations
11. Disable depth/cull for 2D overlay
12. Draw HUD text via sprite system (640×360 virtual coordinates)

### 1.8 Camera

**View matrix** — translate by negative player position, rotate yaw
around Y, rotate pitch around −X. Order: translate → yaw → pitch.
Uses HandmadeMath (degrees, not radians).

**Projection** — fixed horizontal FOV with vertical FOV derived from
aspect ratio: `vfov = 2 × atan(tan(hfov/2) / aspect)`. Near plane
0.125, far plane 1000. This prevents fisheye on ultrawide and avoids
vertical space loss on narrow screens.

### 1.9 Player Movement

- Forward/backward: 5 units/sec along facing direction (θ + π/2 offset)
- Strafe: 5 units/sec perpendicular to facing (along θ directly)
- Turn: 120°/sec yaw
- Pitch: 120°/sec, clamped to ±89°
- Fly: 5 units/sec vertical
- Facing wraps to [0, 360). No collision, no gravity.

### 1.10 Current Limits

| Constant        | Value  | Notes                          |
|-----------------|--------|--------------------------------|
| MAX_SIDES       | 64     | vertices per sector polygon    |
| MAX_SECTORS     | 64     | total sectors in world         |
| MAX_TEXTURES    | 8      | PBR texture sets               |
| MAX_DRAW_GROUPS | 66     | MAX_SIDES + 2 per sector       |
| PORTAL_DEPTH    | 10     | recursion limit                |
| SECTOR_NONE     | 0xffff | solid wall marker              |

### 1.11 Known Limitations

- **One mesh per sector, one draw call per draw group.** For N sectors
  with M walls each, this is roughly N×(M+2) draw calls with 5 texture
  binds each. Scales poorly past ~20 sectors.
- **No frustum culling.** Every reachable sector within PORTAL_DEPTH
  is drawn regardless of direction.
- **No portal clipping.** Full destination sector geometry is drawn even
  if only a sliver is visible through the portal opening.
- **No floor/ceiling portals.** Only wall sides can be portals. No
  multi-story spaces, stairwells, or overlapping areas.
- **No collision.** Player floats freely through walls and geometry.
- **No mesh loading.** All geometry is procedurally generated from sector
  data. No way to add visual detail beyond the extruded prism surfaces.
- **Static map only.** Sectors are hardcoded in `test_sectors[]`. No
  file format, no runtime editing, no procedural generation.
- **Single point light.** The PBR shader supports one torch light at the
  player position. No ambient lights, no light sources in the world.

---

## 2. Gen2 Design Goals

1. **Seamless visibility** — look through doors, windows, stairwells,
   and cave mouths into adjacent spaces. No loading-screen transitions
   for connected areas. The portal system handles visibility across all
   boundary types uniformly.

2. **Large interior spaces** — dungeons with dozens to hundreds of rooms,
   multi-story towers, winding cave systems. Must scale to hundreds of
   cells without proportional draw call growth.

3. **Outdoor terrain** — heightmap-based exterior with placed objects.
   Interior spaces connect to exterior via portal faces (cave mouths,
   building doors).

4. **Visual richness from reusable parts** — convex volumes define space
   and connectivity. Authored kit-piece meshes attached to volume faces
   provide visual detail (stone arches, curved walls, stair treads,
   torch sconces). The engine is not dependent on complex hand-modeled
   room meshes.

5. **Procedural generation** — the volume+portal+kit architecture must
   support runtime dungeon generation. Place volumes, connect with
   portals, assign kit themes, compute PVS, play.

6. **Runtime editing** — add, remove, resize volumes. Recompute affected
   portals and visibility. Useful for development and potentially for
   gameplay (destructible environments, building).

---

## 3. World Structure

### 3.1 Cell Types

The world is composed of **cells**. Each cell is a bounded region of
space with geometry, materials, and connections to other cells.

**Interior cell (convex prism volume):**
- Defined by a 2D convex polygon (the footprint) extruded between a
  floor height and ceiling height — a convex prism
- Each face of the prism (walls, floor, ceiling) is either:
  - A **solid face** with a surface material
  - A **portal face** connecting to an adjacent cell
- Faces carry **decoration attachments** — visual meshes and collision
  shapes that provide detail beyond the flat prism surfaces
- The volume itself is the default collision hull

**Exterior cell (terrain chunk):**
- A fixed-size grid of height values defining terrain elevation
- Texture splatmap for material blending (grass, rock, dirt, etc.)
- Placed objects list (trees, rocks, buildings, NPCs)
- Connects to interior cells via portal faces at cave mouths, doorways

### 3.2 Convex Prism Volumes in Detail

A volume generalizes the current `map_sector`:

```
volume {
    polygon: convex 2D vertex list (CCW), N vertices
    floor_height, ceil_height
    faces[N+2]:          /* N wall faces + floor + ceiling */
        type: SOLID | PORTAL
        material_id        /* if solid: surface material */
        portal_target      /* if portal: target cell + face */
        decorations[]      /* attached kit pieces */
}
```

The N wall faces correspond to the N edges of the polygon. Face indices
0..N-1 are walls (one per polygon edge), face N is the floor, face
N+1 is the ceiling. This extends the current model where only wall
faces (sides) could be portals.

**Why convex?** Convexity guarantees:
- Simple collision detection (separating axis test, support mapping)
- Correct portal clipping (any cross-section of a convex volume through
  a portal plane is a convex polygon)
- Trivial mesh generation (triangle fan for floor/ceiling, quads for
  walls)
- Simple point-in-volume test for determining which cell the player
  occupies

Non-convex rooms are built from multiple convex volumes joined by
portals — the portals between them are invisible to the player (no
doorframe, full-face opening). This is the same decomposition trick
used in Quake's BSP and the Build engine's sector splits.

### 3.3 Portal Faces

A portal face is a convex polygon shared between two cells. It defines:

- The geometric opening (vertices of the shared face)
- Source cell and face index
- Target cell and face index
- Whether the portal is a wall portal, floor portal, or ceiling portal

**Wall portals:** shared edge between two volumes at the same height
range, or overlapping height ranges. The portal polygon is the rectangle
(or trapezoid, if heights differ) defined by the shared edge and the
overlapping floor/ceiling range.

**Floor portals:** the floor face of an upper volume connects to the
ceiling face of a lower volume. Both faces must have the same 2D polygon
footprint (or the portal is the intersection of the two footprints).
Enables multi-story spaces, balconies, stairwells, open shafts.

**Ceiling portals:** reverse of floor portals. The ceiling opens upward
into another volume.

**Terrain portals:** a wall face of an interior volume meets the terrain
surface. The portal polygon sits flush with the terrain at the cave
mouth or building entrance. The terrain renderer and interior renderer
both need to be active when a terrain portal is in the visible set.

Portal faces are the universal connectivity primitive. The rendering
system treats them uniformly regardless of orientation — clip the
frustum to the portal polygon, recurse into the target cell.

### 3.4 Kit-Piece Decoration System

Decorations transform flat prism faces into visually rich surfaces.
Each decoration is:

```
decoration {
    mesh_id              /* reference to a reusable mesh asset */
    transform            /* position/rotation/scale relative to face */
    attachment_face      /* which volume face this attaches to */
    collision_shape      /* NONE | BOX | CYLINDER | RAMP | CONVEX_HULL */
    collision_params     /* shape-specific dimensions */
}
```

**Attachment model:** each volume face provides a local coordinate frame
(origin at face center, U/V axes along the face, normal pointing
outward). Decorations are positioned in this frame. When a volume is
resized or the face moves, decorations follow automatically.

**Collision shapes:** decorations can optionally contribute collision
geometry that modifies the walkable space within the volume:
- **NONE** — purely visual (ceiling beams, wall carvings)
- **BOX** — axis-aligned box in face-local coords (pillars, crates)
- **CYLINDER** — upright cylinder (round pillars, barrels)
- **RAMP** — inclined plane with start/end heights (stairs, ramps)
- **CONVEX_HULL** — arbitrary convex shape (irregular rock formations)

### 3.5 Design Patterns

These patterns demonstrate how convex volumes + kit decorations produce
complex-looking spaces from simple geometric primitives:

**Round tower room:**
- Volume: hexagonal prism (6-sided regular polygon)
- Wall decorations: 6 curved-wall mesh segments, each an arc that spans
  one hex face. The curved mesh has stone texture and correct normals.
  From inside, the room appears circular.
- Floor decoration: optional circular floor tile mesh

**Spiral staircase:**
- Volumes: stack of thin wedge-shaped prisms (pie slices), each rotated
  slightly from the one below. Floor of each wedge is a portal to the
  ceiling of the wedge below.
- Decorations: two reusable meshes per wedge — a stair tread mesh and
  a railing segment. Each tread has a RAMP collision shape.
- The same two meshes are repeated and rotated at every level. A 20-step
  spiral staircase is 20 wedge volumes with 40 decoration instances.

**Archway:**
- Volume: rectangular prism at the doorway
- Decorations: arch mesh filling the top corners of the portal face
  (the curved stone arch). Collision boxes at the arch corners prevent
  walking through the visual obstruction.
- The portal face behind the arch still has its full rectangular
  opening — the arch decoration narrows the visual opening without
  changing portal connectivity.

**Cave tunnel:**
- Volume: irregular convex prism (5–8 sided, asymmetric)
- Decorations: organic rock meshes on every face — stalactites on
  ceiling, uneven rock walls, scattered rubble on floor. Collision
  meshes narrow the walkable cross-section to an uneven passage.
- Different kit themes (limestone cave, lava tube, ice cavern) swap
  the decoration set without changing the volume.

**Vaulted ceiling hall:**
- Volume: large rectangular prism
- Ceiling decorations: vaulted arch mesh segments. No collision needed
  (player can't reach ceiling).
- Wall decorations: pillar meshes at regular intervals with cylinder
  collision shapes. Shelf/alcove meshes between pillars.

---

## 4. Rendering Architecture

### 4.1 Visibility Determination

The rendering pipeline determines which cells to draw using two layers:

**PVS (Potentially Visible Set):** precomputed per cell. For each cell,
a bitset of all cells reachable within N portal hops. This is a static
property of the portal graph — recompute only when the map changes or
the player changes cells.

PVS is a greedy over-approximation. It includes cells that might be
visible from any position and view direction within the source cell.
The cost of drawing a few extra cells is far less than the cost of
per-frame portal graph traversal.

**Frustum culling:** per frame. Intersect the PVS bitset with the view
frustum using per-cell bounding boxes. This eliminates cells that are
behind the player or far off to the side.

**Portal clipping (optional refinement):** for each visible portal face,
clip the view frustum to the portal polygon. Cells beyond the portal are
only drawn if they intersect the clipped frustum. This narrows the draw
set progressively through chains of portals.

Portal clipping is most valuable for the seamless door/window
requirement — it prevents drawing entire rooms that are barely visible
through a narrow opening, saving fragment shader work (the PBR shader
is expensive).

**Recursion termination:** portal clipping recurses until:
- The clipped frustum has zero area (portal is off-screen)
- The portal's screen-space area falls below a pixel threshold
- A maximum depth is reached (safety limit)

### 4.2 Draw Call Batching

The Gen1 renderer issues one draw call per draw group per sector. Gen2
must collapse this to as few draw calls as possible.

**Single merged VBO:** all volume geometry (floors, ceilings, walls) and
all decoration meshes packed into a single vertex buffer. Each drawable
element is a `(first, count, material_id)` tuple in a flat array.

**Texture arrays:** PBR texture sets (diffuse, normal, roughness, AO,
height) loaded into `GL_TEXTURE_2D_ARRAY` (GLES3). Each material is a
layer index. The shader samples by layer:

```glsl
vec3 albedo = texture(u_diffuse_array, vec3(uv, material_layer)).rgb;
vec3 normal = texture(u_normal_array, vec3(uv, material_layer)).rgb;
```

Bind the array textures once per frame. No per-draw-group texture
switching.

**Per-vertex material index:** encode the material layer as a vertex
attribute (or pack into an unused channel). The shader reads it per
vertex, eliminating the need for separate draw calls per material.

**Target:** all visible volume geometry in a single draw call. Decoration
meshes may require separate draw calls if they use different vertex
formats or materials not in the array, but the volume geometry (which is
the majority of triangles) should be one call.

### 4.3 Shader Evolution

The Gen1 PBR shader is already sophisticated (Cook-Torrance, parallax
occlusion, tone mapping). Gen2 changes:

- **Texture array sampling** as described above
- **Material index attribute** to select array layer
- **Multiple light sources** — the torch-at-player model extends to
  placed lights in the world (wall torches, braziers, glowing crystals).
  A uniform buffer of light positions/colors, iterated in the fragment
  shader. Practical limit ~8–16 lights per fragment on GLES3. Lights
  propagate through portals (see §9.3) — the per-cell light list
  includes own lights plus portal-propagated lights from neighbors,
  sorted by contribution and culled to fit the per-fragment budget.
- **Outdoor lighting** — directional sun/moon light for exterior cells,
  with ambient sky contribution. Interior cells use point/spot lights
  only. The shader selects the lighting model based on a per-vertex or
  per-material flag.
- **Shadow mapping** — not in initial Gen2 scope, but the architecture
  should not preclude it. Point light shadows via cubemap or dual-
  paraboloid, directional shadows via cascaded shadow maps.

### 4.4 Terrain Rendering

Exterior cells use a different geometry model:

- **Heightmap mesh:** regular grid of vertices, height sampled from a
  height array. One mesh per terrain chunk.
- **Texture splatting:** blend between terrain materials (grass, rock,
  dirt, snow) using a per-vertex or splatmap weight. Can use texture
  arrays for the material layers.
- **LOD:** distance-based level of detail. Far chunks use coarser grids.
  Geometry clipmaps or a quadtree structure.
- **Frustum culling:** per-chunk bounding boxes.

Terrain and interior geometry share the same frame and depth buffer.
When a terrain portal is visible, both the terrain chunk and the
interior cell behind the portal are drawn in the same frame.

### 4.5 Cell Streaming

Not all cells can be in GPU memory simultaneously for large worlds.

- **Active set:** cells within the PVS of the player's current cell,
  plus all exterior chunks within draw distance. These are loaded and
  have GPU resources allocated.
- **Loading boundary:** when the player approaches a cell near the edge
  of the active set, begin loading cells beyond it.
- **Unloading:** cells that leave the active set are unloaded after a
  grace period (avoid thrashing at boundaries).
- **Budget:** maximum total vertex/texture memory. If exceeded, unload
  least-recently-visible cells first.

For procedurally generated worlds, "loading" means generating the
volume layout, decoration placement, and mesh data on the fly.

---

## 5. Collision and Physics

### 5.1 Volume Collision

The convex prism volume is the primary collision shape. Player collision
is a swept capsule (or swept AABB for simplicity) tested against volume
faces.

**Wall collision:** player capsule vs. wall face planes. On collision,
slide along the wall (project velocity onto the wall plane).

**Floor/ceiling collision:** player snaps to floor height with gravity.
Ceiling prevents upward movement. Floor height is the volume's
floor_height plus any ramp decoration that the player stands on.

**Face normals:** since volumes are convex, all face normals point
outward. Collision response pushes the player inward (toward the volume
center). For an interior space this means the player is kept inside.

### 5.2 Decoration Collision

Decoration collision shapes modify the volume's walkable space:

- **Ramp:** overrides the floor height along its extent. A staircase is
  a sequence of ramp shapes (or stepped boxes). The player's ground
  height is the maximum of the volume floor and any ramp surface at
  the player's XZ position.
- **Pillar/box/cylinder:** solid obstacles within the volume. Player
  capsule collides against these shapes. Standard separating-axis or
  GJK for convex shapes.
- **Convex hull:** general case for irregular obstacles. Collision via
  GJK + EPA.

### 5.3 Sector Membership

The player is always "in" exactly one cell. When the player crosses a
portal face, they transition to the portal's target cell. This is
detected by checking which side of the portal plane the player is on.

Sector membership determines:
- Which PVS bitset to use for rendering
- Which cell's floor height to use for gravity
- Which decorations to test for collision
- Game logic (triggers, enemy AI line-of-sight)

---

## 6. Procedural Generation

### 6.1 Dungeon Generation Pipeline

1. **Layout generation** — place rooms as convex polygons on a 2D graph.
   Rooms can be rectangular, hexagonal, or irregular convex shapes.
   Connect rooms with corridor volumes. Assign floor/ceiling heights.

2. **Portal assignment** — for each pair of adjacent volumes, identify
   shared edges (wall portals) or stacked footprints (floor/ceiling
   portals). Create portal face records.

3. **Kit theming** — select a visual kit for each region (crypt, cave,
   tower, sewer). Assign decoration meshes to volume faces based on
   face type (wall, floor, ceiling, portal frame) and kit rules.

4. **PVS computation** — BFS/DFS through the portal graph to build
   per-cell bitsets. This is fast (milliseconds for hundreds of cells).

5. **Mesh generation** — build volume geometry and decoration instances.
   Pack into VBO. Load textures for the kits in use.

All steps can run at load time for a new dungeon level, or
incrementally during gameplay for infinite dungeon expansion.

### 6.2 Kit System

A kit is a collection of decoration meshes and rules for a visual theme:

```
kit "dungeon_crypt" {
    wall_base:    crypt_wall_base.mesh
    wall_detail:  [crypt_sconce.mesh, crypt_crack.mesh, crypt_alcove.mesh]
    floor:        crypt_flagstone.mesh
    ceiling:      crypt_vault.mesh
    portal_frame: crypt_arch.mesh
    column:       crypt_pillar.mesh
    rules:
        place wall_base on every wall face
        place wall_detail randomly on 30% of wall faces
        place column at convex corners where 2+ walls meet
        place portal_frame around every wall portal
}
```

Different kits can be mixed within a dungeon — a crypt region transitions
to a cave region through a portal, with each side using its own kit.

---

## 7. C Implementation Patterns

### 7.1 container_of for Heterogeneous Subsystems

Two subsystems in the Gen2 design operate on heterogeneous concrete
types through a uniform interface: the **cell system** (interior volumes
and terrain chunks) and the **placed object system** (items, props,
NPCs, lights, triggers). These use the Linux kernel's `container_of`
pattern — embed a common struct in each concrete type, write generic
code against the embedded struct, and recover the concrete type when
type-specific behavior is needed.

```c
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
```

**Cell subsystem:**

```c
/* Generic cell — the portal, PVS, culling, and streaming systems
 * operate on this struct without knowing the concrete cell type. */
struct cell {
    unsigned id;
    enum cell_type { CELL_VOLUME, CELL_TERRAIN } type;
    float bbox[6];                     /* AABB for frustum culling */
    uint64_t pvs[PVS_WORDS];          /* potentially visible set */
    struct portal_face *portals;       /* portal list */
    unsigned num_portals;
    bool loaded;                       /* streaming state */
};

/* Interior cell — convex prism volume with decorations */
struct volume_cell {
    struct cell cell;                  /* embedded, must be first or use container_of */
    struct sector_vertex *polygon;
    unsigned num_sides;
    float floor_h, ceil_h;
    struct face *faces;               /* wall/floor/ceil faces */
    unsigned num_faces;
    struct decoration *decorations;
    unsigned num_decorations;
};

/* Exterior cell — heightmap terrain chunk */
struct terrain_cell {
    struct cell cell;
    float *heightmap;
    unsigned grid_size;
    float chunk_x, chunk_z;           /* world position */
    struct placed_object *objects;     /* trees, rocks, etc. */
    unsigned num_objects;
};
```

Generic code operates on `struct cell *`:

```c
/* PVS, frustum culling, streaming — type-agnostic */
void pvs_compute(struct cell *cells, unsigned count);
bool cell_in_frustum(const struct cell *c, const hmm_mat4 *vp);
void cell_stream_update(struct cell *cells, unsigned count, float px, float pz);
```

Type-specific code recovers the concrete type:

```c
void cell_draw(struct cell *c) {
    switch (c->type) {
    case CELL_VOLUME:
        draw_volume(container_of(c, struct volume_cell, cell));
        break;
    case CELL_TERRAIN:
        draw_terrain(container_of(c, struct terrain_cell, cell));
        break;
    }
}
```

**Placed object subsystem:**

```c
/* Generic object — spatial index, pick system, and cell membership
 * operate on this struct. */
struct object {
    unsigned id;
    enum object_type { OBJ_ITEM, OBJ_PROP, OBJ_NPC, OBJ_LIGHT, OBJ_TRIGGER } type;
    float pos[3];
    float radius;                      /* bounding sphere for culling/picking */
    struct cell *cell;                 /* which cell this object is in */
};

struct light_object {
    struct object obj;
    float color[3];
    float intensity;
    float falloff_linear, falloff_quad;
};

struct npc_object {
    struct object obj;
    unsigned ai_state;
    float health;
    unsigned mesh_id;
    /* ... */
};
```

The spatial index stores `struct object *` pointers. Frustum culling,
distance sorting, and pick testing are generic. Rendering, AI updates,
and interaction dispatch by `obj->type` and `container_of` to the
concrete type.

### 7.2 Where Not to Use container_of

Everything else in the design is uniform within its type and doesn't
benefit from this pattern:

- **Decorations** — always `struct decoration`. Collision shape variance
  is handled by a tagged union inside the struct, not by embedding.
- **Portal faces** — always `struct portal_face`. Wall/floor/ceiling
  orientation is a property, not a type.
- **Draw groups** — always `(first, count, material_id)`. Flat array.
- **Kit rules** — data tables, not polymorphic objects.

Using `container_of` in these cases would add indirection with no
abstraction benefit. Tagged unions and separate arrays by type are
simpler and more cache-friendly for uniform collections.

### 7.3 Collision Shape Tagged Union

The decoration collision system uses a tagged union rather than
`container_of` because collision shapes are a property of a decoration,
not a standalone subsystem with heterogeneous participants:

```c
struct collision_shape {
    enum {
        COLL_NONE,
        COLL_BOX,
        COLL_CYLINDER,
        COLL_RAMP,
        COLL_HULL,
    } type;
    union {
        struct { float hx, hy, hz; } box;
        struct { float radius, height; } cylinder;
        struct { float length, rise; } ramp;
        struct { int hull_id; } hull;
    };
};

struct decoration {
    unsigned mesh_id;
    float transform[16];              /* 4x4 relative to face */
    unsigned attachment_face;
    struct collision_shape collision;
    unsigned lod_count;               /* number of LOD variants */
};
```

The collision dispatch is a switch on `collision.type` — simple, inline,
no pointer chasing.

### 7.4 Spatial Indexing Strategy

The engine has five distinct spatial query workloads. The portal/cell
system itself acts as the primary spatial partition, which eliminates the
need for heavyweight tree structures in most cases.

**Query workloads and chosen structures:**

| Query | Structure | Scale | Rationale |
|-------|-----------|-------|-----------|
| Cell frustum cull | PVS bitset + linear AABB test | ~10–30 candidates/frame | PVS narrows hundreds of loaded cells to a small set. Testing 30 AABBs against 6 frustum planes is a microsecond. A tree adds pointer chasing and complexity for no measurable gain. |
| Object frustum cull | Flat array per cell; grid for dense cells | 0–20 typical, up to thousands for exterior | Most cells have few objects — linear scan suffices. Dense exterior chunks (forests, rubble fields) use a 2D grid binned by XZ position; query only bins overlapping the frustum footprint. |
| Decoration collision | Flat array per cell | <50 shapes/cell | Swept capsule vs. N simple primitives (box, cylinder, ramp). Each test is cheap. N is small. No index needed. |
| Ray pick | Portal graph walk | 1–5 cells along ray | Cast from player's cell, test objects, advance through portal faces the ray intersects. The cell/portal structure *is* the spatial index for rays. |
| Light gather | Portal graph BFS | 1–3 hop neighborhood | Topology-based (portal hop count), not distance-based. Collect own lights + lights in portal-adjacent cells. The graph traversal from §9.3 handles this directly. |

**Why not BVH?**

BVH (bounding volume hierarchy) is optimal when you have a single large
triangle mesh and need to raycast or collide against it — thousands of
triangles with irregular spatial distribution, where a tree reduces
O(N) to O(log N) per query. Classic use: collision against authored
room meshes (Morrowind-style).

The Gen2 design specifically avoids this scenario. Geometry is
partitioned into cells. Within each cell, collision shapes are simple
convex primitives (box, cylinder, ramp, convex hull), not triangle
soups. The cell boundary itself provides the coarse spatial partition;
the fine-grained collision is against a handful of primitives per cell.

BVH would only become relevant if decoration meshes grow to use complex
triangle-level collision (the `COLL_HULL` case at scale — hundreds of
hull shapes in a single cell). Even then, it would be a per-cell local
BVH, not a global one.

**Why not octree or k-d tree?**

Octrees and k-d trees are designed for point clouds and irregular 3D
distributions. Our data is organized into discrete cells with mostly
2D spatial extent (prisms are polygons × height, terrain is a 2D grid).
A 3D volumetric index wastes a dimension and adds complexity for data
that is already spatially partitioned by the cell system.

K-d trees also have poor cache behavior for dynamic data (frequent
object insertion/removal as cells stream in and out). A grid has O(1)
insertion and removal.

**Why the portal graph is the right primary structure:**

The cell/portal graph is a spatial partition tuned to the world's actual
connectivity. A room behind a closed door is topologically distant (many
portal hops) even if geometrically close (one meter through the wall).
This matches gameplay needs — visibility, sound, AI line-of-sight, and
light propagation all follow portal connectivity, not Euclidean
distance.

Traditional spatial indices (BVH, octree, k-d tree) partition by
geometric proximity. They would group a room and the hallway on the
other side of its wall together, which is wrong for every query that
cares about reachability. The portal graph gets this right by
construction.

**Escalation path if bottlenecks emerge:**

If per-cell object counts grow large enough to matter (profiling shows
linear scan as a hotspot), the first step is a 2D grid within the cell:

```c
struct cell_grid {
    unsigned cell_w, cell_h;    /* grid dimensions */
    float origin_x, origin_z;  /* world-space origin */
    float cell_size;            /* grid cell width */
    struct object **bins;       /* array of object lists per bin */
};
```

Simple to implement, cache-friendly (sequential bin access), O(1) cell
lookup, good for the uniform spatial distributions typical of placed
objects. Reach for a BVH only if profiling shows the grid is inadequate
for a specific case.

---

## 8. Phased Implementation Plan

### Phase 1 — Rendering Foundation

Improve the current renderer without changing the geometry model.
All changes are backward-compatible with the existing 2-sector test map.

- Merge all sector geometry into a single VBO
- Implement `GL_TEXTURE_2D_ARRAY` for PBR texture sets
- Add per-vertex material index attribute
- Sort draw groups by material, minimize draw calls

### Phase 2 — Portal Visibility

- Precompute PVS bitsets per sector (BFS through portal graph)
- Replace `draw_sector_recursive()` with PVS-driven flat loop
- Add per-cell bounding boxes and frustum culling
- Implement portal frustum clipping with screen-area termination

### Phase 3 — Convex Volume Cells

Replace 2D sector extrusion with the general convex prism model.

- Define volume data structure with per-face portal/material/decoration
- Implement floor and ceiling portals
- Build the decoration attachment system (mesh + collision per face)
- Move test map to a data file or simple procedural generator
- Support non-axis-aligned portal faces (for ramps, angled connections)

### Phase 4 — Exterior Terrain

- Heightmap terrain renderer with per-chunk meshes
- Texture splatting for terrain materials
- Frustum culling and distance-based LOD for terrain chunks
- Cell streaming (load/unload by player distance)
- Terrain-to-interior portal transitions (cave mouths, building doors)

### Phase 5 — Collision and Physics

- Swept capsule collision against volume faces
- Wall sliding
- Floor following with gravity
- Decoration collision shapes (ramp, box, cylinder)
- Sector membership tracking across portal transitions

### Phase 6 — Game Systems

- Object placement in cells (items, props, NPCs)
- Spatial index for placed objects
- Pick/interact system
- Procedural dungeon generator with kit theming
- Multiple light sources

---

## 9. Design Decisions

Resolved questions and standing decisions. Update when decisions change.

### 9.1 Portal Face Geometry for Mismatched Footprints

**Decision:** compute polygon intersection. When a floor portal connects
two volumes with different 2D footprints, the portal polygon is the
intersection of the two footprints. The non-portal remainder of each
face is filled with the face's surface material (texture, color, etc.)
and rendered as solid geometry.

This means portal faces don't need a material attribute — they have no
visible surface. Only the solid remainder around the portal opening
needs texturing. Fully matching portals (where the intersection equals
both footprints) produce no solid remainder, so they contribute zero
textured triangles and don't participate in texture-based draw sorting.

Partial portals (hole in a larger floor) produce a solid border around
the opening. This border is textured and sorted normally.

### 9.2 Decoration LOD

**Decision:** decorations support multiple LOD levels per mesh. Each
decoration mesh ships with 2–3 LOD variants (full detail, simplified,
billboard or omitted). The distance thresholds will be tuned empirically
once the system is working — no point choosing numbers before we can see
the results.

LOD selection is per-decoration-instance based on distance from camera.
Volume surfaces (flat prism faces) are always drawn at full detail
regardless of distance since they're cheap geometry.

### 9.3 Light Propagation Through Portals

**Decision:** lights propagate through portals. A torch in one room
illuminates surfaces in adjacent rooms visible through the portal
opening.

Implementation approach:
- Each cell collects lights from its own placed light sources plus
  lights visible through portal faces from neighboring cells
- Light contribution is attenuated by distance as normal — the portal
  doesn't add extra attenuation, the geometry naturally limits what's
  illuminated
- Sort and cull lights that contribute negligibly to reduce per-fragment
  work. Especially cull lights leading into brighter areas where their
  contribution would be imperceptible.
- **Portal ambient projection:** compute per-cell ambient brightness
  (average of all lights in the cell). Project this ambient level through
  portal faces as a directional light aimed into the adjacent cell. A
  brightly lit hall behind a doorway casts a directed wash of light into
  a dark corridor — this creates the striking contrast effect of seeing
  light spill through a door between rooms with very different light
  levels.

The per-fragment light limit (~8–16 on GLES3) means aggressive culling
is necessary. Prioritize lights by: (1) distance to fragment, (2)
intensity at fragment, (3) whether the light is the dominant source in
its cell. Distant portal-propagated lights are the first to be culled.

### 9.4 Audio Occlusion

**Decision:** design for it, build it later. Portal connectivity and
the PVS system naturally provide the data needed for audio propagation:
- Sound sources attenuated by portal-hop count
- Low-pass filtering proportional to portal distance (muffled sounds
  through walls/doors)
- Portal face area affects how much sound passes through (narrow crack
  vs. wide archway)

Data structure implication: portal faces should store their geometric
area (or it should be trivially computable). The PVS precomputation
already provides hop counts. No additional data structures are needed
beyond what the visual portal system requires.

Audio occlusion is not in the initial implementation phases but the
portal system should not be designed in a way that makes it hard to add.

### 9.5 Map File Format

**Decision:** binary runtime format, generated from script sources.

- **Runtime format:** binary, loaded directly into data structures. Fast
  loading, compact, memory-mappable. Versioned header for forward
  compatibility.
- **Source format:** an executable script (Lua, JavaScript, or a Pascal
  subset) that runs and emits the binary format. This supports both
  hand-authored maps (script places volumes explicitly) and procedural
  maps (script runs generation algorithms). The script language provides
  loops, conditionals, and math for procedural layout.
- **Toolchain:** offline compiler runs the script, validates the output
  (convexity, portal consistency, PVS), and writes the binary. An online
  editor can also emit the binary directly for runtime editing.
- **Multi-file worlds:** multiple map files connect together to form a
  complete world. Each file defines a region (dungeon level, terrain
  area, building). Cross-file portal references use named connection
  points. The loader resolves connections when files are loaded together.

### 9.6 Terrain Carve-Outs

**Decision:** needs further design, but the concept is promising.

The idea: terrain is rendered normally (full heightmap mesh), but certain
volumes act as **negative space** — they define regions where terrain
should not be drawn. The same carve-out volume that removes terrain also
serves as the portal into interior space.

Possible implementation:
- Carve-out volumes are convex prisms projected onto the terrain surface
- During terrain mesh generation (or as a stencil operation at render
  time), triangles inside the carve-out footprint are removed or clipped
- The carve-out boundary becomes the portal face between terrain and
  interior
- This avoids hand-editing terrain meshes to create cave openings —
  place a carve-out volume and the hole appears automatically

**Open sub-questions:**
- Should carve-outs modify the terrain mesh at build time (permanent
  hole in the chunk) or at render time (stencil buffer masking)?
  Build-time is simpler but means terrain chunks with carve-outs can't
  be shared/reused. Render-time is more flexible but adds GPU cost.
- How does the carve-out boundary interact with terrain LOD? A distant
  chunk at low LOD still needs the carve-out hole if the interior is
  visible through it.
- Can carve-outs be non-convex? (e.g., a wide cave mouth with an
  irregular rock outline). If carve-outs must be convex, complex
  openings need multiple carve-outs — same decomposition as rooms.
- Vertical carve-outs (cliff faces, mine entrances in hillsides) need
  to cut the terrain mesh along a non-horizontal plane. This is more
  complex than a top-down footprint projection.

This feature is not blocking for initial terrain implementation. Phase 4
can start with manually placed portal faces at terrain level and add
carve-out automation later.
