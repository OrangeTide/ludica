/*
 * hero - portal-based 3D engine
 *
 * Ported from Jon Mayo's 2015 immediate-mode OpenGL prototype to ludica/GLES2.
 *
 * The world is composed of convex 2D sectors (polygons in the XZ plane).
 * Each sector side is either a solid wall or a portal into an adjacent sector.
 * Rendering recurses through portals up to a depth limit.
 *
 * Coordinate convention:
 *   sector (x, y)  ->  world (X, Z)
 *   floor/ceil height  ->  world Y  (up)
 */
#include "ludica.h"
#include "ludica_gfx.h"
#include "ludica_font.h"
#include <GLES2/gl2.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>

#define HANDMADE_MATH_IMPLEMENTATION
#include "HandmadeMath.h"

/* ------------------------------------------------------------------ */
/* Constants                                                          */
/* ------------------------------------------------------------------ */

#define ARRAY_SIZE(a) (sizeof(a) / sizeof*(a))
#define SECTOR_NONE   0xffff
#define MAX_SIDES     64
#define MAX_SECTORS   64
#define MAX_TEXTURES  8
#define MAX_DRAW_GROUPS (MAX_SIDES + 2)
#define PORTAL_DEPTH  10
#define DEFAULT_HFOV  80.0f   /* horizontal FOV in degrees */
#define MIN_HFOV      50.0f
#define MAX_HFOV      120.0f
#define HFOV_STEP      5.0f

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ */
/* Vertex format: pos(3) + uv(2) + normal(3) + tangent(3) + color(3) */
/* ------------------------------------------------------------------ */

struct vertex {
	float x, y, z;
	float u, v;
	float nx, ny, nz;
	float tx, ty, tz;
	float r, g, b;
};

/* ------------------------------------------------------------------ */
/* Wall / surface color table (used in color-only render mode)        */
/* ------------------------------------------------------------------ */

static const float wall_colors[][3] = {
	{ 1.0f, 1.0f, 1.0f },  /* 0  white   */
	{ 0.0f, 1.0f, 1.0f },  /* 1  cyan    */
	{ 1.0f, 0.0f, 1.0f },  /* 2  magenta */
	{ 1.0f, 1.0f, 0.0f },  /* 3  yellow  */
	{ 1.0f, 0.0f, 0.0f },  /* 4  red     */
	{ 0.0f, 1.0f, 0.0f },  /* 5  green   */
	{ 0.0f, 0.0f, 1.0f },  /* 6  blue    */
	{ 0.5f, 0.5f, 0.5f },  /* 7  gray    */
	{ 0.0f, 0.5f, 0.5f },  /* 8          */
	{ 0.5f, 0.0f, 0.5f },  /* 9          */
	{ 0.5f, 0.5f, 0.0f },  /* 10         */
	{ 0.5f, 0.0f, 0.0f },  /* 11         */
	{ 0.0f, 0.5f, 0.0f },  /* 12         */
	{ 0.0f, 0.0f, 0.5f },  /* 13         */
};

/* ------------------------------------------------------------------ */
/* Map data structures                                                */
/* ------------------------------------------------------------------ */

struct sector_vertex {
	float x, y;
};

struct map_sector {
	unsigned sector_number;
	float floor_height, ceil_height;
	unsigned num_sides;
	struct sector_vertex sides_xy[MAX_SIDES]; /* CCW order */
	unsigned char color[MAX_SIDES];
	unsigned short destination_sector[MAX_SIDES]; /* SECTOR_NONE = wall */
};

/* ------------------------------------------------------------------ */
/* Draw group: a sub-range of a mesh sharing one texture              */
/* ------------------------------------------------------------------ */

struct draw_group {
	int tex_index; /* index into world textures */
	int first;     /* first vertex */
	int count;     /* vertex count */
};

struct sector_render {
	lud_mesh_t mesh;
	struct draw_group groups[MAX_DRAW_GROUPS];
	int num_groups;
};

/* ------------------------------------------------------------------ */
/* World                                                              */
/* ------------------------------------------------------------------ */

struct texture_set {
	lud_texture_t diffuse;
	lud_texture_t normal;
	lud_texture_t roughness;
	lud_texture_t ao;
	lud_texture_t height;
};

struct world {
	const struct map_sector *sectors[MAX_SECTORS];
	struct sector_render render[MAX_SECTORS];
	unsigned num_sectors;
	struct texture_set textures[MAX_TEXTURES];
	unsigned num_textures;
};

/* ------------------------------------------------------------------ */
/* Game state                                                         */
/* ------------------------------------------------------------------ */

struct game_state {
	float player_x, player_y; /* sector coords */
	float player_z;           /* fly offset */
	float player_facing;      /* yaw, degrees */
	float player_height;      /* eye height above floor */
	float player_tilt;        /* pitch, degrees */
	unsigned player_sector;

	float hfov;               /* horizontal FOV, degrees */
	bool use_textures; /* runtime toggle */

	float time;               /* accumulated time for torch flicker */

	struct {
		bool up, down, left, right;
		bool turn_left, turn_right;
		bool fly_up, fly_down;
		bool look_up, look_down;
	} act;
};

/* ------------------------------------------------------------------ */
/* Globals                                                            */
/* ------------------------------------------------------------------ */

static struct game_state state;
static struct world world;
static lud_shader_t shader_textured;
static lud_shader_t shader_colored;
static lud_font_t font;

/* ------------------------------------------------------------------ */
/* Shaders                                                            */
/* ------------------------------------------------------------------ */

/* ---- Built-in shaders (generated from shaders/) ---------------------- */

extern const char portal_vert[];
extern const char portal_textured_frag[];
extern const char portal_colored_frag[];

/* ------------------------------------------------------------------ */
/* Hardcoded test map (matches the original two-sector demo)          */
/* ------------------------------------------------------------------ */

static const struct map_sector test_sectors[] = {
	{
		.sector_number = 0,
		.floor_height = 0.0f,
		.ceil_height = 2.0f,
		.num_sides = 4,
		.sides_xy = {
			{ 1, 16 },
			{ 5, 11 },
			{ 5,  7 },
			{ 1,  2 },
		},
		.destination_sector = {
			SECTOR_NONE, SECTOR_NONE, SECTOR_NONE, 1,
		},
		.color = { 0, 1, 2, 3 },
	},
	{
		.sector_number = 1,
		.floor_height = 0.0f,
		.ceil_height = 2.0f,
		.num_sides = 4,
		.sides_xy = {
			{  8, 6 },
			{ 10, 1 },
			{  1, 2 },
			{  5, 7 },
		},
		.destination_sector = {
			SECTOR_NONE, SECTOR_NONE, SECTOR_NONE, 0,
		},
		.color = { 4, 5, 6, 7 },
	},
};

static const struct map_sector *
sector_get(unsigned num)
{
	if (num < ARRAY_SIZE(test_sectors))
		return &test_sectors[num];
	return NULL;
}

static void
sector_find_center(const struct map_sector *sec, float *cx, float *cy)
{
	float tx = 0.0f, ty = 0.0f;
	unsigned i;
	for (i = 0; i < sec->num_sides; i++) {
		tx += sec->sides_xy[i].x;
		ty += sec->sides_xy[i].y;
	}
	if (cx) *cx = tx / sec->num_sides;
	if (cy) *cy = ty / sec->num_sides;
}

/* ------------------------------------------------------------------ */
/* Texture loading                                                    */
/* ------------------------------------------------------------------ */

static struct texture_set
load_texture_set(const char *color_path, const char *normal_path,
                 const char *roughness_path, const char *ao_path,
                 const char *height_path)
{
	struct texture_set ts;
	ts.diffuse   = lud_load_texture_srgb(color_path, LUD_FILTER_LINEAR, LUD_FILTER_LINEAR);
	ts.normal    = lud_load_texture(normal_path, LUD_FILTER_LINEAR, LUD_FILTER_LINEAR);
	ts.roughness = lud_load_texture(roughness_path, LUD_FILTER_LINEAR, LUD_FILTER_LINEAR);
	ts.ao        = lud_load_texture(ao_path, LUD_FILTER_LINEAR, LUD_FILTER_LINEAR);
	ts.height    = lud_load_texture(height_path, LUD_FILTER_LINEAR, LUD_FILTER_LINEAR);
	return ts;
}

static void
create_textures(void)
{
	/* 0: floor - ground dirt */
	world.textures[0] = load_texture_set(
		"assets/textures/ground_color.jpg",
		"assets/textures/ground_normal.jpg",
		"assets/textures/ground_roughness.jpg",
		"assets/textures/ground_ao.jpg",
		"assets/textures/ground_height.jpg");
	/* 1: ceiling - dark rock */
	world.textures[1] = load_texture_set(
		"assets/textures/rock_dark_color.jpg",
		"assets/textures/rock_dark_normal.jpg",
		"assets/textures/rock_dark_roughness.jpg",
		"assets/textures/rock_dark_ao.jpg",
		"assets/textures/rock_dark_height.jpg");
	/* 2: walls - brick */
	world.textures[2] = load_texture_set(
		"assets/textures/brick_color.jpg",
		"assets/textures/brick_normal.jpg",
		"assets/textures/brick_roughness.jpg",
		"assets/textures/brick_ao.jpg",
		"assets/textures/brick_height.jpg");
	/* 3: walls - rocky */
	world.textures[3] = load_texture_set(
		"assets/textures/rock_color.jpg",
		"assets/textures/rock_normal.jpg",
		"assets/textures/rock_roughness.jpg",
		"assets/textures/rock_ao.jpg",
		"assets/textures/rock_height.jpg");
	world.num_textures = 4;
}

/* ------------------------------------------------------------------ */
/* Sector mesh building                                               */
/* ------------------------------------------------------------------ */

/*
 * Build all geometry for one sector into a single mesh.
 * Vertices are grouped by texture so we can draw each group with
 * the correct texture bound.
 *
 * Order: floor (tex 0), ceiling (tex 1), then each solid wall.
 */
static void
build_sector_mesh(struct sector_render *sr, const struct map_sector *sec)
{
	/* worst case vertex count */
	int max_verts = 2 * (sec->num_sides - 2) * 3 + sec->num_sides * 6;
	struct vertex *verts = calloc(max_verts, sizeof(*verts));
	int nv = 0;
	int ng = 0;
	unsigned i;

	float fh = sec->floor_height;
	float ch = sec->ceil_height;

	/* --- floor (triangle fan -> triangles) --- */
	if (sec->num_sides > 2) {
		int first = nv;
		const struct sector_vertex *v0 = &sec->sides_xy[0];
		/* CCW winding, normal up, tangent +X */
		for (i = 1; i + 1 < sec->num_sides; i++) {
			const struct sector_vertex *v1 = &sec->sides_xy[i];
			const struct sector_vertex *v2 = &sec->sides_xy[i + 1];
			verts[nv++] = (struct vertex){
				v0->x, fh, v0->y, v0->x, v0->y,
				0,1,0, 1,0,0, 0.4f, 0.4f, 0.4f
			};
			verts[nv++] = (struct vertex){
				v1->x, fh, v1->y, v1->x, v1->y,
				0,1,0, 1,0,0, 0.4f, 0.4f, 0.4f
			};
			verts[nv++] = (struct vertex){
				v2->x, fh, v2->y, v2->x, v2->y,
				0,1,0, 1,0,0, 0.4f, 0.4f, 0.4f
			};
		}
		sr->groups[ng++] = (struct draw_group){
			.tex_index = 0, .first = first, .count = nv - first
		};
	}

	/* --- ceiling (reverse winding for downward-facing normal) --- */
	if (sec->num_sides > 2) {
		int first = nv;
		const struct sector_vertex *v0 = &sec->sides_xy[0];
		/* normal down, tangent +X */
		for (i = 1; i + 1 < sec->num_sides; i++) {
			const struct sector_vertex *v1 = &sec->sides_xy[i];
			const struct sector_vertex *v2 = &sec->sides_xy[i + 1];
			verts[nv++] = (struct vertex){
				v0->x, ch, v0->y, v0->x, v0->y,
				0,-1,0, 1,0,0, 0.3f, 0.3f, 0.4f
			};
			verts[nv++] = (struct vertex){
				v2->x, ch, v2->y, v2->x, v2->y,
				0,-1,0, 1,0,0, 0.3f, 0.3f, 0.4f
			};
			verts[nv++] = (struct vertex){
				v1->x, ch, v1->y, v1->x, v1->y,
				0,-1,0, 1,0,0, 0.3f, 0.3f, 0.4f
			};
		}
		sr->groups[ng++] = (struct draw_group){
			.tex_index = 1, .first = first, .count = nv - first
		};
	}

	/* --- walls (quads as two triangles each) --- */
	const struct sector_vertex *last = &sec->sides_xy[sec->num_sides - 1];
	for (i = 0; i < sec->num_sides; i++) {
		const struct sector_vertex *cur = &sec->sides_xy[i];
		if (sec->destination_sector[i] == SECTOR_NONE) {
			int first = nv;
			/* wall direction and length for texture U coordinate */
			float dx = last->x - cur->x;
			float dz = last->y - cur->y;
			float length = sqrtf(dx * dx + dz * dz);
			float inv_len = (length > 0.0001f) ? 1.0f / length : 0.0f;

			/* tangent along wall direction (U axis) */
			float wall_tx = dx * inv_len;
			float wall_tz = dz * inv_len;

			/* inward-facing normal (perpendicular to wall, pointing into sector) */
			float wall_nx = -wall_tz;
			float wall_nz =  wall_tx;

			/* wall color from the color table */
			unsigned ci = sec->color[i] % ARRAY_SIZE(wall_colors);
			float cr = wall_colors[ci][0];
			float cg = wall_colors[ci][1];
			float cb = wall_colors[ci][2];

			/* texture index for this wall */
			int ti = 2 + (i % (world.num_textures - 2));

			float wall_h = ch - fh;

			/* tri 1: last_top, cur_top, last_bot */
			verts[nv++] = (struct vertex){
				last->x, ch, last->y, length, wall_h,
				wall_nx, 0, wall_nz, wall_tx, 0, wall_tz,
				cr, cg, cb
			};
			verts[nv++] = (struct vertex){
				cur->x, ch, cur->y, 0.0f, wall_h,
				wall_nx, 0, wall_nz, wall_tx, 0, wall_tz,
				cr, cg, cb
			};
			verts[nv++] = (struct vertex){
				last->x, fh, last->y, length, 0.0f,
				wall_nx, 0, wall_nz, wall_tx, 0, wall_tz,
				cr, cg, cb
			};
			/* tri 2: cur_top, cur_bot, last_bot */
			verts[nv++] = (struct vertex){
				cur->x, ch, cur->y, 0.0f, wall_h,
				wall_nx, 0, wall_nz, wall_tx, 0, wall_tz,
				cr, cg, cb
			};
			verts[nv++] = (struct vertex){
				cur->x, fh, cur->y, 0.0f, 0.0f,
				wall_nx, 0, wall_nz, wall_tx, 0, wall_tz,
				cr, cg, cb
			};
			verts[nv++] = (struct vertex){
				last->x, fh, last->y, length, 0.0f,
				wall_nx, 0, wall_nz, wall_tx, 0, wall_tz,
				cr, cg, cb
			};

			sr->groups[ng++] = (struct draw_group){
				.tex_index = ti, .first = first, .count = nv - first
			};
		}
		last = cur;
	}

	sr->num_groups = ng;
	sr->mesh = lud_make_mesh(&(lud_mesh_desc_t){
		.vertices = verts,
		.vertex_count = nv,
		.vertex_stride = sizeof(struct vertex),
		.layout = {
			{ .size = 3, .offset = offsetof(struct vertex, x) },
			{ .size = 2, .offset = offsetof(struct vertex, u) },
			{ .size = 3, .offset = offsetof(struct vertex, nx) },
			{ .size = 3, .offset = offsetof(struct vertex, tx) },
			{ .size = 3, .offset = offsetof(struct vertex, r) },
		},
		.num_attrs = 5,
		.usage = LUD_USAGE_STATIC,
		.primitive = LUD_PRIM_TRIANGLES,
	});

	free(verts);
}

/* ------------------------------------------------------------------ */
/* Rendering                                                          */
/* ------------------------------------------------------------------ */

/* Track which sectors have been drawn this frame to avoid infinite loops */
static bool sector_drawn[MAX_SECTORS];

static void
draw_sector_recursive(unsigned sector_num, int ttl)
{
	if (ttl < 0)
		return;
	if (sector_num >= world.num_sectors)
		return;
	if (sector_drawn[sector_num])
		return;

	sector_drawn[sector_num] = true;

	const struct map_sector *sec = world.sectors[sector_num];
	struct sector_render *sr = &world.render[sector_num];
	int i;

	/* draw this sector's geometry */
	for (i = 0; i < sr->num_groups; i++) {
		struct draw_group *g = &sr->groups[i];
		if (state.use_textures) {
			struct texture_set *t = &world.textures[g->tex_index];
			lud_bind_texture(t->diffuse, 0);
			lud_bind_texture(t->normal, 1);
			lud_bind_texture(t->roughness, 2);
			lud_bind_texture(t->ao, 3);
			lud_bind_texture(t->height, 4);
		}
		lud_draw_range(sr->mesh, g->first, g->count);
	}

	/* recurse into portals */
	if (ttl > 0) {
		unsigned j;
		for (j = 0; j < sec->num_sides; j++) {
			unsigned short dest = sec->destination_sector[j];
			if (dest != SECTOR_NONE)
				draw_sector_recursive(dest, ttl - 1);
		}
	}
}

/* ------------------------------------------------------------------ */
/* Camera / projection                                                */
/* ------------------------------------------------------------------ */

static hmm_mat4
build_view_matrix(void)
{
	/*
	 * Matches the original:
	 *   glRotatef(tilt, -1, 0, 0)
	 *   glRotatef(facing, 0, 1, 0)
	 *   glTranslatef(-px, -height - pz, -py)
	 */
	hmm_mat4 t = HMM_Translate(HMM_Vec3(
		-state.player_x,
		-state.player_height - state.player_z,
		-state.player_y
	));
	hmm_mat4 ry = HMM_Rotate(state.player_facing, HMM_Vec3(0, 1, 0));
	hmm_mat4 rx = HMM_Rotate(state.player_tilt, HMM_Vec3(-1, 0, 0));

	return HMM_MultiplyMat4(rx, HMM_MultiplyMat4(ry, t));
}

static hmm_mat4
build_projection_matrix(void)
{
	float w = (float)lud_width();
	float h = (float)lud_height();
	if (h < 1.0f) h = 1.0f;
	float aspect = w / h;
	/*
	 * Fix horizontal FOV at 90° and derive vertical FOV from aspect ratio.
	 * This keeps ultra-wide (21:9, 32:9) from getting a fisheye effect,
	 * and narrow screens (3:2, 4:3) from losing vertical space.
	 *
	 * hfov_rad = horizontal FOV in radians
	 * vfov = 2 * atan(tan(hfov/2) / aspect)
	 */
	float hfov_rad = state.hfov * (float)M_PI / 180.0f;
	float vfov_deg = 2.0f * atanf(tanf(hfov_rad * 0.5f) / aspect)
	               * 180.0f / (float)M_PI;
	return HMM_Perspective(vfov_deg, aspect, 0.125f, 1000.0f);
}

/* ------------------------------------------------------------------ */
/* Player movement                                                    */
/* ------------------------------------------------------------------ */

static void
update_player(float dt)
{
	float speed = 5.0f;      /* units per second */
	float turn_speed = 120.0f; /* degrees per second */

	float theta = state.player_facing * (float)M_PI / 180.0f;
	float move = speed * dt;

	/* forward / backward (along facing direction) */
	if (state.act.up) {
		state.player_x -= move * cosf(theta + (float)M_PI / 2.0f);
		state.player_y -= move * sinf(theta + (float)M_PI / 2.0f);
	}
	if (state.act.down) {
		state.player_x += move * cosf(theta + (float)M_PI / 2.0f);
		state.player_y += move * sinf(theta + (float)M_PI / 2.0f);
	}

	/* strafe left / right */
	if (state.act.left) {
		state.player_x -= move * cosf(theta);
		state.player_y -= move * sinf(theta);
	}
	if (state.act.right) {
		state.player_x += move * cosf(theta);
		state.player_y += move * sinf(theta);
	}

	/* turning */
	if (state.act.turn_left)
		state.player_facing -= turn_speed * dt;
	if (state.act.turn_right)
		state.player_facing += turn_speed * dt;

	/* fly up/down */
	if (state.act.fly_up)
		state.player_z += speed * dt;
	if (state.act.fly_down)
		state.player_z -= speed * dt;

	/* look up/down */
	if (state.act.look_up)
		state.player_tilt += turn_speed * dt;
	if (state.act.look_down)
		state.player_tilt -= turn_speed * dt;

	/* wrap facing to [0, 360) */
	state.player_facing = fmodf(state.player_facing, 360.0f);
	if (state.player_facing < 0.0f)
		state.player_facing += 360.0f;

	/* clamp pitch */
	if (state.player_tilt > 89.0f) state.player_tilt = 89.0f;
	if (state.player_tilt < -89.0f) state.player_tilt = -89.0f;
}

/* ------------------------------------------------------------------ */
/* ludica callbacks                                                   */
/* ------------------------------------------------------------------ */

static void
init(void)
{
	unsigned i;

	/* create shaders */
	shader_textured = lud_make_shader(&(lud_shader_desc_t){
		.vert_src = portal_vert,
		.frag_src = portal_textured_frag,
		.attrs = { "a_position", "a_texcoord", "a_normal",
		           "a_tangent", "a_color" },
		.num_attrs = 5,
	});
	shader_colored = lud_make_shader(&(lud_shader_desc_t){
		.vert_src = portal_vert,
		.frag_src = portal_colored_frag,
		.attrs = { "a_position", "a_texcoord", "a_normal",
		           "a_tangent", "a_color" },
		.num_attrs = 5,
	});

	font = lud_make_default_font();

	/* load textures */
	create_textures();

	/* load map */
	world.num_sectors = ARRAY_SIZE(test_sectors);
	for (i = 0; i < world.num_sectors; i++) {
		world.sectors[i] = &test_sectors[i];
		build_sector_mesh(&world.render[i], world.sectors[i]);
	}

	/* init player: center of sector 1, facing 180 degrees */
	state.player_sector = 1;
	sector_find_center(sector_get(state.player_sector),
	                   &state.player_x, &state.player_y);
	state.player_facing = 180.0f;
	state.player_height = 1.0f;
	state.hfov = DEFAULT_HFOV;
	state.use_textures = true;
}

static int
on_event(const lud_event_t *ev)
{
	if (ev->type == LUD_EV_KEY_DOWN || ev->type == LUD_EV_KEY_UP) {
		bool down = (ev->type == LUD_EV_KEY_DOWN);
		switch (ev->key.keycode) {
		/* WASD + arrows */
		case LUD_KEY_W:
		case LUD_KEY_UP:
			state.act.up = down;
			return 1;
		case LUD_KEY_S:
		case LUD_KEY_DOWN:
			state.act.down = down;
			return 1;
		case LUD_KEY_Q:
			state.act.left = down;
			return 1;
		case LUD_KEY_E:
			state.act.right = down;
			return 1;
		case LUD_KEY_A:
		case LUD_KEY_LEFT:
			state.act.turn_left = down;
			return 1;
		case LUD_KEY_D:
		case LUD_KEY_RIGHT:
			state.act.turn_right = down;
			return 1;
		/* fly */
		case LUD_KEY_HOME:
			state.act.fly_up = down;
			return 1;
		case LUD_KEY_END:
			state.act.fly_down = down;
			return 1;
		/* look */
		case LUD_KEY_PAGE_UP:
			state.act.look_up = down;
			return 1;
		case LUD_KEY_PAGE_DOWN:
			state.act.look_down = down;
			return 1;
		/* toggle texture mode */
		case LUD_KEY_T:
			if (down)
				state.use_textures = !state.use_textures;
			return 1;
		/* FOV adjustment */
		case LUD_KEY_EQUAL: /* +/= key */
			if (down && state.hfov < MAX_HFOV)
				state.hfov += HFOV_STEP;
			return 1;
		case LUD_KEY_MINUS:
			if (down && state.hfov > MIN_HFOV)
				state.hfov -= HFOV_STEP;
			return 1;
		case LUD_KEY_F11:
			if (down)
				lud_set_fullscreen(!lud_is_fullscreen());
			return 1;
		case LUD_KEY_ESCAPE:
			if (down) lud_quit();
			return 1;
		default:
			break;
		}
	}
	return 0;
}

static void
frame(float dt)
{
	update_player(dt);
	state.time += dt;

	int w = lud_width();
	int h = lud_height();
	lud_viewport(0, 0, w, h);
	lud_clear(0.0f, 0.0f, 0.0f, 1.0f);

	/* enable depth test and backface culling */
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);

	/* build MVP */
	hmm_mat4 proj = build_projection_matrix();
	hmm_mat4 view = build_view_matrix();
	hmm_mat4 mvp = HMM_MultiplyMat4(proj, view);

	/* draw world: pick shader based on render mode */
	lud_shader_t active_shader = state.use_textures
		? shader_textured : shader_colored;
	lud_apply_shader(active_shader);
	lud_uniform_mat4(active_shader, "u_mvp", (const float *)mvp.Elements);
	if (state.use_textures) {
		lud_uniform_int(active_shader, "u_texture", 0);
		lud_uniform_int(active_shader, "u_normal_map", 1);
		lud_uniform_int(active_shader, "u_roughness_map", 2);
		lud_uniform_int(active_shader, "u_ao_map", 3);
		lud_uniform_int(active_shader, "u_height_map", 4);
		lud_uniform_float(active_shader, "u_height_scale", 0.04f);

		/* torch point light at player position */
		float eye_x = state.player_x;
		float eye_y = state.player_height + state.player_z;
		float eye_z = state.player_y;
		lud_uniform_vec3(active_shader, "u_light_pos",
			eye_x, eye_y, eye_z);

		/* torch flicker: layered sine waves for organic variation */
		float t = state.time;
		float flicker = 1.0f
			+ 0.08f * sinf(t * 7.3f)
			+ 0.05f * sinf(t * 13.1f)
			+ 0.03f * sinf(t * 23.7f);
		lud_uniform_vec3(active_shader, "u_light_color",
			2.8f * flicker, 1.8f * flicker, 0.9f * flicker);

		/* camera position in world space */
		lud_uniform_vec3(active_shader, "u_view_pos",
			eye_x, eye_y, eye_z);
	}

	memset(sector_drawn, 0, sizeof(sector_drawn));
	draw_sector_recursive(state.player_sector, PORTAL_DEPTH);

	/* restore state for 2D overlay */
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	/* HUD text */
	float vw = 640.0f, vh = 360.0f;
	lud_sprite_begin(0, 0, vw, vh);

	char buf[128];
	snprintf(buf, sizeof(buf), "pos: %.1f, %.1f  facing: %.0f  hfov: %.0f  [T]extures: %s",
	         state.player_x, state.player_y, state.player_facing,
	         state.hfov, state.use_textures ? "on" : "off");
	lud_draw_text(font, 4, 4, 1, buf);

	snprintf(buf, sizeof(buf), "WASD/arrows: move  Q/E: strafe  PgUp/Dn: look  Home/End: fly  +/-: fov");
	lud_draw_text(font, 4, vh - 12, 1, buf);

	lud_sprite_end();
}

static void
cleanup(void)
{
	unsigned i;
	for (i = 0; i < world.num_sectors; i++)
		lud_destroy_mesh(world.render[i].mesh);
	for (i = 0; i < world.num_textures; i++) {
		lud_destroy_texture(world.textures[i].diffuse);
		lud_destroy_texture(world.textures[i].normal);
	}
	lud_destroy_shader(shader_textured);
	lud_destroy_shader(shader_colored);
	lud_destroy_font(font);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                        */
/* ------------------------------------------------------------------ */

int
main(void)
{
	return lud_run(&(lud_desc_t){
		.app_name = "Hero",
		.width = 960,
		.height = 540,
		.resizable = 1,
		.gles_version = 3,
		.init = init,
		.frame = frame,
		.event = on_event,
		.cleanup = cleanup,
	});
}
