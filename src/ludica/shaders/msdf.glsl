@common
#version 100
/*
 * SDF / MSDF font rendering shader — GLSL ES 100 (GLES2+).
 *
 * Atlas is always RGB8: SDF stores identical values in all three
 * channels, MSDF stores independent distance fields per channel.
 * median(r,g,b) handles both cases.
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */
#extension GL_OES_standard_derivatives : enable
precision mediump float;
@end

@vs msdf
attribute vec2 a_pos;
attribute vec2 a_uv;
attribute vec4 a_color;

uniform mat4 u_proj;

varying vec2 v_uv;
varying vec4 v_color;

void main()
{
	v_uv = a_uv;
	v_color = a_color;
	gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);
}
@end

@fs msdf
uniform sampler2D u_atlas;

varying vec2 v_uv;
varying vec4 v_color;

float median(float r, float g, float b)
{
	return max(min(r, g), min(max(r, g), b));
}

void main()
{
	vec4 s = texture2D(u_atlas, v_uv);
	/* SDF: R==G==B, so median is identity.
	 * MSDF: independent per-channel distances, median selects edge. */
	float d = median(s.r, s.g, s.b);
	float w = fwidth(d);
	float alpha = smoothstep(0.5 - w, 0.5 + w, d);
	gl_FragColor = v_color * alpha;
}
@end
