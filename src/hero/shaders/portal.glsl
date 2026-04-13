@common
precision mediump float;
varying vec2 v_texcoord;
varying vec3 v_color;
varying vec3 v_tangent_light;
varying vec3 v_tangent_view;
varying float v_light_dist;
@end

@vs portal
attribute vec3 a_position;
attribute vec2 a_texcoord;
attribute vec3 a_normal;
attribute vec3 a_tangent;
attribute vec3 a_color;
uniform mat4 u_mvp;
uniform vec3 u_view_pos;
uniform vec3 u_light_pos;
void main() {
    gl_Position = u_mvp * vec4(a_position, 1.0);
    v_texcoord = a_texcoord;
    v_color = a_color;

    /* construct TBN matrix (tangent space <- world space) */
    vec3 N = normalize(a_normal);
    vec3 T = normalize(a_tangent);
    vec3 B = cross(N, T);

    /* point light: direction and distance from vertex to light */
    vec3 world_pos = a_position;
    vec3 to_light = u_light_pos - world_pos;
    v_light_dist = length(to_light);
    vec3 light_dir = to_light / v_light_dist;

    vec3 to_view = normalize(u_view_pos - world_pos);

    v_tangent_light = vec3(dot(light_dir, T), dot(light_dir, B), dot(light_dir, N));
    v_tangent_view  = vec3(dot(to_view, T),   dot(to_view, B),   dot(to_view, N));
}
@end

@fs portal_textured
uniform sampler2D u_texture;
uniform sampler2D u_normal_map;
uniform sampler2D u_roughness_map;
uniform sampler2D u_ao_map;
uniform sampler2D u_height_map;
uniform vec3 u_light_color;
uniform float u_height_scale;

/* ---- Parallax Occlusion Mapping ---- */

vec2 parallax_occlusion(vec2 uv, vec3 view_dir) {
    const float min_layers = 8.0;
    const float max_layers = 32.0;
    float num_layers = mix(max_layers, min_layers, max(dot(vec3(0, 0, 1), view_dir), 0.0));

    float layer_depth = 1.0 / num_layers;
    float current_layer = 0.0;

    /* direction to shift UVs per layer (scaled by height) */
    vec2 P = view_dir.xy / view_dir.z * u_height_scale;
    vec2 delta_uv = P / num_layers;

    vec2 cur_uv = uv;
    float cur_height = texture2D(u_height_map, fract(cur_uv)).r;

    /* step through layers until we hit the surface */
    for (int i = 0; i < 32; i++) {
        if (current_layer >= cur_height) break;
        cur_uv -= delta_uv;
        cur_height = texture2D(u_height_map, fract(cur_uv)).r;
        current_layer += layer_depth;
    }

    /* interpolate between last two layers for smoother result */
    vec2 prev_uv = cur_uv + delta_uv;
    float after_depth  = cur_height - current_layer;
    float before_depth = texture2D(u_height_map, fract(prev_uv)).r
                         - current_layer + layer_depth;
    float weight = after_depth / (after_depth - before_depth);

    return mix(cur_uv, prev_uv, weight);
}

/* ---- Cook-Torrance BRDF ---- */

/* GGX/Trowbridge-Reitz normal distribution */
float distribution_ggx(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * d * d);
}

/* Schlick-GGX geometry (single direction) */
float geometry_schlick(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

/* Smith's method: combined geometry for light and view */
float geometry_smith(float NdotV, float NdotL, float roughness) {
    return geometry_schlick(NdotV, roughness) *
           geometry_schlick(NdotL, roughness);
}

/* Fresnel-Schlick approximation */
vec3 fresnel_schlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    vec3 V = normalize(v_tangent_view);

    /* parallax-shifted UV */
    vec2 uv = parallax_occlusion(v_texcoord, V);

    /* albedo from sRGB texture (hardware-decoded to linear by GL_SRGB8) */
    vec3 albedo = texture2D(u_texture, fract(uv)).rgb;

    /* sample PBR maps (linear data) */
    vec3 N = texture2D(u_normal_map, fract(uv)).rgb;
    N = normalize(N * 2.0 - 1.0);
    float roughness = texture2D(u_roughness_map, fract(uv)).r;
    float ao = texture2D(u_ao_map, fract(uv)).r;

    vec3 L = normalize(v_tangent_light);
    vec3 H = normalize(L + V);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    /* dielectric F0 = 0.04 (non-metallic surfaces) */
    vec3 F0 = vec3(0.04);

    /* Cook-Torrance specular BRDF */
    float D = distribution_ggx(NdotH, roughness);
    float G = geometry_smith(NdotV, NdotL, roughness);
    vec3  F = fresnel_schlick(HdotV, F0);

    vec3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    vec3 specular = numerator / denominator;

    /* energy conservation: diffuse uses what specular doesn't absorb */
    vec3 kD = (vec3(1.0) - F);

    /* Lambertian diffuse */
    vec3 diffuse = kD * albedo / 3.14159265;

    /* point light attenuation (inverse-square with linear falloff) */
    float dist = v_light_dist;
    float atten = 1.0 / (1.0 + 0.35 * dist + 0.44 * dist * dist);

    /* direct lighting with attenuation */
    vec3 Lo = (diffuse + specular) * u_light_color * NdotL * atten;

    /* minimal ambient: just enough to not lose detail in deep shadow */
    vec3 ambient = vec3(0.00125, 0.005, 0.004) * albedo * ao;

    vec3 color = ambient + Lo;

    /* Reinhard tone mapping (keeps bright specular from clipping) */
    color = color / (color + vec3(1.0));

    /* linear -> sRGB gamma correction */
    color = pow(color, vec3(1.0 / 2.2));

    gl_FragColor = vec4(color, 1.0);
}
@end

@fs portal_colored
void main() {
    gl_FragColor = vec4(v_color, 1.0);
}
@end
