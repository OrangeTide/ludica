@common
precision mediump float;
varying vec2 v_texcoord;
varying vec3 v_color;
varying vec3 v_tangent_light;
varying vec3 v_tangent_view;
@end

@vs portal
attribute vec3 a_position;
attribute vec2 a_texcoord;
attribute vec3 a_normal;
attribute vec3 a_tangent;
attribute vec3 a_color;
uniform mat4 u_mvp;
uniform vec3 u_view_pos;
uniform vec3 u_light_dir;
void main() {
    gl_Position = u_mvp * vec4(a_position, 1.0);
    v_texcoord = a_texcoord;
    v_color = a_color;

    /* construct TBN matrix (tangent space <- world space) */
    vec3 N = normalize(a_normal);
    vec3 T = normalize(a_tangent);
    vec3 B = cross(N, T);

    /* transform light and view directions into tangent space */
    vec3 world_pos = a_position;
    vec3 to_light = normalize(-u_light_dir);
    vec3 to_view  = normalize(u_view_pos - world_pos);

    v_tangent_light = vec3(dot(to_light, T), dot(to_light, B), dot(to_light, N));
    v_tangent_view  = vec3(dot(to_view, T),  dot(to_view, B),  dot(to_view, N));
}
@end

@fs portal_textured
uniform sampler2D u_texture;
uniform sampler2D u_normal_map;
uniform sampler2D u_roughness_map;
uniform sampler2D u_ao_map;
uniform vec3 u_light_color;

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
    vec2 uv = fract(v_texcoord);

    /* albedo from sRGB texture (hardware-decoded to linear by GL_SRGB8) */
    vec3 albedo = texture2D(u_texture, uv).rgb;

    /* sample PBR maps (linear data) */
    vec3 N = texture2D(u_normal_map, uv).rgb;
    N = normalize(N * 2.0 - 1.0);
    float roughness = texture2D(u_roughness_map, uv).r;
    float ao = texture2D(u_ao_map, uv).r;

    vec3 L = normalize(v_tangent_light);
    vec3 V = normalize(v_tangent_view);
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

    /* direct lighting */
    vec3 Lo = (diffuse + specular) * u_light_color * NdotL;

    /* ambient (very simple, AO-modulated) */
    vec3 ambient = vec3(0.08) * albedo * ao;

    vec3 color = ambient + Lo;

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
