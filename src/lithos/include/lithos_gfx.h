#ifndef LITHOS_GFX_H_
#define LITHOS_GFX_H_

#define LITHOS_MAX_VERTEX_ATTRS 8

/* Pixel formats */
enum lithos_pixel_format {
	LITHOS_PIXFMT_R8,       /* single channel, 8-bit (GL_LUMINANCE on GLES2) */
	LITHOS_PIXFMT_RGB8,     /* 3 channels, 8-bit each */
	LITHOS_PIXFMT_RGBA8,    /* 4 channels, 8-bit each */
};

/* Texture filter modes */
enum lithos_filter {
	LITHOS_FILTER_NEAREST,
	LITHOS_FILTER_LINEAR,
};

/* Buffer usage hints */
enum lithos_usage {
	LITHOS_USAGE_STATIC,
	LITHOS_USAGE_DYNAMIC,
	LITHOS_USAGE_STREAM,
};

/* Primitive types */
enum lithos_primitive {
	LITHOS_PRIM_TRIANGLES,
	LITHOS_PRIM_TRIANGLE_FAN,
	LITHOS_PRIM_TRIANGLE_STRIP,
	LITHOS_PRIM_LINES,
};

/* Opaque resource handles (id == 0 means invalid/null) */
typedef struct { unsigned id; } lithos_shader_t;
typedef struct { unsigned id; } lithos_mesh_t;
typedef struct { unsigned id; } lithos_texture_t;

/* Vertex attribute layout within a vertex stride */
typedef struct {
	int size;       /* number of components: 1, 2, 3, or 4 */
	int offset;     /* byte offset within vertex */
} lithos_vertex_attr_t;

/* Shader creation descriptor */
typedef struct {
	const char *vert_src;
	const char *frag_src;
	/* Attribute names bound to locations 0, 1, 2, ...
	 * Mesh layout[i] maps to attrs[i]. */
	const char *attrs[LITHOS_MAX_VERTEX_ATTRS];
	int num_attrs;
} lithos_shader_desc_t;

/* Mesh creation descriptor */
typedef struct {
	const void *vertices;
	int vertex_count;
	int vertex_stride;  /* bytes per vertex */
	lithos_vertex_attr_t layout[LITHOS_MAX_VERTEX_ATTRS];
	int num_attrs;      /* must match shader's num_attrs */
	const void *indices;        /* NULL = non-indexed */
	int index_count;            /* number of uint16 indices */
	enum lithos_usage usage;
	enum lithos_primitive primitive;
} lithos_mesh_desc_t;

/* Texture creation descriptor */
typedef struct {
	int width, height;
	enum lithos_pixel_format format;
	enum lithos_filter min_filter;
	enum lithos_filter mag_filter;
	const void *data;   /* initial pixel data, or NULL */
} lithos_texture_desc_t;

/* --- Resource lifecycle --- */

lithos_shader_t  lithos_make_shader(const lithos_shader_desc_t *desc);
void             lithos_destroy_shader(lithos_shader_t shd);

lithos_mesh_t    lithos_make_mesh(const lithos_mesh_desc_t *desc);
void             lithos_destroy_mesh(lithos_mesh_t mesh);

lithos_texture_t lithos_make_texture(const lithos_texture_desc_t *desc);
void             lithos_destroy_texture(lithos_texture_t tex);

/* --- Shader operations --- */

void lithos_apply_shader(lithos_shader_t shd);

/* Uniform setters (shader must be applied first via lithos_apply_shader) */
void lithos_uniform_int(lithos_shader_t shd, const char *name, int val);
void lithos_uniform_float(lithos_shader_t shd, const char *name, float val);
void lithos_uniform_vec2(lithos_shader_t shd, const char *name, float x, float y);
void lithos_uniform_vec3(lithos_shader_t shd, const char *name, float x, float y, float z);
void lithos_uniform_vec4(lithos_shader_t shd, const char *name, float x, float y, float z, float w);
void lithos_uniform_mat4(lithos_shader_t shd, const char *name, const float m[16]);

/* --- Texture operations --- */

/* Bind texture to a texture unit (0, 1, 2, ...).
 * Pass a zero-handle to unbind the unit. */
void lithos_bind_texture(lithos_texture_t tex, int unit);

/* Update a sub-region with tightly-packed pixel data. */
void lithos_update_texture(lithos_texture_t tex, int x, int y, int w, int h,
                           const void *data);

/* --- Mesh drawing --- */

void lithos_draw(lithos_mesh_t mesh);
void lithos_draw_range(lithos_mesh_t mesh, int first, int count);

/* --- Texture queries --- */

int lithos_texture_width(lithos_texture_t tex);
int lithos_texture_height(lithos_texture_t tex);

/* --- Image loading (requires stb_image.h) --- */

/* Load texture from a PNG/JPG/BMP/TGA file.
 * Returns zero-handle on failure. */
lithos_texture_t lithos_load_texture(const char *path,
                                     enum lithos_filter min_filter,
                                     enum lithos_filter mag_filter);

/* --- 2D sprite batch --- */

/* Set up 2D orthographic projection for sprite drawing.
 * (x, y) is the top-left corner of the view in world coords,
 * (w, h) is the view size. Y-down coordinate system. */
void lithos_sprite_begin(float x, float y, float w, float h);

/* Draw a texture region as a 2D quad.
 * dst_x/y/w/h: destination rect in world coords.
 * src_x/y/w/h: source rect in pixels (texel coords).
 * If src_w <= 0, draws the full texture. */
void lithos_sprite_draw(lithos_texture_t tex,
                        float dst_x, float dst_y, float dst_w, float dst_h,
                        float src_x, float src_y, float src_w, float src_h);

/* Draw with horizontal flip. */
void lithos_sprite_draw_flip(lithos_texture_t tex,
                             float dst_x, float dst_y, float dst_w, float dst_h,
                             float src_x, float src_y, float src_w, float src_h,
                             int flip_x);

/* Draw with color tint (multiplied with texture color).
 * Color is RGBA 0.0-1.0. */
void lithos_sprite_draw_tinted(lithos_texture_t tex,
                               float dst_x, float dst_y, float dst_w, float dst_h,
                               float src_x, float src_y, float src_w, float src_h,
                               float r, float g, float b, float a);

/* Draw a filled rectangle with the given color.
 * Call between sprite_begin/end. Color is RGBA 0.0-1.0. */
void lithos_sprite_rect(float x, float y, float w, float h,
                        float r, float g, float b, float a);

/* Draw a 1-pixel rectangle outline.
 * Call between sprite_begin/end. Color is RGBA 0.0-1.0. */
void lithos_sprite_rect_lines(float x, float y, float w, float h,
                              float r, float g, float b, float a);

/* Flush pending draws and tear down sprite state. */
void lithos_sprite_end(void);

/* --- Immediate state --- */

void lithos_clear(float r, float g, float b, float a);
void lithos_viewport(int x, int y, int w, int h);

/* --- Palette-indexed framebuffer --- */

typedef struct { unsigned id; } lithos_framebuffer_t;

/* CRT effect mode for blit */
enum lithos_crt_mode {
	LITHOS_CRT_NONE,        /* flat palette lookup, no post-process */
	LITHOS_CRT_SCANLINES,   /* scanlines + slight barrel distortion */
};

typedef struct {
	int width, height;              /* pixel buffer dimensions */
	enum lithos_crt_mode crt;      /* CRT post-process mode */
} lithos_framebuffer_desc_t;

lithos_framebuffer_t lithos_make_framebuffer(const lithos_framebuffer_desc_t *desc);
void                 lithos_destroy_framebuffer(lithos_framebuffer_t fb);

/* Set the 256-entry RGBA palette. Each entry is 0xAABBGGRR (byte order). */
void lithos_framebuffer_palette(lithos_framebuffer_t fb,
                                const unsigned int palette[256]);

/* Lock returns a pointer to the width*height pixel buffer for CPU writes.
 * Pixels are palette indices (0-255). */
unsigned char *lithos_framebuffer_lock(lithos_framebuffer_t fb);

/* Unlock uploads modified pixels to the GPU. */
void lithos_framebuffer_unlock(lithos_framebuffer_t fb);

/* Blit the framebuffer to the current viewport. */
void lithos_framebuffer_blit(lithos_framebuffer_t fb);

#endif /* LITHOS_GFX_H_ */
