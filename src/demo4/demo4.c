#include "ludica.h"
#include "gamepad.h"
#include "log.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define USE_GLES2 1

#define OK (0)
#define ERR (-1)

/**********************************************************************/

static int up_key, down_key, left_key, right_key, fire_key[2];

struct shader_info {
	GLuint program;
	GLint attr_vertex;
	GLint attr_multitexcoord0;
	GLint unif_color;
	GLint unif_palette;
	GLint unif_texture;
	GLint unif_scale;
};

struct screen {
	struct shader_info shader;
	GLuint buf;
	GLuint palette_tex;
	GLuint palette_buf[256];
	GLubyte *data;
	GLuint tex;
	GLsizei width;
	GLsizei height;
};

struct sprite_instance {
	GLfloat x, y;
};

struct sprite_group {
	struct shader_info shader;
	GLuint quad_buf;
	unsigned count;
	// TODO: GLuint tex;
	// TODO: GLsizei width;
	// TODO: GLsizei height;
	struct sprite_instance *instance;
};

struct demo4_state {
	unsigned tick;
	struct screen screen;
	struct sprite_group sprite_group;
} state;

/**********************************************************************/

static int
compile_shader(GLuint *program_out, const GLchar *vert_source, const GLchar *frag_source)
{
	lud_gl_check();

	*program_out = 0; /* initialize assuming failure */

	// vertex shader
	GLuint vshader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vshader, 1, &vert_source, 0);
	glCompileShader(vshader);

	lud_gl_check();

	// fragment shader
	GLuint fshader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fshader, 1, &frag_source, 0);
	glCompileShader(fshader);

	GLuint shader_parts[] = { vshader, fshader };

	lud_gl_check();

	unsigned i;
	for (i = 0; i < sizeof(shader_parts) / sizeof(*shader_parts); i++) {
		GLint compile_status = GL_TRUE;
		glGetShaderiv(shader_parts[i], GL_COMPILE_STATUS, &compile_status);
		lud_gl_check();
		if (compile_status != GL_TRUE) {
			char info[256] = "Unknown";
			glGetShaderInfoLog(shader_parts[i], sizeof(info), NULL, info);
			log_error("GL shader #%u compile failure:%s", i, info);
			lud_gl_check();
			return ERR;
		}
	}

	lud_gl_check();

	GLuint program = glCreateProgram();
	assert(program != 0);
	glAttachShader(program, vshader);
	glAttachShader(program, fshader);
	glLinkProgram(program);

	GLint link_status;
	glGetProgramiv(program, GL_LINK_STATUS, &link_status);
	lud_gl_check();
	if (link_status != GL_TRUE) {
		char info[256] = "Unknown";
		glGetProgramInfoLog(program, sizeof(info), NULL, info);
		log_error("GL program link failure:%s", info);
		lud_gl_check();
		return ERR;
	}

	glDeleteShader(vshader);
	glDeleteShader(fshader);

	lud_gl_check();

	*program_out = program;

	return OK;
}

static int
load_attributes(GLuint program, const char **attr_names, GLint *attr_out, unsigned count)
{
	int result = OK;
	unsigned i;
	for (i = 0; i < count; i++) {
		GLint attr = glGetAttribLocation(program, attr_names[i]);
		if (attr < 0) {
			log_error("no %s attribute", attr_names[i]);
			result = ERR;
		}

		attr_out[i] = attr;
	}

	return result;
}

static int
load_uniforms(GLuint program, const char **unif_names, GLint *unif_out, unsigned count)
{
	int result = OK;
	unsigned i;
	for (i = 0; i < count; i++) {
		GLint unif = glGetUniformLocation(program, unif_names[i]);
		if (unif < 0) {
			log_error("no %s uniform", unif_names[i]);
			result = ERR;
		}

		unif_out[i] = unif;
	}

	return result;
}

/* initialize 256 color palette */
static void
palette_init(GLuint palette[])
{
	unsigned i;
	unsigned char r, g, b;
	GLuint c;

	/* traditional TTL RGB : colors 0~15 */
	i = 0;
	palette[i++] = 0x000000; // 0 - black
	palette[i++] = 0x800000; // 1 - red
	palette[i++] = 0x008000; // 2 - green
	palette[i++] = 0x808000; // 3 - brown
	palette[i++] = 0x000080; // 4 - blue
	palette[i++] = 0x800080; // 5 - magenta
	palette[i++] = 0x008080; // 6 - cyan
	palette[i++] = 0xc0c0c0; // 7 - white
	/* high-intensity (bright mode) */
	palette[i++] = 0x808080; // 8 - dark grey
	palette[i++] = 0xff0000; // 9 - bright red
	palette[i++] = 0x00ff00; // 10 - bright green
	palette[i++] = 0xffff00; // 11 - yellow
	palette[i++] = 0x0000ff; // 12 - bright blue
	palette[i++] = 0xff00ff; // 13 - bright magenta
	palette[i++] = 0x00ffff; // 14 - bright cyan
	palette[i++] = 0xffffff; // 15 - bright white

	/* RGB 6x6x6 color cube : colors 16~231 */
	for (; i < 232; i++) {
		unsigned n = i - 16;

		b = n % 6;
		g = n / 6 % 6;
		r = n / 36 % 6;

		c = r * 255 / 5;
		c <<= 8;
		c |= g * 255 / 5;
		c <<= 8;
		c |= b * 255 / 5;

		palette[i] = c;
	}

	/* grey scale : colors 232~255 */
	for (; i < 256; i++) {
		unsigned n = i - 232;
		/* 08, 12, 1c, 26, ... d0, da, e4, ee */
		c = 8 + n * 230 / 23;
		palette[i] = c | (c << 8) | (c << 16);
	}
}

static int
screen_update(struct screen *screen, unsigned left, unsigned top, unsigned right, unsigned bottom)
{
	GLsizei width = right - left + 1;
	GLsizei height = bottom - top + 1;

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, screen->tex);
	// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	assert(screen->data != NULL);
#ifdef USE_GLES2 // GLES 2
	glTexSubImage2D(GL_TEXTURE_2D, 0, left, top, width, height, GL_LUMINANCE, GL_UNSIGNED_BYTE, screen->data);
#else // OpenGL 3+
	// TODO: untested code path!
	const GLint one_byte_swizzle[] = { GL_RED, GL_RED, GL_RED, GL_ONE };
	glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, one_byte_swizzle);
	glTexSubImage2D(GL_TEXTURE_2D, 0, left, top, width, height, GL_RED, GL_UNSIGNED_BYTE, screen->data);
#endif
	lud_gl_check();

	return OK;
}

static void
screen_update_full(struct screen *screen)
{
	screen_update(screen, 0, 0, screen->width - 1, screen->height - 1);
}

static void
screen_done(struct screen *screen)
{
	if (screen->shader.program) {
		glUseProgram(0);
		glDeleteProgram(screen->shader.program);
		screen->shader.program = 0;
	}

	if (screen->buf) {
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glDeleteBuffers(1, &screen->buf);
		screen->buf = 0;
	}
}

static int
screen_init(struct screen *screen, int screen_width, int screen_height)
{
	/* vertex shader source */
	const GLchar vert_source[] = {
		"#version 100\n" // GLSL ES 1.00
		"precision mediump float;\n"
		"attribute vec4 vertex;\n"
		// "attribute vec4 AttrMultiTexCoord0;\n"
		"varying vec2 VaryingTexCoord0;\n"
		"void main(void) {\n"
		// "  VaryingTexCoord0 = AttrMultiTexCoord0.xy;\n"
		"  VaryingTexCoord0 = vertex.xy * 0.5 + 0.5;\n"
		"  gl_Position = vertex * 0.5;\n"
		"}\n"
	};
	/* fragment shader source */
	const GLchar frag_source[] = {
		"#version 100\n" // GLSL ES 1.00
		"precision mediump float;\n"
		"uniform sampler2D texture;\n"
		"uniform sampler2D palette;\n"
		"varying vec2 VaryingTexCoord0;\n"
		"void main(void) {\n"
		"  gl_FragColor = texture2D(palette, vec2(texture2D(texture, VaryingTexCoord0).r, 0));\n"
		// "  gl_FragColor = texture2D(palette, VaryingTexCoord0);\n"
		"}\n"
	};

	if (compile_shader(&screen->shader.program, vert_source, frag_source) != OK) {
		log_error("GL shader compile failure");
		return ERR;
	}
	assert(screen->shader.program != 0);

	lud_gl_check();

	load_attributes(screen->shader.program, (const char*[]){ "vertex", "AttrMultiTexCoord0" }, &screen->shader.attr_vertex, 2);
	if (screen->shader.attr_vertex < 0) {
		log_error("GL shader missing vertex attribute");
		return ERR;
	}
	load_uniforms(screen->shader.program, (const char*[]){ "color", "palette", "texture" }, &screen->shader.unif_color, 3);

	lud_gl_check();

	glUseProgram(screen->shader.program);

	lud_gl_check();

	screen->width = screen_width;
	screen->height = screen_height;
	screen->data = malloc(screen_width * screen_height);
	if (!screen->data) {
		log_error("Unable to allocate screen data:%s", strerror(errno));
		return ERR;
	}

	memset(screen->data, 0, screen_width * screen_height);

	/*** Vertex Buffer ***/

	static const GLfloat vertex_data[] = {
		-1.0, -1.0, 1.0, 1.0,
		1.0, -1.0, 1.0, 1.0,
		1.0, 1.0, 1.0, 1.0,
		-1.0, 1.0, 1.0, 1.0
	};

	GLuint buf = 0;
	glGenBuffers(1, &buf);
	glBindBuffer(GL_ARRAY_BUFFER, buf);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_data), vertex_data, GL_STATIC_DRAW);
	glVertexAttribPointer(screen->shader.attr_vertex, 4, GL_FLOAT, 0, 16, 0);
	glEnableVertexAttribArray(screen->shader.attr_vertex);
	screen->buf = buf;

	lud_gl_check();

	/*** Textures ***/

	// Screen texture
	GLuint screen_tex = 0;
	glGenTextures(1, &screen_tex);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, screen_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, screen_width, screen_height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
	screen->tex = screen_tex;

	// Palette texture
	/* R/G/B */
	GLuint palette_tex = 0;
	glActiveTexture(GL_TEXTURE1);
	glGenTextures(1, &palette_tex);
	glBindTexture(GL_TEXTURE_2D, palette_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
#ifndef USE_GLES2 // GLES 3+ / OpenGL 3+
	// TODO: untested code path!
	const GLint lsbred_no_alpha_swizzle[] = { GL_RED, GL_GREEN, GL_BLUE, GL_ONE };
	glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, lsb_no_alpha_swizzle);
#endif
	screen->palette_tex = palette_tex;

	palette_init(screen->palette_buf);
	// TODO: share this in palette_update()
	glBindTexture(GL_TEXTURE_2D, screen->palette_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, screen->palette_buf);

	lud_gl_check();

	return OK;
}

static void
screen_paint(struct screen *screen)
{
	glBindBuffer(GL_ARRAY_BUFFER, screen->buf);
	glUseProgram(screen->shader.program);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, screen->tex);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, screen->palette_tex);

	if (screen->shader.unif_color >= 0) {
		glUniform4f(screen->shader.unif_color, 0.5, 0.5, 0.8, 1.0);
	}
	if (screen->shader.unif_palette >= 0) {
		glUniform1i(screen->shader.unif_palette, 1); // palette_tex is GL_TEXTURE1
	}
	if (screen->shader.unif_texture >= 0) {
		glUniform1i(screen->shader.unif_texture, 0); // screen_tex is GL_TEXTURE0
	}

	lud_gl_check();

	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	/* clean up state */

	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);

	lud_gl_check();
}

static int
sprite_group_init(struct sprite_group *group, unsigned count)
{
	group->count = count;
	group->instance = malloc(count * sizeof(*group->instance));
	if (!group->instance) {
		group->count = 0;
		return ERR;
	}

	unsigned i;
	for (i = 0; i < count; i++) {
		struct sprite_instance *sprite = group->instance + i;

		// initialize each sprite
		*sprite = (struct sprite_instance){};
	}

	/* vertex shader source */
	const GLchar vert_source[] = {
		"#version 100\n" // GLSL ES 1.00
		"precision mediump float;\n"
		"attribute vec4 vertex;\n"
		"uniform vec2 scale;\n"
		"void main(void) {\n"
		"  gl_Position = vertex * vec4(scale, 0.0, 1.0);\n"
		"}\n"
	};
	/* fragment shader source */
	const GLchar frag_source[] = {
		"#version 100\n" // GLSL ES 1.00
		"precision mediump float;\n"
		"void main(void) {\n"
		"  gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
		"}\n"
	};

	if (compile_shader(&group->shader.program, vert_source, frag_source) != OK) {
		return ERR;
	}
	assert(group->shader.program != 0);

	lud_gl_check();

	load_attributes(group->shader.program, (const char*[]){ "vertex", }, &group->shader.attr_vertex, 1);
	if (group->shader.attr_vertex < 0) {
		log_error("GL shader missing vertex attribute");
		return ERR;
	}

	load_uniforms(group->shader.program, (const char*[]){ "scale" }, &group->shader.unif_scale, 1);

	lud_gl_check();

	glUseProgram(group->shader.program);

	lud_gl_check();

	/*** Vertex Buffer ***/

	static const GLfloat quad_data[] = {
		-1.0, -1.0, 1.0, 1.0,
		1.0, -1.0, 1.0, 1.0,
		1.0, 1.0, 1.0, 1.0,
		-1.0, 1.0, 1.0, 1.0
	};

	GLuint quad_buf = 0;
	glGenBuffers(1, &quad_buf);
	glBindBuffer(GL_ARRAY_BUFFER, quad_buf);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad_data), quad_data, GL_STATIC_DRAW);
	glVertexAttribPointer(group->shader.attr_vertex, 4, GL_FLOAT, 0, 16, 0);
	glEnableVertexAttribArray(group->shader.attr_vertex);
	group->quad_buf = quad_buf;

	lud_gl_check();

	/*** Textures ***/

	// TODO: implement a sprite sheet texture

	return OK;
	return OK;
}

static void
sprite_group_paint(struct sprite_group *group)
{
	unsigned count = group->count;
	unsigned i;

	glBindBuffer(GL_ARRAY_BUFFER, group->quad_buf);
	glUseProgram(group->shader.program);

#if 0 // TODO: implement sprite sheet texture
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, group->tex);

	if (screen->shader.unif_texture >= 0) {
		glUniform1i(screen->shader.unif_texture, 0); // screen_tex is GL_TEXTURE0
	}
#endif
	if (group->shader.unif_scale >= 0) {
		glUniform2f(group->shader.unif_scale, 16.0 / 320, 16.0 / 240);
	}

	// dispatch draw calls for sprites

	for (i = 0; i < count; i++) {
		// struct sprite_instance *sprite = group->instance + i;

		lud_gl_check();

		// TODO: apply instance and coordinate information

		glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	}

	lud_gl_check();

	/* clean up state */

	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);

	lud_gl_check();
}

/* load shader, initialize GL state */
static int
my_gl_init(void)
{
	if (screen_init(&state.screen, 320, 240) != OK) {
		log_error("screen init failed");
		return ERR;
	}

	screen_update_full(&state.screen);

	lud_gl_check();

	sprite_group_init(&state.sprite_group, 1);

	lud_gl_check();

	return OK;
}

static void
my_gl_done(void)
{
	screen_done(&state.screen);
}

static void
my_key_init(void)
{
	up_key = lookup_key("Up");
	down_key = lookup_key("Down");
	left_key = lookup_key("Left");
	right_key = lookup_key("Right");
	fire_key[0] = lookup_key("z");
	fire_key[1] = lookup_key("Space");

	log_debug("up=%d down=%d left=%d right=%d fire=[%d %d]", up_key, down_key, left_key, right_key, fire_key[0], fire_key[1]);
}

static void
paint(void)
{
	glClearColor(1.0, 1.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	screen_paint(&state.screen);

	sprite_group_paint(&state.sprite_group);

	glFlush();

	lud_gl_check();
}

void
reshape(int width, int height)
{
	glViewport(0, 0, width, height);
	// TODO: scale and center the input to fill the output
	// in_x_ofs = 0;
	// in_y_ofs = 0;
	// in_width = width * scale;
	// in_height = height * scale;
}

static void
keyevent(int key)
{
	if (key == up_key) {
		log_debug("Up");
	} else if (key == down_key) {
		log_debug("Down");
	} else if (key == left_key) {
		log_debug("Left");
	} else if (key == right_key) {
		log_debug("Right");
	} else if (key == fire_key[0] || key == fire_key[1]) {
		log_debug("Fire!");
	}
}

static void
inputevent(const char *s, unsigned len)
{
	log_debug("Typing Event: \"%.*s\"", len, s);
}

static struct window_callback_functions callbacks = {
	.paint = paint,
	.reshape = reshape,
	.keyevent = keyevent,
	.typingkeyboard = inputevent,
};

static void
animate(void)
{
	unsigned tick = state.tick++;

	GLsizei x, y;
	unsigned row_bytes = state.screen.width;
	GLubyte *screen_data = state.screen.data;

	if (!screen_data) { return; }

	for (y = 0; y < state.screen.height; y++) {
		GLubyte *row = state.screen.data + y * row_bytes;
		for (x = 0; x < state.screen.width; x++) {
			row[x] = ((x + (y ^ 12) + tick) % 16) + 232;
		}
	}

	screen_update_full(&state.screen);
}

int
main()
{
	if (display_init() != LUD_OK) {
		return 1;
	}
	if (window_new(&callbacks) != LUD_OK) {
		return 1;
	}

	my_key_init();
	gamepad_init();

	if (my_gl_init() != OK) {
		return 1;
	}

	while (!terminate_flag) {
		process_events();
		gamepad_poll();
		animate();
		paint_all();
	}

	my_gl_done();
	gamepad_done();
	display_done();

	return 0;
}
