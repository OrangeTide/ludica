#include "ludica_internal.h"
#include "ludica_gfx.h"
#include <GLES2/gl2.h>
#include <string.h>

/* GLES3 3D texture functions (for texture arrays) */
#ifndef glTexImage3D
extern void glTexImage3D(GLenum target, GLint level, GLint internalformat,
                         GLsizei width, GLsizei height, GLsizei depth, GLint border,
                         GLenum format, GLenum type, const void *data);
#endif
#ifndef glTexSubImage3D
extern void glTexSubImage3D(GLenum target, GLint level,
                            GLint xoffset, GLint yoffset, GLint zoffset,
                            GLsizei width, GLsizei height, GLsizei depth,
                            GLenum format, GLenum type, const void *data);
#endif

/* GLES3 formats (not in GLES2 headers) */
#ifndef GL_SRGB8
#define GL_SRGB8       0x8C41
#endif
#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif
#ifndef GL_R8
#define GL_R8          0x8229
#endif
#ifndef GL_RED
#define GL_RED         0x1903
#endif
#ifndef GL_RGBA16F
#define GL_RGBA16F     0x881A
#endif
#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT  0x140B
#endif
#ifndef GL_RG16UI
#define GL_RG16UI      0x823A
#endif
#ifndef GL_RG_INTEGER
#define GL_RG_INTEGER  0x8228
#endif
#ifndef GL_RG
#define GL_RG          0x8227
#endif
#ifndef GL_UNSIGNED_SHORT
#define GL_UNSIGNED_SHORT 0x1403
#endif
#ifndef GL_TEXTURE_2D_ARRAY
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#endif

#define MAX_TEXTURES 64

typedef struct {
	int used;
	GLuint tex;
	int width, height;
	int num_layers;       /* for arrays; 0 for 2D textures */
	GLenum target;        /* GL_TEXTURE_2D or GL_TEXTURE_2D_ARRAY */
	GLenum gl_format;     /* external format (GL_RGB, GL_RGBA, ...) */
	GLenum gl_internal;   /* internal format (may differ for sRGB) */
	GLenum gl_type;       /* pixel type (GL_UNSIGNED_BYTE, GL_HALF_FLOAT, ...) */
	int bpp;
} texture_slot_t;

static texture_slot_t slots[MAX_TEXTURES];

static int
alloc_slot(void)
{
	int i;
	for (i = 0; i < MAX_TEXTURES; i++)
		if (!slots[i].used)
			return i;
	return -1;
}

static texture_slot_t *
get_slot(lud_texture_t tex)
{
	if (tex.id == 0 || tex.id > MAX_TEXTURES)
		return NULL;
	texture_slot_t *s = &slots[tex.id - 1];
	return s->used ? s : NULL;
}

static void
format_to_gl(enum lud_pixel_format fmt, GLenum *gl_fmt, GLenum *gl_internal,
             GLenum *gl_type, int *bpp)
{
	*gl_type = GL_UNSIGNED_BYTE;

	switch (fmt) {
	case LUD_PIXFMT_R8:
		if (lud__state.gles_version >= 3) {
			*gl_fmt = GL_RED; *gl_internal = GL_R8;
		} else {
			*gl_fmt = GL_LUMINANCE; *gl_internal = GL_LUMINANCE;
		}
		*bpp = 1;
		break;
	case LUD_PIXFMT_RGB8:
		*gl_fmt = GL_RGB; *gl_internal = GL_RGB; *bpp = 3;
		break;
	case LUD_PIXFMT_RGBA8:
		*gl_fmt = GL_RGBA; *gl_internal = GL_RGBA; *bpp = 4;
		break;
	case LUD_PIXFMT_SRGB8:
		*gl_fmt = GL_RGB; *bpp = 3;
		*gl_internal = (lud__state.gles_version >= 3) ? GL_SRGB8 : GL_RGB;
		break;
	case LUD_PIXFMT_SRGBA8:
		*gl_fmt = GL_RGBA; *bpp = 4;
		*gl_internal = (lud__state.gles_version >= 3) ? GL_SRGB8_ALPHA8
		                                              : GL_RGBA;
		break;
	case LUD_PIXFMT_RGBA16F:
		if (lud__state.gles_version < 3) {
			lud_err("RGBA16F requires GLES3");
			*gl_fmt = GL_RGBA; *gl_internal = GL_RGBA; *bpp = 4;
			break;
		}
		*gl_fmt = GL_RGBA; *gl_internal = GL_RGBA16F;
		*gl_type = GL_HALF_FLOAT; *bpp = 8;
		break;
	case LUD_PIXFMT_RG16UI:
		if (lud__state.gles_version < 3) {
			lud_err("RG16UI requires GLES3");
			*gl_fmt = GL_RGBA; *gl_internal = GL_RGBA; *bpp = 4;
			break;
		}
		*gl_fmt = GL_RG_INTEGER; *gl_internal = GL_RG16UI;
		*gl_type = GL_UNSIGNED_SHORT; *bpp = 4;
		break;
	default:
		*gl_fmt = GL_RGBA; *gl_internal = GL_RGBA; *bpp = 4;
		break;
	}
}

static GLenum
filter_to_gl(enum lud_filter f)
{
	return f == LUD_FILTER_NEAREST ? GL_NEAREST : GL_LINEAR;
}

lud_texture_t
lud_make_texture(const lud_texture_desc_t *desc)
{
	lud_texture_t out = {0};
	texture_slot_t *s;
	GLenum gl_fmt, gl_internal, gl_type;
	int bpp, idx;

	idx = alloc_slot();
	if (idx < 0) {
		lud_err("texture pool exhausted");
		return out;
	}

	format_to_gl(desc->format, &gl_fmt, &gl_internal, &gl_type, &bpp);

	s = &slots[idx];
	memset(s, 0, sizeof(*s));
	s->used = 1;
	s->width = desc->width;
	s->height = desc->height;
	s->num_layers = 0;
	s->target = GL_TEXTURE_2D;
	s->gl_format = gl_fmt;
	s->gl_internal = gl_internal;
	s->gl_type = gl_type;
	s->bpp = bpp;

	glGenTextures(1, &s->tex);
	glBindTexture(GL_TEXTURE_2D, s->tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
	                filter_to_gl(desc->min_filter));
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
	                filter_to_gl(desc->mag_filter));
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	if (bpp == 1)
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	glTexImage2D(GL_TEXTURE_2D, 0, gl_internal, desc->width, desc->height, 0,
	             gl_fmt, gl_type, desc->data);

	if (bpp == 1)
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

	glBindTexture(GL_TEXTURE_2D, 0);

	out.id = (unsigned)(idx + 1);
	return out;
}

void
lud_destroy_texture(lud_texture_t tex)
{
	texture_slot_t *s = get_slot(tex);
	if (!s) return;
	if (s->tex) glDeleteTextures(1, &s->tex);
	memset(s, 0, sizeof(*s));
}

void
lud_bind_texture(lud_texture_t tex, int unit)
{
	texture_slot_t *s = get_slot(tex);
	glActiveTexture(GL_TEXTURE0 + unit);
	if (s) {
		glBindTexture(s->target, s->tex);
	} else {
		glBindTexture(GL_TEXTURE_2D, 0);
	}
}

void
lud_update_texture(lud_texture_t tex, int x, int y, int w, int h,
                      const void *data)
{
	texture_slot_t *s = get_slot(tex);
	if (!s || !data) return;

	glBindTexture(GL_TEXTURE_2D, s->tex);

	if (s->bpp == 1)
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h,
	                s->gl_format, s->gl_type, data);

	if (s->bpp == 1)
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

/* --- Texture queries --- */

int
lud_texture_width(lud_texture_t tex)
{
	texture_slot_t *s = get_slot(tex);
	return s ? s->width : 0;
}

int
lud_texture_height(lud_texture_t tex)
{
	texture_slot_t *s = get_slot(tex);
	return s ? s->height : 0;
}

/* --- Global state operations --- */

void
lud_clear(float r, float g, float b, float a)
{
	glClearColor(r, g, b, a);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void
lud_viewport(int x, int y, int w, int h)
{
	glViewport(x, y, w, h);
}

/* --- Texture arrays (GLES3 only) --- */

lud_texture_t
lud_make_texture_array(const lud_texture_array_desc_t *desc)
{
	lud_texture_t out = {0};
	texture_slot_t *s;
	GLenum gl_fmt, gl_internal, gl_type;
	int bpp, idx;

	/* GLES3 required */
	if (lud__state.gles_version < 3) {
		lud_err("texture arrays require GLES3");
		return out;
	}

	idx = alloc_slot();
	if (idx < 0) {
		lud_err("texture pool exhausted");
		return out;
	}

	format_to_gl(desc->format, &gl_fmt, &gl_internal, &gl_type, &bpp);

	s = &slots[idx];
	memset(s, 0, sizeof(*s));
	s->used = 1;
	s->width = desc->width;
	s->height = desc->height;
	s->num_layers = desc->num_layers;
	s->target = GL_TEXTURE_2D_ARRAY;
	s->gl_format = gl_fmt;
	s->gl_internal = gl_internal;
	s->gl_type = gl_type;
	s->bpp = bpp;

	glGenTextures(1, &s->tex);
	glBindTexture(GL_TEXTURE_2D_ARRAY, s->tex);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER,
	                filter_to_gl(desc->min_filter));
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER,
	                filter_to_gl(desc->mag_filter));
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	if (bpp == 1)
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	/* Allocate array storage without initial data */
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, gl_internal,
	             desc->width, desc->height, desc->num_layers, 0,
	             gl_fmt, gl_type, NULL);

	if (bpp == 1)
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

	out.id = (unsigned)(idx + 1);
	return out;
}

void
lud_texture_array_set_layer(lud_texture_t arr, int layer, const void *data)
{
	texture_slot_t *s = get_slot(arr);
	if (!s || !data || layer < 0 || layer >= s->num_layers)
		return;

	glBindTexture(GL_TEXTURE_2D_ARRAY, s->tex);

	if (s->bpp == 1)
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0,
	                0, 0, layer,
	                s->width, s->height, 1,
	                s->gl_format, s->gl_type, data);

	if (s->bpp == 1)
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}
