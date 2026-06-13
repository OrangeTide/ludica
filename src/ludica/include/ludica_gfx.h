#ifndef LUDICA_GFX_H_
#define LUDICA_GFX_H_

#define LUD_MAX_VERTEX_ATTRS 8

/* Pixel formats */
enum lud_pixel_format {
	LUD_PIXFMT_R8,       /* single channel, 8-bit (GL_LUMINANCE on GLES2) */
	LUD_PIXFMT_RGB8,     /* 3 channels, 8-bit each */
	LUD_PIXFMT_RGBA8,    /* 4 channels, 8-bit each */
	LUD_PIXFMT_SRGB8,    /* 3 channels, sRGB (GLES3 only, falls back to RGB8) */
	LUD_PIXFMT_SRGBA8,   /* 4 channels, sRGB+alpha (GLES3 only, falls back to RGBA8) */
	LUD_PIXFMT_RGBA16F,  /* 4 channels, 16-bit float (GLES3 only) */
	LUD_PIXFMT_RG16UI,   /* 2 channels, 16-bit unsigned int (GLES3 only) */
};

/* Texture filter modes */
enum lud_filter {
	LUD_FILTER_NEAREST,
	LUD_FILTER_LINEAR,
};

/* Buffer usage hints */
enum lud_usage {
	LUD_USAGE_STATIC,
	LUD_USAGE_DYNAMIC,
	LUD_USAGE_STREAM,
};

/* Primitive types */
enum lud_primitive {
	LUD_PRIM_TRIANGLES,
	LUD_PRIM_TRIANGLE_FAN,
	LUD_PRIM_TRIANGLE_STRIP,
	LUD_PRIM_LINES,
};

/* Depth comparison function */
enum lud_depth_func {
	LUD_DEPTH_LESS,    /* pass if incoming depth < stored (default) */
	LUD_DEPTH_LEQUAL,  /* pass if incoming depth <= stored */
	LUD_DEPTH_ALWAYS,  /* always pass (depth test effectively off) */
};

/* Face culling mode */
enum lud_cull {
	LUD_CULL_NONE,   /* draw all faces */
	LUD_CULL_BACK,   /* cull back faces (CCW front, the default winding) */
	LUD_CULL_FRONT,  /* cull front faces */
};

/* Blend mode (source factor, destination factor) */
enum lud_blend {
	LUD_BLEND_NONE,   /* blending off; source replaces destination */
	LUD_BLEND_ALPHA,  /* src_alpha, 1-src_alpha (standard transparency) */
	LUD_BLEND_ADD,    /* src_alpha, 1 (additive: glows, fire, light) */
};

/* Triangle winding that counts as front-facing (pairs with lud_cull) */
enum lud_winding {
	LUD_WINDING_CCW,  /* counter-clockwise is front (default) */
	LUD_WINDING_CW,   /* clockwise is front */
};

/* Opaque resource handles (id == 0 means invalid/null) */
typedef struct { unsigned id; } lud_shader_t;
typedef struct { unsigned id; } lud_mesh_t;
typedef struct { unsigned id; } lud_texture_t;

/* Vertex attribute layout within a vertex stride */
typedef struct {
	int size;       /* number of components: 1, 2, 3, or 4 */
	int offset;     /* byte offset within vertex */
} lud_vertex_attr_t;

/* Shader creation descriptor */
typedef struct {
	const char *vert_src;
	const char *frag_src;
	/* Attribute names bound to locations 0, 1, 2, ...
	 * Mesh layout[i] maps to attrs[i]. */
	const char *attrs[LUD_MAX_VERTEX_ATTRS];
	int num_attrs;
} lud_shader_desc_t;

/* Mesh creation descriptor */
typedef struct {
	const void *vertices;
	int vertex_count;
	int vertex_stride;  /* bytes per vertex */
	lud_vertex_attr_t layout[LUD_MAX_VERTEX_ATTRS];
	int num_attrs;      /* must match shader's num_attrs */
	const void *indices;        /* NULL = non-indexed */
	int index_count;            /* number of uint16 indices */
	enum lud_usage usage;
	enum lud_primitive primitive;
} lud_mesh_desc_t;

/* Texture creation descriptor */
typedef struct {
	int width, height;
	enum lud_pixel_format format;
	enum lud_filter min_filter;
	enum lud_filter mag_filter;
	const void *data;   /* initial pixel data, or NULL */
} lud_texture_desc_t;

/* Texture array creation descriptor (GLES3 only; GL_TEXTURE_2D_ARRAY) */
typedef struct {
	int width, height;
	int num_layers;
	enum lud_pixel_format format;
	enum lud_filter min_filter;
	enum lud_filter mag_filter;
} lud_texture_array_desc_t;

/* --- Resource lifecycle --- */

lud_shader_t  lud_make_shader(const lud_shader_desc_t *desc);
void             lud_destroy_shader(lud_shader_t shd);

lud_mesh_t    lud_make_mesh(const lud_mesh_desc_t *desc);
void             lud_destroy_mesh(lud_mesh_t mesh);

lud_texture_t lud_make_texture(const lud_texture_desc_t *desc);
void             lud_destroy_texture(lud_texture_t tex);

/* Texture array (GLES3 only; returns zero-handle if GLES2) */
lud_texture_t lud_make_texture_array(const lud_texture_array_desc_t *desc);
void lud_texture_array_set_layer(lud_texture_t arr, int layer, const void *data);

/* --- Shader operations --- */

void lud_apply_shader(lud_shader_t shd);

/* Uniform setters (shader must be applied first via lud_apply_shader) */
void lud_uniform_int(lud_shader_t shd, const char *name, int val);
void lud_uniform_float(lud_shader_t shd, const char *name, float val);
void lud_uniform_vec2(lud_shader_t shd, const char *name, float x, float y);
void lud_uniform_vec3(lud_shader_t shd, const char *name, float x, float y, float z);
void lud_uniform_vec4(lud_shader_t shd, const char *name, float x, float y, float z, float w);
void lud_uniform_mat4(lud_shader_t shd, const char *name, const float m[16]);

/* --- Texture operations --- */

/* Bind texture to a texture unit (0, 1, 2, ...).
 * Pass a zero-handle to unbind the unit. */
void lud_bind_texture(lud_texture_t tex, int unit);

/* Update a sub-region with tightly-packed pixel data. */
void lud_update_texture(lud_texture_t tex, int x, int y, int w, int h,
                           const void *data);

/* --- Mesh drawing --- */

void lud_draw(lud_mesh_t mesh);
void lud_draw_range(lud_mesh_t mesh, int first, int count);

/* --- Texture queries --- */

int lud_texture_width(lud_texture_t tex);
int lud_texture_height(lud_texture_t tex);

/* --- Image loading (requires stb_image.h) --- */

/* Load texture from a PNG/JPG/BMP/TGA file.
 * Returns zero-handle on failure. */
lud_texture_t lud_load_texture(const char *path,
                                     enum lud_filter min_filter,
                                     enum lud_filter mag_filter);

/* Load texture with sRGB internal format (GLES3 only, falls back to linear).
 * Use for color/albedo maps; the GPU decodes gamma on sample. */
lud_texture_t lud_load_texture_srgb(const char *path,
                                           enum lud_filter min_filter,
                                           enum lud_filter mag_filter);

/* --- 2D sprite batch --- */

/* Set up 2D orthographic projection for sprite drawing.
 * (x, y) is the top-left corner of the view in world coords,
 * (w, h) is the view size. Y-down coordinate system. */
void lud_sprite_begin(float x, float y, float w, float h);

/* Draw a texture region as a 2D quad.
 * dst_x/y/w/h: destination rect in world coords.
 * src_x/y/w/h: source rect in pixels (texel coords).
 * If src_w <= 0, draws the full texture. */
void lud_sprite_draw(lud_texture_t tex,
                        float dst_x, float dst_y, float dst_w, float dst_h,
                        float src_x, float src_y, float src_w, float src_h);

/* Draw with horizontal flip. */
void lud_sprite_draw_flip(lud_texture_t tex,
                             float dst_x, float dst_y, float dst_w, float dst_h,
                             float src_x, float src_y, float src_w, float src_h,
                             int flip_x);

/* Draw with color tint (multiplied with texture color).
 * Color is RGBA 0.0-1.0. */
void lud_sprite_draw_tinted(lud_texture_t tex,
                               float dst_x, float dst_y, float dst_w, float dst_h,
                               float src_x, float src_y, float src_w, float src_h,
                               float r, float g, float b, float a);

/* Draw a filled rectangle with the given color.
 * Call between sprite_begin/end. Color is RGBA 0.0-1.0. */
void lud_sprite_rect(float x, float y, float w, float h,
                        float r, float g, float b, float a);

/* Draw a 1-pixel rectangle outline.
 * Call between sprite_begin/end. Color is RGBA 0.0-1.0. */
void lud_sprite_rect_lines(float x, float y, float w, float h,
                              float r, float g, float b, float a);

/* Flush pending draws and tear down sprite state. */
void lud_sprite_end(void);

/* --- Immediate state --- */

void lud_clear(float r, float g, float b, float a);
void lud_viewport(int x, int y, int w, int h);

/* --- Render state ---
 *
 * Thin wrappers over the GL render state apps need for 3D drawing.
 * These keep programs off raw <GLES2/gl2.h> so the GLES2/GLES3/WebGL
 * differences stay inside ludica. State is global GL session state and
 * persists until changed. The sprite batch sets its own blend state on
 * begin and clears it on end, so set lud_blend() for custom mesh draws
 * outside a sprite batch. */

/* Enable or disable the depth test. */
void lud_depth_test(int enable);

/* Set the depth comparison function (only meaningful while depth test on). */
void lud_depth_func(enum lud_depth_func fn);

/* Set face culling mode (LUD_CULL_NONE disables culling). */
void lud_cull(enum lud_cull mode);

/* Choose which winding is front-facing (for lud_cull). Default CCW. */
void lud_front_face(enum lud_winding w);

/* Enable or disable writing to the depth buffer. Disable (0) for a
 * translucent pass: keep the depth test on so geometry is occluded by
 * solid walls, but stop transparent surfaces from writing depth and
 * hiding each other. Re-enable (1) before the next opaque pass. */
void lud_depth_mask(int write);

/* Set the blend mode (LUD_BLEND_NONE disables blending). */
void lud_blend(enum lud_blend mode);

/* Restrict drawing to a rectangle; fragments outside are discarded.
 * Coordinates are window pixels with the origin at the bottom-left,
 * the same space as lud_viewport. lud_scissor_off() removes it. */
void lud_scissor(int x, int y, int w, int h);
void lud_scissor_off(void);

/* Read back a rectangle of the color buffer as RGBA8 (w*h*4 bytes into
 * rgba). (x, y) is the TOP-LEFT of the region in window pixels with a
 * top-left origin (same convention as lud_mouse_pos); rows are returned
 * top-first. For 3D mouse picking, render each pickable object in a
 * unique color, then read the pixel under the cursor:
 *     lud_read_pixels(mx, my, 1, 1, &rgba);
 * and map the color back to an object id. GLES can read back only the
 * color buffer, not depth, so picking is color-id based rather than
 * depth-unproject based. */
void lud_read_pixels(int x, int y, int w, int h, void *rgba);

/* Flush queued GL commands to the driver (does not swap buffers). */
void lud_flush(void);

/* --- Loading progress --- */

/* Draw a progress bar and swap buffers.  Call during init() or frame()
 * while loading assets.  step/total define progress (0..total).
 * label is optional (may be NULL). */
void lud_draw_progress(int step, int total, const char *label);

/* --- Palette-indexed framebuffer --- */

typedef struct { unsigned id; } lud_framebuffer_t;

/* CRT effect mode for blit */
enum lud_crt_mode {
	LUD_CRT_NONE,        /* flat palette lookup, no post-process */
	LUD_CRT_SCANLINES,   /* scanlines + slight barrel distortion */
};

typedef struct {
	int width, height;              /* pixel buffer dimensions */
	enum lud_crt_mode crt;      /* CRT post-process mode */
} lud_framebuffer_desc_t;

lud_framebuffer_t lud_make_framebuffer(const lud_framebuffer_desc_t *desc);
void                 lud_destroy_framebuffer(lud_framebuffer_t fb);

/* Set the 256-entry RGBA palette. Each entry is 0xAABBGGRR (byte order). */
void lud_framebuffer_palette(lud_framebuffer_t fb,
                                const unsigned int palette[256]);

/* Lock returns a pointer to the width*height pixel buffer for CPU writes.
 * Pixels are palette indices (0-255). */
unsigned char *lud_framebuffer_lock(lud_framebuffer_t fb);

/* Unlock uploads modified pixels to the GPU. */
void lud_framebuffer_unlock(lud_framebuffer_t fb);

/* Blit the framebuffer to the current viewport. */
void lud_framebuffer_blit(lud_framebuffer_t fb);

#endif /* LUDICA_GFX_H_ */
