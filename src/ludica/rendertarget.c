/*
 * rendertarget - offscreen render-to-texture framebuffers
 *
 * A render target wraps a GL framebuffer object whose color attachment
 * is a ludica texture (so it can be sampled or read back) and an
 * optional depth renderbuffer for passes that need the depth test.
 *
 * Kept separate from framebuffer.c, which is the palette-indexed CPU
 * buffer used for retro/CRT output, a different concept.
 *
 * Made by a machine. PUBLIC DOMAIN (CC0-1.0)
 */
#include "ludica_internal.h"
#include "ludica_gfx.h"
#include <GLES2/gl2.h>
#include <string.h>

#define MAX_TARGETS 16

typedef struct {
	int used;
	GLuint fbo;
	GLuint depth_rb;       /* 0 if no depth buffer */
	lud_texture_t color;   /* color attachment, owned by this target */
	int width, height;
} target_slot_t;

static target_slot_t slots[MAX_TARGETS];

/* Height of the surface bound for drawing: a render target's height
 * while one is bound, else the window. lud_read_pixels uses this to
 * flip top-left coordinates to GL's bottom-left origin. */
static int bound_height;

int
lud__draw_height(void)
{
	return bound_height > 0 ? bound_height : lud__state.win_height;
}

static target_slot_t *
get_slot(lud_target_t target)
{
	if (target.id == 0 || target.id > MAX_TARGETS)
		return NULL;
	target_slot_t *s = &slots[target.id - 1];
	return s->used ? s : NULL;
}

static int
alloc_slot(void)
{
	int i;
	for (i = 0; i < MAX_TARGETS; i++)
		if (!slots[i].used)
			return i;
	return -1;
}

lud_target_t
lud_make_render_target(const lud_target_desc_t *desc)
{
	lud_target_t out = {0};
	target_slot_t *s;
	GLuint name;
	GLenum status;
	int idx;

	if (!desc || desc->width <= 0 || desc->height <= 0) {
		lud_err("render target: bad dimensions");
		return out;
	}

	idx = alloc_slot();
	if (idx < 0) {
		lud_err("render target pool exhausted");
		return out;
	}
	s = &slots[idx];
	memset(s, 0, sizeof(*s));

	/* Color attachment is a normal ludica texture so callers can bind
	 * and sample it like any other. */
	s->color = lud_make_texture(&(lud_texture_desc_t){
		.width = desc->width,
		.height = desc->height,
		.format = desc->format,
		.min_filter = desc->min_filter,
		.mag_filter = desc->mag_filter,
		.data = NULL,
	});
	if (s->color.id == 0) {
		lud_err("render target: color texture creation failed");
		return out;
	}
	name = lud__texture_glname(s->color);

	glGenFramebuffers(1, &s->fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
	                       GL_TEXTURE_2D, name, 0);

	if (desc->depth) {
		glGenRenderbuffers(1, &s->depth_rb);
		glBindRenderbuffer(GL_RENDERBUFFER, s->depth_rb);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16,
		                      desc->width, desc->height);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
		                          GL_RENDERBUFFER, s->depth_rb);
		glBindRenderbuffer(GL_RENDERBUFFER, 0);
	}

	status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (status != GL_FRAMEBUFFER_COMPLETE) {
		lud_err("render target incomplete (status 0x%04X)", (unsigned)status);
		if (s->depth_rb) glDeleteRenderbuffers(1, &s->depth_rb);
		glDeleteFramebuffers(1, &s->fbo);
		lud_destroy_texture(s->color);
		return out;
	}

	s->used = 1;
	s->width = desc->width;
	s->height = desc->height;
	out.id = (unsigned)(idx + 1);
	return out;
}

void
lud_destroy_render_target(lud_target_t target)
{
	target_slot_t *s = get_slot(target);
	if (!s) return;
	if (s->depth_rb) glDeleteRenderbuffers(1, &s->depth_rb);
	if (s->fbo) glDeleteFramebuffers(1, &s->fbo);
	lud_destroy_texture(s->color);
	memset(s, 0, sizeof(*s));
}

lud_texture_t
lud_render_target_texture(lud_target_t target)
{
	target_slot_t *s = get_slot(target);
	lud_texture_t none = {0};
	return s ? s->color : none;
}

void
lud_bind_render_target(lud_target_t target)
{
	target_slot_t *s = get_slot(target);

	if (s) {
		glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
		glViewport(0, 0, s->width, s->height);
		bound_height = s->height;
	} else {
		/* Zero-handle (or invalid): back to the window framebuffer. */
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, lud__state.win_width, lud__state.win_height);
		bound_height = 0;
	}
}
