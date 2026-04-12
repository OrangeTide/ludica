#pragma sokol @ctype mat4 hmm_mat4

#pragma sokol @vs vs
in vec4 position;
out vec2 uv0;

void main() {
    gl_Position = position;
    uv0 = position.xy * 0.5 + 0.5;
}
#pragma sokol @end

#pragma sokol @fs fs
uniform sampler2D screentexture;
uniform sampler2D palette;
in vec2 uv0;
layout(location = 0) out vec4 frag_color;

void main(void) {
	frag_color = texture(palette, vec2(texture(screentexture, uv0).r, 0));
}

#pragma sokol @end

#pragma sokol @program palette vs fs
