/*
 * sprite.c — 2D sprite batch renderer.
 *
 * Immediate-mode quad submission with automatic batching.
 * Flushes when texture changes or batch is full.
 */

#include "initgl_internal.h"
#include "initgl_gfx.h"
#include <GLES2/gl2.h>
#include <string.h>

/* Maximum quads per batch (6 verts each) */
#define MAX_QUADS 1024
#define VERTS_PER_QUAD 6
#define FLOATS_PER_VERT 4  /* x, y, u, v */

/* Built-in sprite shader sources */
static const char sprite_vert_src[] =
	"attribute vec2 a_pos;\n"
	"attribute vec2 a_uv;\n"
	"uniform mat4 u_proj;\n"
	"varying vec2 v_uv;\n"
	"void main() {\n"
	"    v_uv = a_uv;\n"
	"    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);\n"
	"}\n";

static const char sprite_frag_src[] =
	"precision mediump float;\n"
	"varying vec2 v_uv;\n"
	"uniform sampler2D u_tex;\n"
	"void main() {\n"
	"    gl_FragColor = texture2D(u_tex, v_uv);\n"
	"}\n";

/* Batch state */
static struct {
	int initialized;
	initgl_shader_t shader;
	GLuint vbo;
	float verts[MAX_QUADS * VERTS_PER_QUAD * FLOATS_PER_VERT];
	int quad_count;
	initgl_texture_t current_tex;
	float proj[16];
} batch;

static void
sprite_init(void)
{
	if (batch.initialized)
		return;

	batch.shader = initgl_make_shader(&(initgl_shader_desc_t){
		.vert_src = sprite_vert_src,
		.frag_src = sprite_frag_src,
		.attrs = { "a_pos", "a_uv" },
		.num_attrs = 2,
	});

	glGenBuffers(1, &batch.vbo);

	batch.initialized = 1;
}

static void
flush(void)
{
	int vert_count;

	if (batch.quad_count == 0)
		return;

	vert_count = batch.quad_count * VERTS_PER_QUAD;

	initgl_apply_shader(batch.shader);
	initgl_uniform_mat4(batch.shader, "u_proj", batch.proj);
	initgl_uniform_int(batch.shader, "u_tex", 0);
	initgl_bind_texture(batch.current_tex, 0);

	glBindBuffer(GL_ARRAY_BUFFER, batch.vbo);
	glBufferData(GL_ARRAY_BUFFER,
	             vert_count * FLOATS_PER_VERT * sizeof(float),
	             batch.verts, GL_STREAM_DRAW);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
	                      FLOATS_PER_VERT * sizeof(float), (void *)0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
	                      FLOATS_PER_VERT * sizeof(float),
	                      (void *)(2 * sizeof(float)));

	glDrawArrays(GL_TRIANGLES, 0, vert_count);

	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	batch.quad_count = 0;
}

static void
push_vertex(float *dst, float x, float y, float u, float v)
{
	dst[0] = x;
	dst[1] = y;
	dst[2] = u;
	dst[3] = v;
}

void
initgl_sprite_begin(float x, float y, float w, float h)
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

void
initgl_sprite_draw(initgl_texture_t tex,
                   float dst_x, float dst_y, float dst_w, float dst_h,
                   float src_x, float src_y, float src_w, float src_h)
{
	initgl_sprite_draw_flip(tex, dst_x, dst_y, dst_w, dst_h,
	                        src_x, src_y, src_w, src_h, 0);
}

void
initgl_sprite_draw_flip(initgl_texture_t tex,
                        float dst_x, float dst_y, float dst_w, float dst_h,
                        float src_x, float src_y, float src_w, float src_h,
                        int flip_x)
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
	tw = (float)initgl_texture_width(tex);
	th = (float)initgl_texture_height(tex);
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

	/* Two triangles: top-left, bottom-left, bottom-right, top-left, bottom-right, top-right */
	v = batch.verts + batch.quad_count * VERTS_PER_QUAD * FLOATS_PER_VERT;
	push_vertex(v +  0, x0, y0, u0, v0);
	push_vertex(v +  4, x0, y1, u0, v1);
	push_vertex(v +  8, x1, y1, u1, v1);
	push_vertex(v + 12, x0, y0, u0, v0);
	push_vertex(v + 16, x1, y1, u1, v1);
	push_vertex(v + 20, x1, y0, u1, v0);

	batch.quad_count++;
}

void
initgl_sprite_end(void)
{
	flush();
	glDisable(GL_BLEND);
}
