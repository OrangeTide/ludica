# Ludica Engine Features for 3D Game Support

Design notes from analysis of the hero Gen2 engine requirements.
Documents what ludica should provide vs. what stays game-side.

## Guiding Principle

Ludica is a framework, not an engine. It provides platform abstraction,
GPU resource management, and reusable primitives. Game-specific
architecture (portal graphs, scene graphs, ECS) stays in the game or
in a separate engine library built on ludica.

```
ludica (framework)     -- platform, GL, input, audio, sprites, fonts, jobs
  ^
hero-engine (library)  -- cells, portals, PVS, streaming, collision, terrain
  ^
hero (game)            -- procgen, kits, game logic, content
```

---

## 1. Job System (`ludica_job.h`)

**Priority: HIGH** -- unblocks cell streaming, async asset loading,
procedural generation. Every 3D game benefits.

### Problem

The hero Gen2 design has these parallelizable workloads:

- Cell streaming (generate/load cells as player moves)
- PVS computation (BFS through portal graph)
- Asset loading (texture decode, mesh building)
- Procedural generation (noise, dungeon layout, terrain heightmaps)

These must work on native (pthreads available) and WASM (single-threaded,
or limited SharedArrayBuffer pthreads). The app should not need
conditional compilation for threading.

### Design: Work Queue with Deferred Completion

```c
// Submit work -- returns immediately
lud_job_t lud_job_submit(lud_job_func_t func, void *arg,
                         lud_job_func_t on_complete);

// Poll completions on main thread (ludica calls this each frame;
// app can also call for finer control)
int lud_job_poll(int max_completions);

// Block until done (use sparingly, mainly during init)
void lud_job_wait(lud_job_t job);

// Query status
bool lud_job_done(lud_job_t job);
```

**`func`** runs on a worker thread (native) or inline during
`lud_job_poll` (WASM fallback). Must not touch GL or main-thread state.

**`on_complete`** runs on the main thread during `lud_job_poll`. This is
where GPU uploads happen (`lud_make_mesh`, `lud_make_texture`).

### Job Groups (bulk loading)

```c
lud_job_group_t lud_job_group_create(void);
void lud_job_group_add(lud_job_group_t g, lud_job_func_t func,
                       void *arg, lud_job_func_t on_complete);
void lud_job_group_submit(lud_job_group_t g);
bool lud_job_group_done(lud_job_group_t g);
void lud_job_group_wait(lud_job_group_t g);
```

`lud_job_group_wait` integrates with `lud_draw_progress` for loading
screens.

### WASM Single-Threaded Fallback

`lud_job_submit` pushes onto a queue. `lud_job_poll` runs jobs inline
with a per-frame time budget (~4ms) so the frame loop doesn't stall.

**Constraint:** jobs should be written as chunkable/resumable for WASM.
Long procgen jobs yield after N iterations. This is the one thing the
app must be aware of.

### Worker Count

Ludica picks worker count based on platform and CPU count. The app
doesn't choose or manage threads.

### Arena Allocator for Job Data

Jobs that produce temporary data (mesh vertex arrays, heightmap buffers)
need allocation that's fast and bulk-freeable. A bump/arena allocator
fits naturally:

- Worker allocates from a per-job arena (fast, no contention)
- Completion callback reads the data and uploads to GPU
- Arena is reset after completion

Reference implementation: `DEVEL/osdev/cmd/jsh/jsh.c` lines 533-647.
A bump allocator with a hybrid strategy -- `node_alloc()` routes to the
arena for ephemeral data (REPL parse trees) or to malloc for persistent
data (function definitions). `freetree()` only frees malloc'd trees and
has a safety check against freeing arena nodes. The same pattern applies
to job data: arena for per-frame scratch, malloc for data that outlives
the job.

Ludica should provide a simple arena allocator:

```c
typedef struct { char *buf; int off, cap; } lud_arena_t;
void  lud_arena_init(lud_arena_t *a, int size);
void *lud_arena_alloc(lud_arena_t *a, int size); /* 4-byte aligned */
void  lud_arena_reset(lud_arena_t *a);
void  lud_arena_free(lud_arena_t *a);
```

Games can use this for job scratch data, per-frame temporaries,
procedural generation buffers, and any context where bulk-free is
appropriate.

---

## 2. Graphics API Extensions

### 2a. Texture Arrays (GLES3)

**Priority: HIGH** -- required for Gen2 Phase 1 draw call batching.

```c
lud_texture_t lud_make_texture_array(const lud_texture_array_desc_t *desc);
void lud_texture_array_set_layer(lud_texture_t arr, int layer,
                                 const void *data);
```

Bind once per frame, index by material layer in the shader. Eliminates
per-draw-group texture switching.

### 2b. Mesh Update (Streaming VBO)

**Priority: MEDIUM** -- needed for Phase 4 cell streaming.

```c
void lud_update_mesh(lud_mesh_t mesh, int first_vertex, int count,
                     const void *data);
```

Partial VBO updates for streaming world geometry.

### 2c. Instanced Drawing (GLES3)

**Priority: MEDIUM** -- useful for Phase 3 kit decorations.

```c
void lud_draw_instanced(lud_mesh_t mesh, int instance_count);
```

Same mesh drawn many times with per-instance transforms (decoration
kit pieces).

### 2d. Deferred Resource Destruction

**Priority: MEDIUM** -- needed for safe streaming.

```c
void lud_destroy_mesh_deferred(lud_mesh_t mesh);
void lud_destroy_texture_deferred(lud_texture_t tex);
```

Queues GL delete to end of frame, preventing use-after-free when a cell
is unloaded while its mesh is still referenced by an in-flight draw.

---

## 3. Math / Spatial Utilities

### 3a. Frustum Culling

**Priority: MEDIUM** -- needed for Phase 2 portal visibility.

```c
typedef struct { float min[3], max[3]; } lud_aabb_t;
bool lud_frustum_test_aabb(const float frustum_planes[6][4],
                           const lud_aabb_t *box);
void lud_frustum_extract(const float mvp[16], float planes[6][4]);
```

Small utility, useful for any 3D game. Does not impose scene structure.

---

## 4. Collision Primitives (`ludica_phys.h`)

**Priority: LOW** -- Phase 5 in Gen2. Develop in-game first, extract
to ludica when the API stabilizes.

```c
typedef struct { float base[3]; float height, radius; } lud_capsule_t;

bool lud_sweep_capsule_plane(const lud_capsule_t *cap,
                             const float vel[3],
                             const float plane[4],
                             lud_contact_t *out);

void lud_slide_velocity(float vel[3], const lud_contact_t *contact);
```

Swept capsule vs. convex shapes, wall sliding. General enough for any
first/third-person 3D game.

---

## 5. What Ludica Should NOT Provide

These are game-specific or engine-specific and don't belong in ludica:

- **Portal/cell system** -- deeply coupled to game world representation.
  Other games use BSP, octrees, open-world chunks, or none.
- **Scene graph / ECS** -- commits all users to a paradigm. Games that
  want ECS can use flecs or similar.
- **Procedural generation** -- noise, dungeon layout, terrain gen are
  game content decisions.
- **Kit/decoration system** -- game content pipeline.

---

## Priority Summary

| Feature              | Priority | Unblocks         | Size  |
|----------------------|----------|------------------|-------|
| Job system           | HIGH     | Streaming, async | ~500L |
| Texture arrays       | HIGH     | Gen2 Phase 1     | ~150L |
| Arena allocator      | HIGH     | Jobs, procgen    | ~100L |
| Mesh update          | MEDIUM   | Gen2 Phase 4     | ~50L  |
| Frustum utilities    | MEDIUM   | Gen2 Phase 2     | ~80L  |
| Instanced drawing    | MEDIUM   | Gen2 Phase 3     | ~50L  |
| Deferred destruction | MEDIUM   | Safe streaming   | ~80L  |
| Collision primitives | LOW      | Gen2 Phase 5     | ~300L |
