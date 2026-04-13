/*
 * framebuffer.c — palette-indexed pixel framebuffer with optional CRT effect.
 *
 * Wraps a single-channel (R8) screen texture + 256x1 RGBA palette texture
 * + lookup shader + fullscreen quad into one opaque handle. The user writes
 * palette indices into a CPU-side pixel buffer (lock/unlock), then blits
 * to the current viewport.
 */

#include "ludica_internal.h"
#include "ludica_gfx.h"
#include <GLES2/gl2.h>
#include <string.h>
#include <stdlib.h>

#define MAX_FRAMEBUFFERS 8

/* ---- Built-in shaders ------------------------------------------------- */

/* Shared vertex shader: full-screen quad [-1,1] → UV [0,1] */
static const char fb_vert_src[] =
	"#version 100\n"
	"precision mediump float;\n"
	"attribute vec4 vertex;\n"
	"varying vec2 v_uv;\n"
	"void main(void) {\n"
	"  v_uv = vertex.xy * 0.5 + 0.5;\n"
	"  gl_Position = vertex;\n"
	"}\n";

/* Flat palette lookup — no post-process */
static const char fb_frag_flat[] =
	"#version 100\n"
	"precision mediump float;\n"
	"uniform sampler2D u_screen;\n"
	"uniform sampler2D u_palette;\n"
	"varying vec2 v_uv;\n"
	"void main(void) {\n"
	"  float idx = texture2D(u_screen, v_uv).r;\n"
	"  gl_FragColor = texture2D(u_palette, vec2(idx, 0.0));\n"
	"}\n";

/* CRT scanlines + barrel distortion */
static const char fb_frag_crt[] =
	"#version 100\n"
	"precision mediump float;\n"
	"uniform sampler2D u_screen;\n"
	"uniform sampler2D u_palette;\n"
	"uniform vec2 u_resolution;\n"
	"varying vec2 v_uv;\n"
	"\n"
	"vec2 barrel(vec2 uv) {\n"
	"  vec2 c = uv - 0.5;\n"
	"  float r2 = dot(c, c);\n"
	"  return uv + c * r2 * 0.15;\n"
	"}\n"
	"\n"
	"void main(void) {\n"
	"  vec2 uv = barrel(v_uv);\n"
	"  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {\n"
	"    gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);\n"
	"    return;\n"
	"  }\n"
	"  float idx = texture2D(u_screen, uv).r;\n"
	"  vec4 col = texture2D(u_palette, vec2(idx, 0.0));\n"
	"  /* scanlines: darken every other physical row */\n"
	"  float scan = 0.85 + 0.15 * sin(uv.y * u_resolution.y * 3.14159);\n"
	"  /* slight vignette */\n"
	"  vec2 vig = uv * (1.0 - uv);\n"
	"  float v = clamp(pow(vig.x * vig.y * 15.0, 0.25), 0.0, 1.0);\n"
	"  gl_FragColor = vec4(col.rgb * scan * v, 1.0);\n"
	"}\n";

/* ---- Full-screen quad geometry ---------------------------------------- */

static const float quad_verts[] = {
	-1.0f, -1.0f, 0.0f, 1.0f,
	 1.0f, -1.0f, 0.0f, 1.0f,
	 1.0f,  1.0f, 0.0f, 1.0f,
	-1.0f,  1.0f, 0.0f, 1.0f,
};

/* ---- Slot pool -------------------------------------------------------- */

typedef struct {
	int used;
	int width, height;
	enum lud_crt_mode crt;
	unsigned char *pixels;          /* CPU-side pixel buffer */
	lud_shader_t shader;
	lud_mesh_t quad;
	lud_texture_t screen_tex;
	lud_texture_t palette_tex;
} fb_slot_t;

static fb_slot_t slots[MAX_FRAMEBUFFERS];

static int
alloc_slot(void)
{
	int i;
	for (i = 0; i < MAX_FRAMEBUFFERS; i++)
		if (!slots[i].used)
			return i;
	return -1;
}

static fb_slot_t *
get_slot(lud_framebuffer_t fb)
{
	if (fb.id == 0 || fb.id > MAX_FRAMEBUFFERS)
		return NULL;
	fb_slot_t *s = &slots[fb.id - 1];
	return s->used ? s : NULL;
}

/* ---- Public API ------------------------------------------------------- */

lud_framebuffer_t
lud_make_framebuffer(const lud_framebuffer_desc_t *desc)
{
	lud_framebuffer_t out = {0};
	fb_slot_t *s;
	int idx;
	const char *frag;

	idx = alloc_slot();
	if (idx < 0) {
		lud_err("framebuffer pool exhausted");
		return out;
	}

	s = &slots[idx];
	memset(s, 0, sizeof(*s));
	s->used = 1;
	s->width = desc->width;
	s->height = desc->height;
	s->crt = desc->crt;

	/* CPU pixel buffer */
	s->pixels = (unsigned char *)calloc(1, (size_t)desc->width * desc->height);
	if (!s->pixels) {
		lud_err("framebuffer pixel alloc failed");
		s->used = 0;
		return out;
	}

	/* Choose fragment shader */
	frag = (desc->crt == LUD_CRT_SCANLINES) ? fb_frag_crt : fb_frag_flat;

	/* Compile shader */
	s->shader = lud_make_shader(&(lud_shader_desc_t){
		.vert_src = fb_vert_src,
		.frag_src = frag,
		.attrs = { "vertex" },
		.num_attrs = 1,
	});

	/* Full-screen quad */
	s->quad = lud_make_mesh(&(lud_mesh_desc_t){
		.vertices = quad_verts,
		.vertex_count = 4,
		.vertex_stride = 16,
		.layout = { { .size = 4, .offset = 0 } },
		.num_attrs = 1,
		.primitive = LUD_PRIM_TRIANGLE_FAN,
		.usage = LUD_USAGE_STATIC,
	});

	/* Screen texture: WxH single-channel */
	s->screen_tex = lud_make_texture(&(lud_texture_desc_t){
		.width = desc->width,
		.height = desc->height,
		.format = LUD_PIXFMT_R8,
		.min_filter = LUD_FILTER_LINEAR,
		.mag_filter = LUD_FILTER_LINEAR,
		.data = s->pixels,
	});

	/* Palette texture: 256x1 RGBA, default all-black */
	s->palette_tex = lud_make_texture(&(lud_texture_desc_t){
		.width = 256,
		.height = 1,
		.format = LUD_PIXFMT_RGBA8,
		.min_filter = LUD_FILTER_LINEAR,
		.mag_filter = LUD_FILTER_LINEAR,
		.data = NULL,
	});

	out.id = (unsigned)(idx + 1);
	return out;
}

void
lud_destroy_framebuffer(lud_framebuffer_t fb)
{
	fb_slot_t *s = get_slot(fb);
	if (!s) return;
	lud_destroy_mesh(s->quad);
	lud_destroy_texture(s->screen_tex);
	lud_destroy_texture(s->palette_tex);
	lud_destroy_shader(s->shader);
	free(s->pixels);
	memset(s, 0, sizeof(*s));
}

void
lud_framebuffer_palette(lud_framebuffer_t fb,
                           const unsigned int palette[256])
{
	fb_slot_t *s = get_slot(fb);
	if (!s) return;
	lud_update_texture(s->palette_tex, 0, 0, 256, 1, palette);
}

unsigned char *
lud_framebuffer_lock(lud_framebuffer_t fb)
{
	fb_slot_t *s = get_slot(fb);
	return s ? s->pixels : NULL;
}

void
lud_framebuffer_unlock(lud_framebuffer_t fb)
{
	fb_slot_t *s = get_slot(fb);
	if (!s) return;
	lud_update_texture(s->screen_tex, 0, 0, s->width, s->height, s->pixels);
}

void
lud_framebuffer_blit(lud_framebuffer_t fb)
{
	fb_slot_t *s = get_slot(fb);
	if (!s) return;

	lud_apply_shader(s->shader);
	lud_bind_texture(s->screen_tex, 0);
	lud_bind_texture(s->palette_tex, 1);
	lud_uniform_int(s->shader, "u_screen", 0);
	lud_uniform_int(s->shader, "u_palette", 1);

	if (s->crt == LUD_CRT_SCANLINES) {
		lud_uniform_vec2(s->shader, "u_resolution",
		                    (float)s->width, (float)s->height);
	}

	lud_draw(s->quad);
}
