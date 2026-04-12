#pragma sokol @ctype mat4 hmm_mat4

#pragma sokol @vs vs
in vec4 position;
in vec4 color0;
in vec2 texcoord0;

layout(location = 0) out vec4 color;
layout(location = 1) out vec2 tex;

void main() {
    gl_Position = position;
    tex = texcoord0;
    color = color0;
}
#pragma sokol @end

#pragma sokol @fs fs
uniform sampler2D texsampler;
layout(location = 0) in vec4 color;
layout(location = 1) in vec2 tex;
layout(location = 0) out vec4 frag_color;

void main()
{
	frag_color = texture(texsampler, tex) * color;
}
#pragma sokol @end

#pragma sokol @program textmode vs fs
