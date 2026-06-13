/*
 * demo08_picking - 3D object picking via an offscreen color-id buffer
 *
 * The scene is a row of cubes. Each frame the cubes are drawn twice:
 *   1. into an offscreen render target, each cube flat-shaded with a
 *      color that encodes its object id (color-id pass);
 *   2. onto the screen with display colors, the picked cube highlighted.
 * The id under the crosshair is read back from the target with
 * lud_read_pixels, which is the whole point of the render-target +
 * readback feature.
 *
 * Drive the crosshair with the arrow keys (or the mouse) and pick with
 * space (or left click). The crosshair position and the hovered/picked
 * ids are registered for automation, and the same moves are exposed as
 * named actions, so the picking loop is fully scriptable:
 *   action cursor_left ; ... ; action pick ; query var picked_id
 *
 * Run --selftest to point the crosshair at the center cube, read the
 * id back, assert it, and exit (used by 'make run-tests').
 *
 * Made by a machine. PUBLIC DOMAIN (CC0-1.0)
 */
#include "ludica.h"
#include "ludica_gfx.h"
#include "ludica_input.h"
#include "ludica_font.h"
#include "ludica_auto.h"
#include <stdio.h>
#include <string.h>

#define HANDMADE_MATH_IMPLEMENTATION
#include "HandmadeMath.h"

#define WIN_W 800
#define WIN_H 600
#define NUM_CUBES 5

/* Cube centered at the origin, unit size. */
static const float cube_verts[] = {
	-0.5f, -0.5f, -0.5f,
	 0.5f, -0.5f, -0.5f,
	 0.5f,  0.5f, -0.5f,
	-0.5f,  0.5f, -0.5f,
	-0.5f, -0.5f,  0.5f,
	 0.5f, -0.5f,  0.5f,
	 0.5f,  0.5f,  0.5f,
	-0.5f,  0.5f,  0.5f,
};
static const unsigned short cube_idx[] = {
	0, 1, 2,  0, 2, 3,   /* back */
	4, 6, 5,  4, 7, 6,   /* front */
	0, 4, 5,  0, 5, 1,   /* bottom */
	3, 2, 6,  3, 6, 7,   /* top */
	0, 3, 7,  0, 7, 4,   /* left */
	1, 5, 6,  1, 6, 2,   /* right */
};

/* Per-cube base display colors. */
static const float cube_color[NUM_CUBES][3] = {
	{ 0.85f, 0.25f, 0.25f },
	{ 0.25f, 0.75f, 0.35f },
	{ 0.30f, 0.45f, 0.90f },
	{ 0.90f, 0.75f, 0.20f },
	{ 0.70f, 0.35f, 0.80f },
};

static const char *vert_src =
	"attribute vec3 a_pos;\n"
	"uniform mat4 u_mvp;\n"
	"void main() { gl_Position = u_mvp * vec4(a_pos, 1.0); }\n";

static const char *frag_src =
	"precision mediump float;\n"
	"uniform vec4 u_color;\n"
	"void main() { gl_FragColor = u_color; }\n";

static lud_shader_t shader;
static lud_mesh_t cube;
static lud_target_t id_target;
static lud_font_t font;

static float cursor_fx = WIN_W / 2.0f;
static float cursor_fy = WIN_H / 2.0f;
static int cursor_x, cursor_y;       /* registered (int) */
static int hovered_id;               /* registered: id under crosshair, 0 = none */
static int picked_id;                /* registered: last picked id, 0 = none */
static float spin;                   /* shared cube rotation */

static lud_action_t act_left, act_right, act_up, act_down, act_pick;

static int last_mx = -1, last_my = -1;
static int mouse_was_down;
static int selftest;
static int selftest_failures;
static int selftest_frames;

static hmm_mat4
view_proj(void)
{
	hmm_mat4 proj = HMM_Perspective(55.0f, (float)WIN_W / (float)WIN_H,
	                                0.1f, 100.0f);
	hmm_mat4 view = HMM_LookAt(HMM_Vec3(0.0f, 2.5f, 9.0f),
	                           HMM_Vec3(0.0f, 0.0f, 0.0f),
	                           HMM_Vec3(0.0f, 1.0f, 0.0f));
	return HMM_MultiplyMat4(proj, view);
}

static hmm_mat4
cube_mvp(const hmm_mat4 *vp, int i)
{
	float x = (float)(i - NUM_CUBES / 2) * 2.2f;
	hmm_mat4 t = HMM_Translate(HMM_Vec3(x, 0.0f, 0.0f));
	hmm_mat4 r = HMM_Rotate(spin + (float)i * 15.0f, HMM_Vec3(0.3f, 1.0f, 0.0f));
	hmm_mat4 model = HMM_MultiplyMat4(t, r);
	return HMM_MultiplyMat4(*vp, model);
}

/* Render the color-id pass into the offscreen target and read the id
 * sitting under the crosshair. */
static void
update_hover(const hmm_mat4 *vp)
{
	unsigned char px[4] = { 0, 0, 0, 0 };
	int i;

	lud_bind_render_target(id_target);
	lud_depth_test(1);
	lud_depth_func(LUD_DEPTH_LESS);
	lud_cull(LUD_CULL_NONE);
	lud_blend(LUD_BLEND_NONE);
	lud_clear(0.0f, 0.0f, 0.0f, 1.0f);   /* id 0 = nothing */

	lud_apply_shader(shader);
	for (i = 0; i < NUM_CUBES; i++) {
		hmm_mat4 mvp = cube_mvp(vp, i);
		int id = i + 1;
		lud_uniform_mat4(shader, "u_mvp", (const float *)mvp.Elements);
		lud_uniform_vec4(shader, "u_color", (float)id / 255.0f, 0.0f, 0.0f, 1.0f);
		lud_draw(cube);
	}

	lud_read_pixels(cursor_x, cursor_y, 1, 1, px);
	hovered_id = px[0];

	lud_bind_render_target((lud_target_t){0});
}

static void
init(void)
{
	selftest = lud_get_config("selftest") != NULL;

	shader = lud_make_shader(&(lud_shader_desc_t){
		.vert_src = vert_src,
		.frag_src = frag_src,
		.attrs = { "a_pos" },
		.num_attrs = 1,
	});
	cube = lud_make_mesh(&(lud_mesh_desc_t){
		.vertices = cube_verts,
		.vertex_count = 8,
		.vertex_stride = 3 * sizeof(float),
		.layout = { { .size = 3, .offset = 0 } },
		.num_attrs = 1,
		.indices = cube_idx,
		.index_count = sizeof(cube_idx) / sizeof(cube_idx[0]),
		.usage = LUD_USAGE_STATIC,
		.primitive = LUD_PRIM_TRIANGLES,
	});
	id_target = lud_make_render_target(&(lud_target_desc_t){
		.width = WIN_W, .height = WIN_H,
		.format = LUD_PIXFMT_RGBA8,
		.min_filter = LUD_FILTER_NEAREST,
		.mag_filter = LUD_FILTER_NEAREST,
		.depth = 1,
	});
	font = lud_make_default_font();

	act_left  = lud_make_action("cursor_left");
	act_right = lud_make_action("cursor_right");
	act_up    = lud_make_action("cursor_up");
	act_down  = lud_make_action("cursor_down");
	act_pick  = lud_make_action("pick");
	lud_bind_key(LUD_KEY_LEFT,  act_left);
	lud_bind_key(LUD_KEY_RIGHT, act_right);
	lud_bind_key(LUD_KEY_UP,    act_up);
	lud_bind_key(LUD_KEY_DOWN,  act_down);
	lud_bind_key(LUD_KEY_SPACE, act_pick);

	lud_auto_register_int("cursor_x", &cursor_x);
	lud_auto_register_int("cursor_y", &cursor_y);
	lud_auto_register_int("hovered_id", &hovered_id);
	lud_auto_register_int("picked_id", &picked_id);
}

static void
move_cursor(float dt)
{
	const float speed = 360.0f;   /* pixels per second */
	int mx, my, do_pick = 0;

	/* Mouse takes over the crosshair when it actually moves. Seed the
	 * baseline on the first read so the crosshair keeps its centered
	 * default instead of snapping to wherever the pointer happens to be. */
	lud_mouse_pos(&mx, &my);
	if (last_mx < 0) {
		last_mx = mx;
		last_my = my;
	} else if (mx != last_mx || my != last_my) {
		cursor_fx = (float)mx;
		cursor_fy = (float)my;
		last_mx = mx;
		last_my = my;
	}

	if (lud_action_down(act_left))  cursor_fx -= speed * dt;
	if (lud_action_down(act_right)) cursor_fx += speed * dt;
	if (lud_action_down(act_up))    cursor_fy -= speed * dt;
	if (lud_action_down(act_down))  cursor_fy += speed * dt;

	if (cursor_fx < 0) cursor_fx = 0;
	if (cursor_fy < 0) cursor_fy = 0;
	if (cursor_fx > WIN_W - 1) cursor_fx = WIN_W - 1;
	if (cursor_fy > WIN_H - 1) cursor_fy = WIN_H - 1;
	cursor_x = (int)cursor_fx;
	cursor_y = (int)cursor_fy;

	if (lud_action_pressed(act_pick))
		do_pick = 1;
	if (lud_mouse_button_down(LUD_MOUSE_LEFT)) {
		if (!mouse_was_down) do_pick = 1;
		mouse_was_down = 1;
	} else {
		mouse_was_down = 0;
	}
	if (do_pick)
		picked_id = hovered_id;
}

static void
frame(float dt)
{
	hmm_mat4 vp;
	int i;

	if (selftest) {
		/* Aim at the screen center, where the middle cube sits. */
		cursor_fx = WIN_W / 2.0f;
		cursor_fy = WIN_H / 2.0f;
		cursor_x = WIN_W / 2;
		cursor_y = WIN_H / 2;
	} else {
		spin += dt * 25.0f;
		move_cursor(dt);
	}

	vp = view_proj();

	/* Color-id pass + readback (offscreen). */
	update_hover(&vp);

	/* Beauty pass to the screen. */
	lud_viewport(0, 0, lud_width(), lud_height());
	lud_clear(0.08f, 0.09f, 0.12f, 1.0f);
	lud_depth_test(1);
	lud_depth_func(LUD_DEPTH_LESS);
	lud_cull(LUD_CULL_NONE);

	lud_apply_shader(shader);
	for (i = 0; i < NUM_CUBES; i++) {
		hmm_mat4 mvp = cube_mvp(&vp, i);
		int id = i + 1;
		float r = cube_color[i][0], g = cube_color[i][1], b = cube_color[i][2];
		if (id == picked_id) {
			r = g = b = 1.0f;            /* selected: white */
		} else if (id == hovered_id) {
			r = r * 0.4f + 0.6f;         /* hovered: brightened */
			g = g * 0.4f + 0.6f;
			b = b * 0.4f + 0.6f;
		}
		lud_uniform_mat4(shader, "u_mvp", (const float *)mvp.Elements);
		lud_uniform_vec4(shader, "u_color", r, g, b, 1.0f);
		lud_draw(cube);
	}

	/* 2D overlay: crosshair + readout. */
	lud_depth_test(0);
	lud_sprite_begin(0, 0, lud_width(), lud_height());
	lud_sprite_rect((float)cursor_x - 8, (float)cursor_y - 1, 17, 2,
	                1.0f, 1.0f, 1.0f, 0.9f);
	lud_sprite_rect((float)cursor_x - 1, (float)cursor_y - 8, 2, 17,
	                1.0f, 1.0f, 1.0f, 0.9f);
	{
		char buf[64];
		snprintf(buf, sizeof(buf), "hovered=%d  picked=%d", hovered_id, picked_id);
		lud_draw_text(font, 12, 12, 2.0f, buf);
	}
	lud_sprite_end();

	if (selftest) {
		selftest_frames++;
		/* Let one full frame render and read back, then check. */
		if (selftest_frames >= 2) {
			int expect = NUM_CUBES / 2 + 1;   /* center cube id */
			if (hovered_id != expect) {
				selftest_failures++;
				printf("FAIL center pick: hovered=%d want=%d\n",
				       hovered_id, expect);
			} else {
				printf("ok   center pick hovered=%d\n", hovered_id);
			}
			printf("\nselftest: %d failure(s)\n", selftest_failures);
			lud_quit();
		}
	}
}

static void
cleanup(void)
{
	lud_destroy_render_target(id_target);
	lud_destroy_mesh(cube);
	lud_destroy_shader(shader);
}

int
main(int argc, char **argv)
{
	lud_run(&(lud_desc_t){
		.app_name = "demo08_picking",
		.width = WIN_W, .height = WIN_H,
		.gles_version = 2,
		.init = init,
		.frame = frame,
		.cleanup = cleanup,
		.argc = argc, .argv = argv,
	});
	return selftest_failures ? 1 : 0;
}
