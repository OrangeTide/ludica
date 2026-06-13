/*
 * meshupdate_test - verify lud_update_mesh / lud_update_mesh_indices
 * actually rewrite the GPU buffers, including the growth (reallocate)
 * path and promoting a non-indexed mesh to indexed.
 *
 * Strategy: draw a flat-colored triangle in clip space and sample the
 * center pixel. A full-screen triangle covers the center (pixel = red);
 * a tiny corner triangle does not (pixel = clear color). Mutating the
 * mesh between draws and observing the center pixel proves the update
 * reached the GPU.
 *
 * Needs a display (X11 DISPLAY); run under xvfb on a headless box.
 *
 * Made by a machine. PUBLIC DOMAIN (CC0-1.0)
 */
#include "ludica.h"
#include "ludica_gfx.h"
#include <GLES2/gl2.h>
#include <stdio.h>

static int checks;
static int failures;

static void
expect_int(const char *what, GLint got, GLint want)
{
	checks++;
	if (got == want) {
		printf("ok   %s = %d\n", what, (int)got);
	} else {
		failures++;
		printf("FAIL %s: got %d want %d\n", what, (int)got, (int)want);
	}
}

static const char *VERT =
	"#version 100\n"
	"attribute vec2 pos;\n"
	"void main(){ gl_Position = vec4(pos, 0.0, 1.0); }\n";

static const char *FRAG =
	"#version 100\n"
	"precision mediump float;\n"
	"void main(){ gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";

/* Clip-space triangle that covers the whole viewport (and the center). */
static const float FULL[6]   = { -1.0f, -1.0f,  3.0f, -1.0f, -1.0f, 3.0f };
/* Tiny triangle hugging the bottom-left corner, away from the center. */
static const float CORNER[6] = { -1.0f, -1.0f, -0.9f, -1.0f, -1.0f, -0.9f };

/* Draw the mesh, then report whether the center pixel came out red. */
static int
center_is_red(lud_shader_t shd, lud_mesh_t mesh)
{
	unsigned char px[4] = {0, 0, 0, 0};
	int cx = 32, cy = 32;  /* center of a 64x64 window */

	lud_clear(0.0f, 0.0f, 0.0f, 1.0f);  /* opaque black */
	lud_apply_shader(shd);
	lud_draw(mesh);
	lud_read_pixels(cx, cy, 1, 1, px);
	return px[0] == 255 && px[1] == 0 && px[2] == 0;
}

static void
init(void)
{
	lud_shader_t shd = lud_make_shader(&(lud_shader_desc_t){
		.vert_src = VERT, .frag_src = FRAG,
		.attrs = { "pos" }, .num_attrs = 1,
	});
	expect_int("shader created", shd.id != 0, 1);

	/* A DYNAMIC, non-indexed triangle, initially the corner triangle. */
	lud_mesh_t m = lud_make_mesh(&(lud_mesh_desc_t){
		.vertices = CORNER,
		.vertex_count = 3,
		.vertex_stride = 2 * (int)sizeof(float),
		.layout = { { .size = 2, .offset = 0 } },
		.num_attrs = 1,
		.usage = LUD_USAGE_DYNAMIC,
		.primitive = LUD_PRIM_TRIANGLES,
	});
	expect_int("mesh created", m.id != 0, 1);

	/* Baseline: corner triangle does not cover the center. */
	expect_int("corner triangle leaves center black", center_is_red(shd, m), 0);

	/* In-place sub-update (fits in capacity): swap in the full-screen
	 * triangle. Center must turn red. */
	lud_update_mesh(m, 0, 3, FULL);
	expect_int("update to full triangle covers center", center_is_red(shd, m), 1);

	/* Update back to the corner triangle to confirm the change is real,
	 * not a one-way latch. */
	lud_update_mesh(m, 0, 3, CORNER);
	expect_int("update back to corner clears center", center_is_red(shd, m), 0);

	/* Growth path: write 6 vertices into a 3-vertex buffer (forces a
	 * reallocate). Verts 0-2 stay in the corner, 3-5 are a second
	 * full-screen triangle. lud_draw's count must grow to 6 and the
	 * center must be covered. */
	{
		float two[12];
		int i;
		for (i = 0; i < 6; i++) two[i] = CORNER[i];
		for (i = 0; i < 6; i++) two[6 + i] = FULL[i];
		lud_update_mesh(m, 0, 6, two);
	}
	expect_int("grown mesh draws 6 verts, covers center", center_is_red(shd, m), 1);

	/* Index path: a fresh non-indexed mesh holding the full-screen
	 * triangle, then promote it to indexed via lud_update_mesh_indices
	 * (grows the index buffer from zero and creates the IBO). */
	{
		unsigned short idx[3] = { 0, 1, 2 };
		lud_mesh_t mi = lud_make_mesh(&(lud_mesh_desc_t){
			.vertices = FULL,
			.vertex_count = 3,
			.vertex_stride = 2 * (int)sizeof(float),
			.layout = { { .size = 2, .offset = 0 } },
			.num_attrs = 1,
			.usage = LUD_USAGE_DYNAMIC,
			.primitive = LUD_PRIM_TRIANGLES,
		});
		lud_update_mesh_indices(mi, 0, 3, idx);
		expect_int("promoted-to-indexed mesh covers center",
		           center_is_red(shd, mi), 1);
		lud_destroy_mesh(mi);
	}

	expect_int("no GL error", (GLint)glGetError(), GL_NO_ERROR);

	lud_destroy_mesh(m);
	lud_destroy_shader(shd);

	printf("\n%d checks, %d failures\n", checks, failures);
	lud_quit();
}

int
main(int argc, char **argv)
{
	lud_run(&(lud_desc_t){
		.app_name = "meshupdate_test",
		.width = 64, .height = 64,
		.gles_version = 3,
		.init = init,
		.argc = argc, .argv = argv,
	});
	return failures ? 1 : 0;
}
