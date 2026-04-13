@common
#version 100
precision mediump float;
varying vec2 v_uv;
@end

@vs framebuffer
attribute vec4 vertex;
void main(void) {
  v_uv = vertex.xy * 0.5 + 0.5;
  gl_Position = vertex;
}
@end

@fs framebuffer_flat
uniform sampler2D u_screen;
uniform sampler2D u_palette;
void main(void) {
  float idx = texture2D(u_screen, v_uv).r;
  gl_FragColor = texture2D(u_palette, vec2(idx, 0.0));
}
@end

@fs framebuffer_crt
uniform sampler2D u_screen;
uniform sampler2D u_palette;
uniform vec2 u_resolution;

vec2 barrel(vec2 uv) {
  vec2 c = uv - 0.5;
  float r2 = dot(c, c);
  return uv + c * r2 * 0.15;
}

void main(void) {
  vec2 uv = barrel(v_uv);
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
    gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }
  float idx = texture2D(u_screen, uv).r;
  vec4 col = texture2D(u_palette, vec2(idx, 0.0));
  /* scanlines: darken every other physical row */
  float scan = 0.85 + 0.15 * sin(uv.y * u_resolution.y * 3.14159);
  /* slight vignette */
  vec2 vig = uv * (1.0 - uv);
  float v = clamp(pow(vig.x * vig.y * 15.0, 0.25), 0.0, 1.0);
  gl_FragColor = vec4(col.rgb * scan * v, 1.0);
}
@end
