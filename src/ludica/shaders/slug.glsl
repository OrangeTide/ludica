@common
#version 300 es
/*
 * Slug font rendering shader — GLSL ES 300 port.
 *
 * Based on the Slug algorithm reference shaders by Eric Lengyel.
 * Original code: MIT License, Copyright 2017 Eric Lengyel.
 * SPDX-License-Identifier: MIT OR Apache-2.0
 *
 * Curve texture (RGBA16F): pairs of texels store quadratic Bezier
 * control points (p1.xy, p2.xy) and (p3.xy, unused).
 * Band texture (RG16UI): maps glyph bands to curve lists.
 */
precision highp float;
precision highp int;
@end

@vs slug
layout(location = 0) in vec2  a_pos;    /* screen-space vertex position */
layout(location = 1) in vec2  a_norm;   /* outward normal for dilation */
layout(location = 2) in vec2  a_em;     /* em-space sample coordinate */
layout(location = 3) in vec4  a_glyph;  /* (glyph_x, glyph_y, band_max_x, band_max_y) as floats */
layout(location = 4) in vec4  a_band;   /* (band_scale_x, band_scale_y, band_off_x, band_off_y) */
layout(location = 5) in vec4  a_color;  /* RGBA vertex color */

uniform mat4 u_proj;
uniform vec2 u_em_per_pixel; /* em-space size of one screen pixel */

out vec2      v_em;
flat out ivec4 v_glyph;
flat out vec4  v_band;
out vec4      v_color;

void main()
{
	/* Dilate vertex outward by 2 pixels for AA margin.
	 * a_norm is +/-1 at each corner in screen space.
	 * u_em_per_pixel converts pixel distances to em-space.
	 * em Y is flipped relative to screen Y, so negate the Y component. */
	vec2 em_dilation = vec2(a_norm.x, -a_norm.y) * u_em_per_pixel * 2.0;
	vec2 pos = a_pos + a_norm * 2.0;  /* 2 view-pixels outward */
	vec2 em  = a_em  + em_dilation;

	v_em    = em;
	v_glyph = ivec4(a_glyph);
	v_band  = a_band;
	v_color = a_color;
	gl_Position = u_proj * vec4(pos, 0.0, 1.0);
}
@end

@fs slug
precision highp usampler2D;

uniform sampler2D  u_curves; /* RGBA16F curve control points */
uniform highp usampler2D u_bands;  /* RG16UI band index data */

in vec2      v_em;
flat in ivec4 v_glyph;
flat in vec4  v_band;
in vec4      v_color;

out vec4 frag_color;

#define LOG_BAND_TEX_W 12

uint calc_root_code(float y1, float y2, float y3)
{
	uint i1 = floatBitsToUint(y1) >> 31u;
	uint i2 = floatBitsToUint(y2) >> 30u;
	uint i3 = floatBitsToUint(y3) >> 29u;

	uint shift = (i2 & 2u) | (i1 & ~2u);
	shift = (i3 & 4u) | (shift & ~4u);

	return (0x2E74u >> shift) & 0x0101u;
}

vec2 solve_horiz_poly(vec4 p12, vec2 p3)
{
	vec2 a = p12.xy - p12.zw * 2.0 + p3;
	vec2 b = p12.xy - p12.zw;
	float ra = 1.0 / a.y;
	float rb = 0.5 / b.y;

	float d = sqrt(max(b.y * b.y - a.y * p12.y, 0.0));
	float t1 = (b.y - d) * ra;
	float t2 = (b.y + d) * ra;

	if (abs(a.y) < 1.0 / 65536.0) { t1 = p12.y * rb; t2 = t1; }

	return vec2(
		(a.x * t1 - b.x * 2.0) * t1 + p12.x,
		(a.x * t2 - b.x * 2.0) * t2 + p12.x
	);
}

vec2 solve_vert_poly(vec4 p12, vec2 p3)
{
	vec2 a = p12.xy - p12.zw * 2.0 + p3;
	vec2 b = p12.xy - p12.zw;
	float ra = 1.0 / a.x;
	float rb = 0.5 / b.x;

	float d = sqrt(max(b.x * b.x - a.x * p12.x, 0.0));
	float t1 = (b.x - d) * ra;
	float t2 = (b.x + d) * ra;

	if (abs(a.x) < 1.0 / 65536.0) { t1 = p12.x * rb; t2 = t1; }

	return vec2(
		(a.y * t1 - b.y * 2.0) * t1 + p12.y,
		(a.y * t2 - b.y * 2.0) * t2 + p12.y
	);
}

ivec2 calc_band_loc(ivec2 glyph_loc, uint offset)
{
	ivec2 loc = ivec2(glyph_loc.x + int(offset), glyph_loc.y);
	loc.y += loc.x >> LOG_BAND_TEX_W;
	loc.x &= (1 << LOG_BAND_TEX_W) - 1;
	return loc;
}

void main()
{
	vec2 em = v_em;
	vec2 ems_per_pixel = fwidth(em);
	vec2 pixels_per_em = 1.0 / ems_per_pixel;

	ivec2 glyph_loc = v_glyph.xy;
	ivec2 band_max  = v_glyph.zw;

	ivec2 band_index = clamp(
		ivec2(em * v_band.xy + v_band.zw),
		ivec2(0), band_max
	);

	/* --- horizontal bands --- */
	float xcov = 0.0;
	float xwgt = 0.0;

	uvec2 hband_data = texelFetch(u_bands, ivec2(glyph_loc.x + band_index.y, glyph_loc.y), 0).rg;
	ivec2 hband_loc = calc_band_loc(glyph_loc, hband_data.y);

	for (int ci = 0; ci < int(hband_data.x); ci++)
	{
		ivec2 curve_loc = ivec2(texelFetch(u_bands, ivec2(hband_loc.x + ci, hband_loc.y), 0).rg);
		vec4 p12 = texelFetch(u_curves, curve_loc, 0) - vec4(em, em);
		vec2 p3  = texelFetch(u_curves, ivec2(curve_loc.x + 1, curve_loc.y), 0).xy - em;

		if (max(max(p12.x, p12.z), p3.x) * pixels_per_em.x < -0.5) break;

		uint code = calc_root_code(p12.y, p12.w, p3.y);
		if (code != 0u)
		{
			vec2 r = solve_horiz_poly(p12, p3) * pixels_per_em.x;

			if ((code & 1u) != 0u)
			{
				xcov += clamp(r.x + 0.5, 0.0, 1.0);
				xwgt = max(xwgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0));
			}
			if (code > 1u)
			{
				xcov -= clamp(r.y + 0.5, 0.0, 1.0);
				xwgt = max(xwgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0));
			}
		}
	}

	/* --- vertical bands --- */
	float ycov = 0.0;
	float ywgt = 0.0;

	uvec2 vband_data = texelFetch(u_bands, ivec2(glyph_loc.x + band_max.y + 1 + band_index.x, glyph_loc.y), 0).rg;
	ivec2 vband_loc = calc_band_loc(glyph_loc, vband_data.y);

	for (int ci = 0; ci < int(vband_data.x); ci++)
	{
		ivec2 curve_loc = ivec2(texelFetch(u_bands, ivec2(vband_loc.x + ci, vband_loc.y), 0).rg);
		vec4 p12 = texelFetch(u_curves, curve_loc, 0) - vec4(em, em);
		vec2 p3  = texelFetch(u_curves, ivec2(curve_loc.x + 1, curve_loc.y), 0).xy - em;

		if (max(max(p12.y, p12.w), p3.y) * pixels_per_em.y < -0.5) break;

		uint code = calc_root_code(p12.x, p12.z, p3.x);
		if (code != 0u)
		{
			vec2 r = solve_vert_poly(p12, p3) * pixels_per_em.y;

			if ((code & 1u) != 0u)
			{
				ycov -= clamp(r.x + 0.5, 0.0, 1.0);
				ywgt = max(ywgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0));
			}
			if (code > 1u)
			{
				ycov += clamp(r.y + 0.5, 0.0, 1.0);
				ywgt = max(ywgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0));
			}
		}
	}

	/* --- combine coverage --- */
	float denom = max(xwgt + ywgt, 1.0 / 65536.0);
	float coverage = max(
		abs(xcov * xwgt + ycov * ywgt) / denom,
		min(abs(xcov), abs(ycov))
	);
	coverage = clamp(coverage, 0.0, 1.0);

	frag_color = v_color * coverage;
}
@end
