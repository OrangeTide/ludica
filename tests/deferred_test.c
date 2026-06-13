/* deferred_test.c : verify deferred destroy waits for end of frame */
/* Made by a machine. PUBLIC DOMAIN (CC0-1.0) */

/* Resource handles carry a pool id. A direct lud_destroy_mesh frees the
 * pool slot immediately, so the next make reuses that id. A deferred
 * destroy must NOT free the slot until the frame's end-of-frame flush, so
 * a make in the same frame gets a fresh id, and only a make on a later
 * frame reuses the freed id. That difference is what this test observes. */

#include "ludica.h"
#include "ludica_gfx.h"
#include <stdio.h>

static int checks;
static int failures;
static int step;
static unsigned deferred_id;
static unsigned direct_id;

static void
expect(const char *what, int cond)
{
	checks++;
	if (cond) {
		printf("ok   %s\n", what);
	} else {
		failures++;
		printf("FAIL %s\n", what);
	}
}

static const float TRI[6] = { 0, 0, 1, 0, 0, 1 };

static lud_mesh_t
make_tri(void)
{
	return lud_make_mesh(&(lud_mesh_desc_t){
		.vertices = TRI, .vertex_count = 3,
		.vertex_stride = 2 * (int)sizeof(float),
		.layout = { { .size = 2, .offset = 0 } }, .num_attrs = 1,
		.usage = LUD_USAGE_STATIC, .primitive = LUD_PRIM_TRIANGLES,
	});
}

static void
frame(float dt)
{
	(void)dt;
	if (step == 0) {
		/* Deferred destroy A, then make B in the same frame. */
		lud_mesh_t a = make_tri();
		deferred_id = a.id;
		lud_destroy_mesh_deferred(a);
		lud_mesh_t b = make_tri();
		expect("deferred destroy keeps slot during frame (B != A)",
		       b.id != deferred_id);

		/* Exercise the texture path too (observed only by not crashing
		 * and by the flush running at end of frame). */
		lud_texture_t t = lud_make_texture(&(lud_texture_desc_t){
			.width = 4, .height = 4, .format = LUD_PIXFMT_RGBA8,
			.min_filter = LUD_FILTER_NEAREST,
			.mag_filter = LUD_FILTER_NEAREST, .data = NULL,
		});
		lud_destroy_texture_deferred(t);
		step = 1;
	} else if (step == 1) {
		/* The end-of-frame flush after frame 0 freed A's slot, so a
		 * fresh make now reuses that id. */
		lud_mesh_t c = make_tri();
		expect("deferred slot freed after frame (C reuses A's id)",
		       c.id == deferred_id);

		/* Contrast: a direct destroy frees the slot at once, so the
		 * next make reuses it within the same frame. */
		lud_mesh_t d = make_tri();
		direct_id = d.id;
		lud_destroy_mesh(d);
		lud_mesh_t e = make_tri();
		expect("direct destroy reuses slot immediately (E == D)",
		       e.id == direct_id);

		printf("\n%d checks, %d failures\n", checks, failures);
		lud_quit();
	}
}

int
main(int argc, char **argv)
{
	lud_run(&(lud_desc_t){
		.app_name = "deferred_test",
		.width = 64, .height = 64,
		.frame = frame,
		.argc = argc, .argv = argv,
	});
	return failures ? 1 : 0;
}
