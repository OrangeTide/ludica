/* instanced_test.c : verify lud_draw_instanced draws N instances (GLES3) */
/* Made by a machine. PUBLIC DOMAIN (CC0-1.0) */

#include "ludica.h"
#include "ludica_gfx.h"
#include <GLES2/gl2.h>
#include <stdio.h>

static int checks;
static int failures;

static void
expect(const char *what, int got, int want)
{
	checks++;
	if (got == want) {
		printf("ok   %s\n", what);
	} else {
		failures++;
		printf("FAIL %s: got %d want %d\n", what, got, want);
	}
}

/* Each instance draws a vertical band, shifted horizontally by its
 * gl_InstanceID, so 4 instances paint 4 separate columns. */
static const char *VERT =
	"#version 300 es\n"
	"in vec2 pos;\n"
	"void main(){\n"
	"  float dx = -0.75 + float(gl_InstanceID) * 0.5;\n"
	"  gl_Position = vec4(pos.x + dx, pos.y, 0.0, 1.0);\n"
	"}\n";

static const char *FRAG =
	"#version 300 es\n"
	"precision mediump float;\n"
	"out vec4 frag;\n"
	"void main(){ frag = vec4(1.0, 0.0, 0.0, 1.0); }\n";

/* A solid band, x in [-0.15, 0.15], y in [-0.8, 0.8], as two triangles. */
static const float BAND[12] = {
	-0.15f, -0.8f,   0.15f, -0.8f,   0.15f, 0.8f,
	-0.15f, -0.8f,   0.15f,  0.8f,  -0.15f, 0.8f,
};

static int
is_red(int x, int y)
{
	unsigned char px[4] = {0, 0, 0, 0};
	lud_read_pixels(x, y, 1, 1, px);
	return px[0] == 255 && px[1] == 0 && px[2] == 0;
}

static void
init(void)
{
	lud_shader_t shd = lud_make_shader(&(lud_shader_desc_t){
		.vert_src = VERT, .frag_src = FRAG,
		.attrs = { "pos" }, .num_attrs = 1,
	});
	lud_mesh_t m = lud_make_mesh(&(lud_mesh_desc_t){
		.vertices = BAND, .vertex_count = 6,
		.vertex_stride = 2 * (int)sizeof(float),
		.layout = { { .size = 2, .offset = 0 } }, .num_attrs = 1,
		.usage = LUD_USAGE_STATIC, .primitive = LUD_PRIM_TRIANGLES,
	});
	expect("shader created", shd.id != 0, 1);
	expect("mesh created", m.id != 0, 1);

	lud_clear(0.0f, 0.0f, 0.0f, 1.0f);
	lud_apply_shader(shd);
	lud_draw_instanced(m, 4);

	/* Instance i centers at screen x = (dx+1)*32 = 8, 24, 40, 56. */
	expect("instance 0 column drawn", is_red(8, 32), 1);
	expect("instance 1 column drawn", is_red(24, 32), 1);
	expect("instance 2 column drawn", is_red(40, 32), 1);
	expect("instance 3 column drawn", is_red(56, 32), 1);

	/* The gap between instance 0 and 1 must be empty: proves the 4 are
	 * discrete instances, not one over-wide fill. */
	expect("gap between instances is empty", is_red(16, 32), 0);

	/* A zero instance count is a no-op. */
	lud_clear(0.0f, 0.0f, 0.0f, 1.0f);
	lud_draw_instanced(m, 0);
	expect("zero instances draws nothing", is_red(8, 32), 0);

	expect("no GL error", (int)glGetError(), GL_NO_ERROR);

	lud_destroy_mesh(m);
	lud_destroy_shader(shd);
	printf("\n%d checks, %d failures\n", checks, failures);
	lud_quit();
}

int
main(int argc, char **argv)
{
	lud_run(&(lud_desc_t){
		.app_name = "instanced_test",
		.width = 64, .height = 64,
		.gles_version = 3,
		.init = init,
		.argc = argc, .argv = argv,
	});
	return failures ? 1 : 0;
}
