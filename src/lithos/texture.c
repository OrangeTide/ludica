#include "lithos_internal.h"
#include "lithos_gfx.h"
#include <GLES2/gl2.h>
#include <string.h>

#define MAX_TEXTURES 64

typedef struct {
	int used;
	GLuint tex;
	int width, height;
	GLenum gl_format;
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
get_slot(lithos_texture_t tex)
{
	if (tex.id == 0 || tex.id > MAX_TEXTURES)
		return NULL;
	texture_slot_t *s = &slots[tex.id - 1];
	return s->used ? s : NULL;
}

static void
format_to_gl(enum lithos_pixel_format fmt, GLenum *gl_fmt, int *bpp)
{
	switch (fmt) {
	case LITHOS_PIXFMT_R8:    *gl_fmt = GL_LUMINANCE; *bpp = 1; break;
	case LITHOS_PIXFMT_RGB8:  *gl_fmt = GL_RGB;       *bpp = 3; break;
	case LITHOS_PIXFMT_RGBA8: *gl_fmt = GL_RGBA;      *bpp = 4; break;
	default:                  *gl_fmt = GL_RGBA;       *bpp = 4; break;
	}
}

static GLenum
filter_to_gl(enum lithos_filter f)
{
	return f == LITHOS_FILTER_NEAREST ? GL_NEAREST : GL_LINEAR;
}

lithos_texture_t
lithos_make_texture(const lithos_texture_desc_t *desc)
{
	lithos_texture_t out = {0};
	texture_slot_t *s;
	GLenum gl_fmt;
	int bpp, idx;

	idx = alloc_slot();
	if (idx < 0) {
		lithos_err("texture pool exhausted");
		return out;
	}

	format_to_gl(desc->format, &gl_fmt, &bpp);

	s = &slots[idx];
	memset(s, 0, sizeof(*s));
	s->used = 1;
	s->width = desc->width;
	s->height = desc->height;
	s->gl_format = gl_fmt;
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

	glTexImage2D(GL_TEXTURE_2D, 0, gl_fmt, desc->width, desc->height, 0,
	             gl_fmt, GL_UNSIGNED_BYTE, desc->data);

	if (bpp == 1)
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

	glBindTexture(GL_TEXTURE_2D, 0);

	out.id = (unsigned)(idx + 1);
	return out;
}

void
lithos_destroy_texture(lithos_texture_t tex)
{
	texture_slot_t *s = get_slot(tex);
	if (!s) return;
	if (s->tex) glDeleteTextures(1, &s->tex);
	memset(s, 0, sizeof(*s));
}

void
lithos_bind_texture(lithos_texture_t tex, int unit)
{
	texture_slot_t *s = get_slot(tex);
	glActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(GL_TEXTURE_2D, s ? s->tex : 0);
}

void
lithos_update_texture(lithos_texture_t tex, int x, int y, int w, int h,
                      const void *data)
{
	texture_slot_t *s = get_slot(tex);
	if (!s || !data) return;

	glBindTexture(GL_TEXTURE_2D, s->tex);

	if (s->bpp == 1)
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h,
	                s->gl_format, GL_UNSIGNED_BYTE, data);

	if (s->bpp == 1)
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

/* --- Texture queries --- */

int
lithos_texture_width(lithos_texture_t tex)
{
	texture_slot_t *s = get_slot(tex);
	return s ? s->width : 0;
}

int
lithos_texture_height(lithos_texture_t tex)
{
	texture_slot_t *s = get_slot(tex);
	return s ? s->height : 0;
}

/* --- Global state operations --- */

void
lithos_clear(float r, float g, float b, float a)
{
	glClearColor(r, g, b, a);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void
lithos_viewport(int x, int y, int w, int h)
{
	glViewport(x, y, w, h);
}
