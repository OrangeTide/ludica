#pragma sokol @ctype mat4 hmm_mat4

#pragma sokol @vs vs
layout (location = 0) in vec3 position;
layout (location = 1) in vec2 texcoord;

uniform mvp_matrix { mat4 mvp; };

out vec2 uv0;

void main() {
    gl_Position = mvp * vec4(position, 1.0);
    uv0 = texcoord;
}
#pragma sokol @end

#pragma sokol @fs fs
layout(location = 0) out vec4 frag_color;

uniform sampler2D u_tex;

in vec2 uv0;

void main(void) {
	frag_color = vec4(texture(u_tex, uv0.st).rgb, 1.0);
}

#pragma sokol @end

#pragma sokol @program basic vs fs
