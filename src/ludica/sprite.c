/*
 * sprite.c — 2D sprite batch renderer.
 *
 * Immediate-mode quad submission with automatic batching.
 * Flushes when texture changes or batch is full.
 */

#include "ludica_internal.h"
#include "ludica_gfx.h"
#include <GLES2/gl2.h>
#include <string.h>

/* Maximum quads per batch (6 verts each) */
#define MAX_QUADS 1024
#define VERTS_PER_QUAD 6
#define FLOATS_PER_VERT 8  /* x, y, u, v, r, g, b, a */

/* ---- Built-in shaders (generated from shaders/) ---------------------- */

extern const char sprite_vert[];
extern const char sprite_frag[];

/* Batch state */
static struct {
	int initialized;
	lud_shader_t shader;
	GLuint vbo;
	float verts[MAX_QUADS * VERTS_PER_QUAD * FLOATS_PER_VERT];
	int quad_count;
	lud_texture_t current_tex;
	float proj[16];
} batch;

/* 1x1 white pixel texture for solid-color drawing */
static lud_texture_t white_tex;

static lud_texture_t
get_white_tex(void)
{
	if (white_tex.id == 0) {
		unsigned char pixel[4] = { 255, 255, 255, 255 };
		white_tex = lud_make_texture(&(lud_texture_desc_t){
			.width = 1, .height = 1,
			.format = LUD_PIXFMT_RGBA8,
			.min_filter = LUD_FILTER_NEAREST,
			.mag_filter = LUD_FILTER_NEAREST,
			.data = pixel,
		});
	}
	return white_tex;
}

static void
sprite_init(void)
{
	if (batch.initialized)
		return;

	batch.shader = lud_make_shader(&(lud_shader_desc_t){
		.vert_src = sprite_vert,
		.frag_src = sprite_frag,
		.attrs = { "a_pos", "a_uv", "a_color" },
		.num_attrs = 3,
	});

	glGenBuffers(1, &batch.vbo);

	batch.initialized = 1;
}

static void
flush(void)
{
	int vert_count;
	int stride;

	if (batch.quad_count == 0)
		return;

	vert_count = batch.quad_count * VERTS_PER_QUAD;
	stride = FLOATS_PER_VERT * (int)sizeof(float);

	lud_apply_shader(batch.shader);
	lud_uniform_mat4(batch.shader, "u_proj", batch.proj);
	lud_uniform_int(batch.shader, "u_tex", 0);
	lud_bind_texture(batch.current_tex, 0);

	glBindBuffer(GL_ARRAY_BUFFER, batch.vbo);
	glBufferData(GL_ARRAY_BUFFER,
	             vert_count * FLOATS_PER_VERT * sizeof(float),
	             batch.verts, GL_STREAM_DRAW);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void *)0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
	                      (void *)(2 * sizeof(float)));
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride,
	                      (void *)(4 * sizeof(float)));

	glDrawArrays(GL_TRIANGLES, 0, vert_count);

	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
	glDisableVertexAttribArray(2);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	batch.quad_count = 0;
}

static void
push_vertex(float *dst, float x, float y, float u, float v,
            float r, float g, float b, float a)
{
	dst[0] = x;
	dst[1] = y;
	dst[2] = u;
	dst[3] = v;
	dst[4] = r;
	dst[5] = g;
	dst[6] = b;
	dst[7] = a;
}

void
lud_sprite_begin(float x, float y, float w, float h)
{
	sprite_init();

	/* Orthographic projection, Y-down */
	memset(batch.proj, 0, sizeof(batch.proj));
	batch.proj[0]  =  2.0f / w;
	batch.proj[5]  = -2.0f / h;
	batch.proj[10] = -1.0f;
	batch.proj[12] = -(2.0f * x / w + 1.0f);
	batch.proj[13] =  (2.0f * y / h + 1.0f);
	batch.proj[15] =  1.0f;

	batch.quad_count = 0;
	batch.current_tex.id = 0;

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void
sprite_draw_internal(lud_texture_t tex,
                     float dst_x, float dst_y, float dst_w, float dst_h,
                     float src_x, float src_y, float src_w, float src_h,
                     int flip_x,
                     float cr, float cg, float cb, float ca)
{
	float tw, th;
	float u0, v0, u1, v1;
	float x0, y0, x1, y1;
	float *v;

	/* Flush if texture changed or batch full */
	if (batch.current_tex.id != tex.id || batch.quad_count >= MAX_QUADS) {
		flush();
		batch.current_tex = tex;
	}

	/* Compute UVs */
	tw = (float)lud_texture_width(tex);
	th = (float)lud_texture_height(tex);
	if (tw <= 0.0f || th <= 0.0f)
		return;

	if (src_w <= 0.0f) {
		u0 = 0.0f; v0 = 0.0f;
		u1 = 1.0f; v1 = 1.0f;
	} else {
		u0 = src_x / tw;
		v0 = src_y / th;
		u1 = (src_x + src_w) / tw;
		v1 = (src_y + src_h) / th;
	}

	if (flip_x) {
		float tmp = u0;
		u0 = u1;
		u1 = tmp;
	}

	x0 = dst_x;
	y0 = dst_y;
	x1 = dst_x + dst_w;
	y1 = dst_y + dst_h;

	/* Two triangles */
	v = batch.verts + batch.quad_count * VERTS_PER_QUAD * FLOATS_PER_VERT;
	push_vertex(v +  0, x0, y0, u0, v0, cr, cg, cb, ca);
	push_vertex(v +  8, x0, y1, u0, v1, cr, cg, cb, ca);
	push_vertex(v + 16, x1, y1, u1, v1, cr, cg, cb, ca);
	push_vertex(v + 24, x0, y0, u0, v0, cr, cg, cb, ca);
	push_vertex(v + 32, x1, y1, u1, v1, cr, cg, cb, ca);
	push_vertex(v + 40, x1, y0, u1, v0, cr, cg, cb, ca);

	batch.quad_count++;
}

void
lud_sprite_draw(lud_texture_t tex,
                   float dst_x, float dst_y, float dst_w, float dst_h,
                   float src_x, float src_y, float src_w, float src_h)
{
	sprite_draw_internal(tex, dst_x, dst_y, dst_w, dst_h,
	                     src_x, src_y, src_w, src_h, 0,
	                     1.0f, 1.0f, 1.0f, 1.0f);
}

void
lud_sprite_draw_flip(lud_texture_t tex,
                        float dst_x, float dst_y, float dst_w, float dst_h,
                        float src_x, float src_y, float src_w, float src_h,
                        int flip_x)
{
	sprite_draw_internal(tex, dst_x, dst_y, dst_w, dst_h,
	                     src_x, src_y, src_w, src_h, flip_x,
	                     1.0f, 1.0f, 1.0f, 1.0f);
}

void
lud_sprite_draw_tinted(lud_texture_t tex,
                          float dst_x, float dst_y, float dst_w, float dst_h,
                          float src_x, float src_y, float src_w, float src_h,
                          float r, float g, float b, float a)
{
	sprite_draw_internal(tex, dst_x, dst_y, dst_w, dst_h,
	                     src_x, src_y, src_w, src_h, 0,
	                     r, g, b, a);
}

void
lud_sprite_rect(float x, float y, float w, float h,
                   float r, float g, float b, float a)
{
	lud_texture_t wt = get_white_tex();
	sprite_draw_internal(wt, x, y, w, h,
	                     0, 0, 1, 1, 0,
	                     r, g, b, a);
}

void
lud_sprite_rect_lines(float x, float y, float w, float h,
                         float r, float g, float b, float a)
{
	lud_texture_t wt = get_white_tex();
	/* top */
	sprite_draw_internal(wt, x, y, w, 1, 0, 0, 1, 1, 0, r, g, b, a);
	/* bottom */
	sprite_draw_internal(wt, x, y + h - 1, w, 1, 0, 0, 1, 1, 0, r, g, b, a);
	/* left */
	sprite_draw_internal(wt, x, y + 1, 1, h - 2, 0, 0, 1, 1, 0, r, g, b, a);
	/* right */
	sprite_draw_internal(wt, x + w - 1, y + 1, 1, h - 2, 0, 0, 1, 1, 0, r, g, b, a);
}

void
lud_sprite_end(void)
{
	flush();
	glDisable(GL_BLEND);
}
