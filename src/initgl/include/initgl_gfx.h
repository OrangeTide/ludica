#ifndef INITGL_GFX_H_
#define INITGL_GFX_H_

#define INITGL_MAX_VERTEX_ATTRS 8

/* Pixel formats */
enum initgl_pixel_format {
	INITGL_PIXFMT_R8,       /* single channel, 8-bit (GL_LUMINANCE on GLES2) */
	INITGL_PIXFMT_RGB8,     /* 3 channels, 8-bit each */
	INITGL_PIXFMT_RGBA8,    /* 4 channels, 8-bit each */
};

/* Texture filter modes */
enum initgl_filter {
	INITGL_FILTER_NEAREST,
	INITGL_FILTER_LINEAR,
};

/* Buffer usage hints */
enum initgl_usage {
	INITGL_USAGE_STATIC,
	INITGL_USAGE_DYNAMIC,
	INITGL_USAGE_STREAM,
};

/* Primitive types */
enum initgl_primitive {
	INITGL_PRIM_TRIANGLES,
	INITGL_PRIM_TRIANGLE_FAN,
	INITGL_PRIM_TRIANGLE_STRIP,
	INITGL_PRIM_LINES,
};

/* Opaque resource handles (id == 0 means invalid/null) */
typedef struct { unsigned id; } initgl_shader_t;
typedef struct { unsigned id; } initgl_mesh_t;
typedef struct { unsigned id; } initgl_texture_t;

/* Vertex attribute layout within a vertex stride */
typedef struct {
	int size;       /* number of components: 1, 2, 3, or 4 */
	int offset;     /* byte offset within vertex */
} initgl_vertex_attr_t;

/* Shader creation descriptor */
typedef struct {
	const char *vert_src;
	const char *frag_src;
	/* Attribute names bound to locations 0, 1, 2, ...
	 * Mesh layout[i] maps to attrs[i]. */
	const char *attrs[INITGL_MAX_VERTEX_ATTRS];
	int num_attrs;
} initgl_shader_desc_t;

/* Mesh creation descriptor */
typedef struct {
	const void *vertices;
	int vertex_count;
	int vertex_stride;  /* bytes per vertex */
	initgl_vertex_attr_t layout[INITGL_MAX_VERTEX_ATTRS];
	int num_attrs;      /* must match shader's num_attrs */
	const void *indices;        /* NULL = non-indexed */
	int index_count;            /* number of uint16 indices */
	enum initgl_usage usage;
	enum initgl_primitive primitive;
} initgl_mesh_desc_t;

/* Texture creation descriptor */
typedef struct {
	int width, height;
	enum initgl_pixel_format format;
	enum initgl_filter min_filter;
	enum initgl_filter mag_filter;
	const void *data;   /* initial pixel data, or NULL */
} initgl_texture_desc_t;

/* --- Resource lifecycle --- */

initgl_shader_t  initgl_make_shader(const initgl_shader_desc_t *desc);
void             initgl_destroy_shader(initgl_shader_t shd);

initgl_mesh_t    initgl_make_mesh(const initgl_mesh_desc_t *desc);
void             initgl_destroy_mesh(initgl_mesh_t mesh);

initgl_texture_t initgl_make_texture(const initgl_texture_desc_t *desc);
void             initgl_destroy_texture(initgl_texture_t tex);

/* --- Shader operations --- */

void initgl_apply_shader(initgl_shader_t shd);

/* Uniform setters (shader must be applied first via initgl_apply_shader) */
void initgl_uniform_int(initgl_shader_t shd, const char *name, int val);
void initgl_uniform_float(initgl_shader_t shd, const char *name, float val);
void initgl_uniform_vec2(initgl_shader_t shd, const char *name, float x, float y);
void initgl_uniform_vec3(initgl_shader_t shd, const char *name, float x, float y, float z);
void initgl_uniform_vec4(initgl_shader_t shd, const char *name, float x, float y, float z, float w);
void initgl_uniform_mat4(initgl_shader_t shd, const char *name, const float m[16]);

/* --- Texture operations --- */

/* Bind texture to a texture unit (0, 1, 2, ...).
 * Pass a zero-handle to unbind the unit. */
void initgl_bind_texture(initgl_texture_t tex, int unit);

/* Update a sub-region with tightly-packed pixel data. */
void initgl_update_texture(initgl_texture_t tex, int x, int y, int w, int h,
                           const void *data);

/* --- Mesh drawing --- */

void initgl_draw(initgl_mesh_t mesh);
void initgl_draw_range(initgl_mesh_t mesh, int first, int count);

/* --- Immediate state --- */

void initgl_clear(float r, float g, float b, float a);
void initgl_viewport(int x, int y, int w, int h);

/* --- Palette-indexed framebuffer --- */

typedef struct { unsigned id; } initgl_framebuffer_t;

/* CRT effect mode for blit */
enum initgl_crt_mode {
	INITGL_CRT_NONE,        /* flat palette lookup, no post-process */
	INITGL_CRT_SCANLINES,   /* scanlines + slight barrel distortion */
};

typedef struct {
	int width, height;              /* pixel buffer dimensions */
	enum initgl_crt_mode crt;      /* CRT post-process mode */
} initgl_framebuffer_desc_t;

initgl_framebuffer_t initgl_make_framebuffer(const initgl_framebuffer_desc_t *desc);
void                 initgl_destroy_framebuffer(initgl_framebuffer_t fb);

/* Set the 256-entry RGBA palette. Each entry is 0xAABBGGRR (byte order). */
void initgl_framebuffer_palette(initgl_framebuffer_t fb,
                                const unsigned int palette[256]);

/* Lock returns a pointer to the width*height pixel buffer for CPU writes.
 * Pixels are palette indices (0-255). */
unsigned char *initgl_framebuffer_lock(initgl_framebuffer_t fb);

/* Unlock uploads modified pixels to the GPU. */
void initgl_framebuffer_unlock(initgl_framebuffer_t fb);

/* Blit the framebuffer to the current viewport. */
void initgl_framebuffer_blit(initgl_framebuffer_t fb);

#endif /* INITGL_GFX_H_ */
