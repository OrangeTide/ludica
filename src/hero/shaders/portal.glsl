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
void main() {
    vec3 albedo = texture2D(u_texture, fract(v_texcoord)).rgb;

    /* sample normal map and unpack from [0,1] to [-1,1] */
    vec3 N = texture2D(u_normal_map, fract(v_texcoord)).rgb;
    N = normalize(N * 2.0 - 1.0);

    vec3 L = normalize(v_tangent_light);
    vec3 V = normalize(v_tangent_view);
    vec3 H = normalize(L + V);

    /* ambient + diffuse + specular */
    float ambient = 0.15;
    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), 32.0);

    vec3 color = albedo * (ambient + diff * 0.75) + vec3(spec * 0.2);
    gl_FragColor = vec4(color, 1.0);
}
@end

@fs portal_colored
void main() {
    gl_FragColor = vec4(v_color, 1.0);
}
@end
