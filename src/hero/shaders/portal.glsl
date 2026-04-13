@common
precision mediump float;
varying vec2 v_texcoord;
varying vec3 v_color;
@end

@vs portal
attribute vec3 a_position;
attribute vec2 a_texcoord;
attribute vec3 a_color;
uniform mat4 u_mvp;
void main() {
    gl_Position = u_mvp * vec4(a_position, 1.0);
    v_texcoord = a_texcoord;
    v_color = a_color;
}
@end

@fs portal_textured
uniform sampler2D u_texture;
void main() {
    gl_FragColor = texture2D(u_texture, fract(v_texcoord));
}
@end

@fs portal_colored
void main() {
    gl_FragColor = vec4(v_color, 1.0);
}
@end
