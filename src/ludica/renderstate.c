/*
 * renderstate - thin wrappers over GL render state
 *
 * Keeps application code off raw <GLES2/gl2.h> so the GLES2/GLES3/WebGL
 * backend differences stay contained inside ludica.
 *
 * Made by a machine. PUBLIC DOMAIN (CC0-1.0)
 */
#include "ludica.h"
#include "ludica_gfx.h"
#include <GLES2/gl2.h>
#include <stdlib.h>
#include <string.h>

void
lud_depth_test(int enable)
{
	if (enable)
		glEnable(GL_DEPTH_TEST);
	else
		glDisable(GL_DEPTH_TEST);
}

void
lud_depth_func(enum lud_depth_func fn)
{
	GLenum f;

	switch (fn) {
	case LUD_DEPTH_LEQUAL: f = GL_LEQUAL;  break;
	case LUD_DEPTH_ALWAYS: f = GL_ALWAYS;  break;
	case LUD_DEPTH_LESS:
	default:               f = GL_LESS;    break;
	}
	glDepthFunc(f);
}

void
lud_cull(enum lud_cull mode)
{
	switch (mode) {
	case LUD_CULL_BACK:
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
		break;
	case LUD_CULL_FRONT:
		glEnable(GL_CULL_FACE);
		glCullFace(GL_FRONT);
		break;
	case LUD_CULL_NONE:
	default:
		glDisable(GL_CULL_FACE);
		break;
	}
}

void
lud_front_face(enum lud_winding w)
{
	glFrontFace(w == LUD_WINDING_CW ? GL_CW : GL_CCW);
}

void
lud_depth_mask(int write)
{
	glDepthMask(write ? GL_TRUE : GL_FALSE);
}

void
lud_scissor(int x, int y, int w, int h)
{
	glEnable(GL_SCISSOR_TEST);
	glScissor(x, y, w, h);
}

void
lud_scissor_off(void)
{
	glDisable(GL_SCISSOR_TEST);
}

void
lud_read_pixels(int x, int y, int w, int h, void *rgba)
{
	unsigned char *buf = rgba;
	int stride, gl_y, row;
	unsigned char *tmp, *top, *bot;

	if (w <= 0 || h <= 0 || !buf)
		return;

	stride = w * 4;
	/* Flip the top-left origin to GL's bottom-left for the read. */
	gl_y = lud_height() - y - h;
	glReadPixels(x, gl_y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);

	/* glReadPixels returns rows bottom-first; flip to top-first so the
	 * caller gets the region the way it was addressed. */
	if (h < 2)
		return;
	tmp = malloc(stride);
	if (!tmp)
		return;  /* leave data bottom-up rather than fail */
	for (row = 0; row < h / 2; row++) {
		top = buf + row * stride;
		bot = buf + (h - 1 - row) * stride;
		memcpy(tmp, top, stride);
		memcpy(top, bot, stride);
		memcpy(bot, tmp, stride);
	}
	free(tmp);
}

void
lud_blend(enum lud_blend mode)
{
	switch (mode) {
	case LUD_BLEND_ALPHA:
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		break;
	case LUD_BLEND_ADD:
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		break;
	case LUD_BLEND_NONE:
	default:
		glDisable(GL_BLEND);
		break;
	}
}

void
lud_flush(void)
{
	glFlush();
}
