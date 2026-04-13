#include "lithos_internal.h"
#include "lithos_gfx.h"
#include <GLES2/gl2.h>
#include <stddef.h>

#define MAX_SHADERS 32

typedef struct {
	int used;
	GLuint program;
} shader_slot_t;

static shader_slot_t slots[MAX_SHADERS];

static int
alloc_slot(void)
{
	int i;
	for (i = 0; i < MAX_SHADERS; i++)
		if (!slots[i].used)
			return i;
	return -1;
}

static shader_slot_t *
get_slot(lithos_shader_t shd)
{
	if (shd.id == 0 || shd.id > MAX_SHADERS)
		return NULL;
	shader_slot_t *s = &slots[shd.id - 1];
	return s->used ? s : NULL;
}

static int
compile_stage(GLuint shader, const char *source, const char *label)
{
	GLint status;

	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (status != GL_TRUE) {
		char info[512];
		glGetShaderInfoLog(shader, sizeof(info), NULL, info);
		lithos_err("%s shader compile failed: %s", label, info);
		return LITHOS_ERR;
	}
	return LITHOS_OK;
}

lithos_shader_t
lithos_make_shader(const lithos_shader_desc_t *desc)
{
	lithos_shader_t out = {0};
	GLuint vs, fs, prog;
	GLint status;
	int idx, i;

	idx = alloc_slot();
	if (idx < 0) {
		lithos_err("shader pool exhausted");
		return out;
	}

	vs = glCreateShader(GL_VERTEX_SHADER);
	if (compile_stage(vs, desc->vert_src, "vertex") != LITHOS_OK) {
		glDeleteShader(vs);
		return out;
	}

	fs = glCreateShader(GL_FRAGMENT_SHADER);
	if (compile_stage(fs, desc->frag_src, "fragment") != LITHOS_OK) {
		glDeleteShader(vs);
		glDeleteShader(fs);
		return out;
	}

	prog = glCreateProgram();
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);

	/* Bind attribute locations before linking */
	for (i = 0; i < desc->num_attrs && i < LITHOS_MAX_VERTEX_ATTRS; i++) {
		if (desc->attrs[i])
			glBindAttribLocation(prog, i, desc->attrs[i]);
	}

	glLinkProgram(prog);
	glDeleteShader(vs);
	glDeleteShader(fs);

	glGetProgramiv(prog, GL_LINK_STATUS, &status);
	if (status != GL_TRUE) {
		char info[512];
		glGetProgramInfoLog(prog, sizeof(info), NULL, info);
		lithos_err("shader link failed: %s", info);
		glDeleteProgram(prog);
		return out;
	}

	slots[idx].used = 1;
	slots[idx].program = prog;
	out.id = (unsigned)(idx + 1);
	return out;
}

void
lithos_destroy_shader(lithos_shader_t shd)
{
	shader_slot_t *s = get_slot(shd);
	if (!s) return;
	glDeleteProgram(s->program);
	s->program = 0;
	s->used = 0;
}

void
lithos_apply_shader(lithos_shader_t shd)
{
	shader_slot_t *s = get_slot(shd);
	glUseProgram(s ? s->program : 0);
}

void
lithos_uniform_int(lithos_shader_t shd, const char *name, int val)
{
	shader_slot_t *s = get_slot(shd);
	GLint loc;
	if (!s) return;
	loc = glGetUniformLocation(s->program, name);
	if (loc >= 0) glUniform1i(loc, val);
}

void
lithos_uniform_float(lithos_shader_t shd, const char *name, float val)
{
	shader_slot_t *s = get_slot(shd);
	GLint loc;
	if (!s) return;
	loc = glGetUniformLocation(s->program, name);
	if (loc >= 0) glUniform1f(loc, val);
}

void
lithos_uniform_vec2(lithos_shader_t shd, const char *name, float x, float y)
{
	shader_slot_t *s = get_slot(shd);
	GLint loc;
	if (!s) return;
	loc = glGetUniformLocation(s->program, name);
	if (loc >= 0) glUniform2f(loc, x, y);
}

void
lithos_uniform_vec3(lithos_shader_t shd, const char *name, float x, float y, float z)
{
	shader_slot_t *s = get_slot(shd);
	GLint loc;
	if (!s) return;
	loc = glGetUniformLocation(s->program, name);
	if (loc >= 0) glUniform3f(loc, x, y, z);
}

void
lithos_uniform_vec4(lithos_shader_t shd, const char *name, float x, float y, float z, float w)
{
	shader_slot_t *s = get_slot(shd);
	GLint loc;
	if (!s) return;
	loc = glGetUniformLocation(s->program, name);
	if (loc >= 0) glUniform4f(loc, x, y, z, w);
}

void
lithos_uniform_mat4(lithos_shader_t shd, const char *name, const float m[16])
{
	shader_slot_t *s = get_slot(shd);
	GLint loc;
	if (!s) return;
	loc = glGetUniformLocation(s->program, name);
	if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, m);
}
