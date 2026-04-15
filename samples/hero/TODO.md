# Hero — TODO

Goal: dungeon-delving game with large interiors, tunnels, and outdoor
terrain. Seamless visibility through doors and openings (no loading
screen transitions). Support both authored and procedurally generated
worlds.

## Design Direction

The world is composed of **cells** connected by **portal faces**:

- **Interior cells** are convex prism volumes (2D convex polygon extruded
  to floor/ceil heights). Walls, floors, and ceilings can all be portals.
  Visual detail and complex collision come from **kit-piece decorations**
  attached to volume faces, not from the volume geometry itself.
- **Exterior cells** are terrain heightmap chunks with placed objects.
- **Portal faces** are the universal connectivity primitive — doorways
  between rooms, cave mouths where an interior meets terrain, stairwells
  between floors, windows. The renderer clips the view frustum to each
  portal polygon and recurses.

Examples of convex volumes + kit decoration:
- Round tower: hex prism + 6 curved-wall meshes with arc geometry
- Spiral staircase: stack of thin wedge prisms, each with a step mesh
  and ramp collision shape. Two reusable meshes, repeated and rotated.
- Archway: rectangular portal volume + arch mesh filling corners
- Cave tunnel: irregular prism + organic rock meshes on all faces,
  collision meshes narrowing the walkable space

Kit decorations are first-class: each volume face has attachment points,
each decoration declares its visual mesh and collision shape.

## Phase 1 — Rendering Foundation

Improve the current renderer before changing the geometry model.

- [ ] merge all sector geometry into a single VBO
  - draw groups as flat array of `(first, count, tex_index)` sorted by texture
- [ ] use `GL_TEXTURE_2D_ARRAY` for PBR texture sets (GLES3)
  - bind once per frame, layer index per draw group
  - shader samples via `texture(u_array, vec3(uv, layer))`
- [ ] encode material/layer index as per-vertex attribute
  - goal: single draw call for all visible geometry

## Phase 2 — Portal Visibility

- [ ] precompute PVS bitsets per cell
  - BFS/DFS through portal graph, store as bitset per cell
  - recompute only when player changes cells (or on map edit)
- [ ] replace `draw_sector_recursive()` with flat PVS-driven loop
- [ ] frustum culling against cell bounding boxes
- [ ] portal clipping — clip frustum to portal polygon at each recursion
  - required for seamless door/window visibility
  - screen-area threshold stops recursion for distant portals

## Phase 3 — Convex Volume Cells

Replace 2D sector extrusion with the general convex prism model.

- [ ] define volume data structure
  - 2D convex polygon + floor/ceil heights (generalizes current sectors)
  - per-face: portal reference or surface material
  - per-face: decoration attachment list
- [ ] floor and ceiling portals
  - shared face between volume above and volume below
  - enables multi-story spaces, stairwells, balconies
- [ ] decoration attachment system
  - mesh reference + transform relative to attachment face
  - collision shape (box, cylinder, ramp, convex hull)
  - face attachment points computed from volume geometry
- [ ] move test map to a cell data file (or procedural generator)

## Phase 4 — Exterior Terrain

- [ ] heightmap terrain renderer
  - grid of height values per chunk, single mesh per chunk
  - texture splatting (terrain material blending)
  - frustum cull chunks, distance-based LOD
- [ ] cell streaming
  - load/unload terrain chunks based on player distance
  - interior cells loaded when reachable within PVS from any loaded cell
- [ ] terrain-to-interior portal transitions
  - cave mouth: portal face on volume flush with terrain surface
  - building door: portal face on volume at terrain level

## Phase 5 — Collision and Physics

- [ ] swept capsule or AABB collision against volume faces
- [ ] wall sliding along volume faces
- [ ] floor following (gravity, snap to floor height)
- [ ] decoration collision shapes (pillars, stairs, furniture)
  - staircase: ramp or stepped box collision
  - pillar: cylinder collision
- [ ] ramp/stair traversal from collision shape metadata

## Phase 6 — Game Systems

- [ ] object placement in cells (items, props, NPCs)
- [ ] spatial index for placed objects (grid or BVH)
- [ ] pick/interact system
- [ ] procedural dungeon generator
  - place room volumes, connect with corridor volumes
  - assign kit themes per region
  - compute portal faces and PVS on the fly
