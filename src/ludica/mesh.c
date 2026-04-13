#include "ludica_internal.h"
#include "ludica_gfx.h"
#include <GLES2/gl2.h>
#include <string.h>

#define MAX_MESHES 64

typedef struct {
	int used;
	GLuint vbo;
	GLuint ibo;
	int vertex_count;
	int index_count;
	GLenum primitive;
	int num_attrs;
	struct {
		GLint size;
		int offset;
	} attrs[LUD_MAX_VERTEX_ATTRS];
	int vertex_stride;
} mesh_slot_t;

static mesh_slot_t slots[MAX_MESHES];

static int
alloc_slot(void)
{
	int i;
	for (i = 0; i < MAX_MESHES; i++)
		if (!slots[i].used)
			return i;
	return -1;
}

static mesh_slot_t *
get_slot(lud_mesh_t mesh)
{
	if (mesh.id == 0 || mesh.id > MAX_MESHES)
		return NULL;
	mesh_slot_t *s = &slots[mesh.id - 1];
	return s->used ? s : NULL;
}

static GLenum
translate_primitive(enum lud_primitive p)
{
	switch (p) {
	case LUD_PRIM_TRIANGLES:      return GL_TRIANGLES;
	case LUD_PRIM_TRIANGLE_FAN:   return GL_TRIANGLE_FAN;
	case LUD_PRIM_TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
	case LUD_PRIM_LINES:          return GL_LINES;
	default:                         return GL_TRIANGLES;
	}
}

static GLenum
translate_usage(enum lud_usage u)
{
	switch (u) {
	case LUD_USAGE_STATIC:  return GL_STATIC_DRAW;
	case LUD_USAGE_DYNAMIC: return GL_DYNAMIC_DRAW;
	case LUD_USAGE_STREAM:  return GL_STREAM_DRAW;
	default:                   return GL_STATIC_DRAW;
	}
}

lud_mesh_t
lud_make_mesh(const lud_mesh_desc_t *desc)
{
	lud_mesh_t out = {0};
	mesh_slot_t *s;
	GLenum usage;
	int idx, i;

	idx = alloc_slot();
	if (idx < 0) {
		lud_err("mesh pool exhausted");
		return out;
	}

	s = &slots[idx];
	memset(s, 0, sizeof(*s));
	s->used = 1;
	s->vertex_count = desc->vertex_count;
	s->vertex_stride = desc->vertex_stride;
	s->primitive = translate_primitive(desc->primitive);
	s->num_attrs = desc->num_attrs;

	for (i = 0; i < desc->num_attrs && i < LUD_MAX_VERTEX_ATTRS; i++) {
		s->attrs[i].size = desc->layout[i].size;
		s->attrs[i].offset = desc->layout[i].offset;
	}

	usage = translate_usage(desc->usage);

	/* Vertex buffer */
	glGenBuffers(1, &s->vbo);
	glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
	glBufferData(GL_ARRAY_BUFFER,
	             desc->vertex_count * desc->vertex_stride,
	             desc->vertices, usage);

	/* Index buffer (optional) */
	if (desc->indices && desc->index_count > 0) {
		s->index_count = desc->index_count;
		glGenBuffers(1, &s->ibo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->ibo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER,
		             desc->index_count * (int)sizeof(unsigned short),
		             desc->indices, usage);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	glBindBuffer(GL_ARRAY_BUFFER, 0);

	out.id = (unsigned)(idx + 1);
	return out;
}

void
lud_destroy_mesh(lud_mesh_t mesh)
{
	mesh_slot_t *s = get_slot(mesh);
	if (!s) return;
	if (s->vbo) glDeleteBuffers(1, &s->vbo);
	if (s->ibo) glDeleteBuffers(1, &s->ibo);
	memset(s, 0, sizeof(*s));
}

static void
bind_and_draw(mesh_slot_t *s, int first, int count)
{
	int i;

	glBindBuffer(GL_ARRAY_BUFFER, s->vbo);

	for (i = 0; i < s->num_attrs; i++) {
		glVertexAttribPointer(i, s->attrs[i].size, GL_FLOAT, GL_FALSE,
		                      s->vertex_stride,
		                      (const void *)(long)s->attrs[i].offset);
		glEnableVertexAttribArray(i);
	}

	if (s->index_count > 0 && s->ibo) {
		int n = count > 0 ? count : s->index_count;
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->ibo);
		glDrawElements(s->primitive, n, GL_UNSIGNED_SHORT,
		               (const void *)(long)(first * (int)sizeof(unsigned short)));
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	} else {
		int n = count > 0 ? count : s->vertex_count;
		glDrawArrays(s->primitive, first, n);
	}

	for (i = 0; i < s->num_attrs; i++)
		glDisableVertexAttribArray(i);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void
lud_draw(lud_mesh_t mesh)
{
	mesh_slot_t *s = get_slot(mesh);
	if (!s) return;
	bind_and_draw(s, 0, 0);
}

void
lud_draw_range(lud_mesh_t mesh, int first, int count)
{
	mesh_slot_t *s = get_slot(mesh);
	if (!s) return;
	bind_and_draw(s, first, count);
}
