@common
precision mediump float;
varying vec2 v_uv;
varying vec4 v_color;
@end

@vs sprite
attribute vec2 a_pos;
attribute vec2 a_uv;
attribute vec4 a_color;
uniform mat4 u_proj;
void main() {
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);
}
@end

@fs sprite
uniform sampler2D u_tex;
void main() {
    gl_FragColor = texture2D(u_tex, v_uv) * v_color;
}
@end
