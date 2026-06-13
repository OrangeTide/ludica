/*
 * renderstate_test - verify the lud_* render-state wrappers map to the
 * correct GL state.
 *
 * Opens a tiny ludica window so a GL context is current, sets each
 * render state through the ludica wrapper, then reads the state back
 * with glIsEnabled/glGetIntegerv and asserts it matches. Prints a
 * summary and exits non-zero if any check fails.
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
		printf("ok   %s = 0x%04X\n", what, (unsigned)got);
	} else {
		failures++;
		printf("FAIL %s: got 0x%04X want 0x%04X\n",
		       what, (unsigned)got, (unsigned)want);
	}
}

static void
expect_enabled(const char *what, GLenum cap, int want)
{
	expect_int(what, glIsEnabled(cap) ? 1 : 0, want ? 1 : 0);
}

static void
init(void)
{
	GLint v;

	/* depth test on/off */
	lud_depth_test(1);
	expect_enabled("depth_test(1) -> GL_DEPTH_TEST", GL_DEPTH_TEST, 1);
	lud_depth_test(0);
	expect_enabled("depth_test(0) -> GL_DEPTH_TEST", GL_DEPTH_TEST, 0);

	/* depth comparison function */
	lud_depth_func(LUD_DEPTH_LESS);
	glGetIntegerv(GL_DEPTH_FUNC, &v);
	expect_int("depth_func(LESS)", v, GL_LESS);
	lud_depth_func(LUD_DEPTH_LEQUAL);
	glGetIntegerv(GL_DEPTH_FUNC, &v);
	expect_int("depth_func(LEQUAL)", v, GL_LEQUAL);
	lud_depth_func(LUD_DEPTH_ALWAYS);
	glGetIntegerv(GL_DEPTH_FUNC, &v);
	expect_int("depth_func(ALWAYS)", v, GL_ALWAYS);

	/* face culling */
	lud_cull(LUD_CULL_NONE);
	expect_enabled("cull(NONE) -> GL_CULL_FACE", GL_CULL_FACE, 0);
	lud_cull(LUD_CULL_BACK);
	expect_enabled("cull(BACK) -> GL_CULL_FACE", GL_CULL_FACE, 1);
	glGetIntegerv(GL_CULL_FACE_MODE, &v);
	expect_int("cull(BACK) mode", v, GL_BACK);
	lud_cull(LUD_CULL_FRONT);
	glGetIntegerv(GL_CULL_FACE_MODE, &v);
	expect_int("cull(FRONT) mode", v, GL_FRONT);

	/* front-face winding */
	lud_front_face(LUD_WINDING_CCW);
	glGetIntegerv(GL_FRONT_FACE, &v);
	expect_int("front_face(CCW)", v, GL_CCW);
	lud_front_face(LUD_WINDING_CW);
	glGetIntegerv(GL_FRONT_FACE, &v);
	expect_int("front_face(CW)", v, GL_CW);

	/* depth write mask */
	{
		GLboolean b;
		lud_depth_mask(0);
		glGetBooleanv(GL_DEPTH_WRITEMASK, &b);
		expect_int("depth_mask(0)", b ? 1 : 0, 0);
		lud_depth_mask(1);
		glGetBooleanv(GL_DEPTH_WRITEMASK, &b);
		expect_int("depth_mask(1)", b ? 1 : 0, 1);
	}

	/* scissor */
	{
		GLint box[4];
		lud_scissor(2, 3, 8, 9);
		expect_enabled("scissor -> GL_SCISSOR_TEST", GL_SCISSOR_TEST, 1);
		glGetIntegerv(GL_SCISSOR_BOX, box);
		expect_int("scissor box x", box[0], 2);
		expect_int("scissor box y", box[1], 3);
		expect_int("scissor box w", box[2], 8);
		expect_int("scissor box h", box[3], 9);
		lud_scissor_off();
		expect_enabled("scissor_off -> GL_SCISSOR_TEST", GL_SCISSOR_TEST, 0);
	}

	/* blend modes */
	lud_blend(LUD_BLEND_NONE);
	expect_enabled("blend(NONE) -> GL_BLEND", GL_BLEND, 0);
	lud_blend(LUD_BLEND_ALPHA);
	expect_enabled("blend(ALPHA) -> GL_BLEND", GL_BLEND, 1);
	glGetIntegerv(GL_BLEND_SRC_RGB, &v);
	expect_int("blend(ALPHA) src", v, GL_SRC_ALPHA);
	glGetIntegerv(GL_BLEND_DST_RGB, &v);
	expect_int("blend(ALPHA) dst", v, GL_ONE_MINUS_SRC_ALPHA);
	lud_blend(LUD_BLEND_ADD);
	glGetIntegerv(GL_BLEND_SRC_RGB, &v);
	expect_int("blend(ADD) src", v, GL_SRC_ALPHA);
	glGetIntegerv(GL_BLEND_DST_RGB, &v);
	expect_int("blend(ADD) dst", v, GL_ONE);

	/* read_pixels: clear to a known color and read it back */
	{
		unsigned char px[4] = {0, 0, 0, 0};
		lud_clear(1.0f, 0.0f, 0.0f, 1.0f);  /* opaque red */
		lud_read_pixels(0, 0, 1, 1, px);
		expect_int("read_pixels R", px[0], 255);
		expect_int("read_pixels G", px[1], 0);
		expect_int("read_pixels B", px[2], 0);
		expect_int("read_pixels A", px[3], 255);
	}

	/* render target: create one a different size than the window, draw
	 * into it, and read it back (exercises the target-height flip path) */
	{
		unsigned char px[4] = {0, 0, 0, 0};
		lud_target_t rt = lud_make_render_target(&(lud_target_desc_t){
			.width = 32, .height = 16,
			.format = LUD_PIXFMT_RGBA8,
			.min_filter = LUD_FILTER_NEAREST,
			.mag_filter = LUD_FILTER_NEAREST,
			.depth = 1,
		});
		lud_texture_t ct = lud_render_target_texture(rt);
		expect_int("render target created", rt.id != 0, 1);
		expect_int("rt color texture valid", ct.id != 0, 1);
		expect_int("rt texture width", lud_texture_width(ct), 32);
		expect_int("rt texture height", lud_texture_height(ct), 16);

		lud_bind_render_target(rt);
		lud_clear(0.0f, 1.0f, 0.0f, 1.0f);  /* opaque green into target */
		lud_read_pixels(0, 0, 1, 1, px);
		expect_int("rt read R", px[0], 0);
		expect_int("rt read G", px[1], 255);
		expect_int("rt read B", px[2], 0);
		expect_int("rt read A", px[3], 255);

		lud_bind_render_target((lud_target_t){0});  /* back to window */
		lud_destroy_render_target(rt);
		expect_int("rt texture freed on destroy", lud_texture_width(ct), 0);
	}

	/* flush is fire-and-forget; just confirm it does not error */
	lud_flush();
	expect_int("flush -> glGetError", (GLint)glGetError(), GL_NO_ERROR);

	printf("\n%d checks, %d failures\n", checks, failures);
	lud_quit();
}

int
main(int argc, char **argv)
{
	lud_run(&(lud_desc_t){
		.app_name = "renderstate_test",
		.width = 64, .height = 64,
		.gles_version = 3,
		.init = init,
		.argc = argc, .argv = argv,
	});
	return failures ? 1 : 0;
}
