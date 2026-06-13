/*
 * renderstate - thin wrappers over GL render state
 *
 * Keeps application code off raw <GLES2/gl2.h> so the GLES2/GLES3/WebGL
 * backend differences stay contained inside ludica.
 *
 * Made by a machine. PUBLIC DOMAIN (CC0-1.0)
 */
#include "ludica_gfx.h"
#include <GLES2/gl2.h>

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
lud_flush(void)
{
	glFlush();
}
