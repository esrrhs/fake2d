#version 330 core
// Fake2D custom shader slot — "fx blur", fragment stage.
//
// A separable Gaussian: run it once with u_dir = (step/w, 0) to blur
// horizontally, once with u_dir = (0, step/h) to blur vertically, which is a
// full 2D blur for a ninth of the taps (the two-pass trick every bloom uses).
//
// The step is passed in UV, not pixels, because the shader has no idea how big
// the surface is: the caller owns the resolution, so u_dir = 0 is a straight
// copy (every tap samples the same texel). That "0 = unchanged" contract is
// what makes the pass assertable, and it means a chain can be built before a
// single knob is turned.
in vec2 v_uv;
in vec4 v_color;

uniform sampler2D u_texture;
uniform vec2 u_dir; // step in UV; (x, 0) horizontal, (0, y) vertical
uniform float u_dim; // 0 = unchanged, 1 = black: the composite gain

out vec4 frag_color;

void main() {
    vec4 sum = texture(u_texture, v_uv) * 0.2270270270;
    sum += texture(u_texture, v_uv + u_dir * 1.0) * 0.1945945946;
    sum += texture(u_texture, v_uv - u_dir * 1.0) * 0.1945945946;
    sum += texture(u_texture, v_uv + u_dir * 2.0) * 0.1216216216;
    sum += texture(u_texture, v_uv - u_dir * 2.0) * 0.1216216216;
    sum += texture(u_texture, v_uv + u_dir * 3.0) * 0.0540540541;
    sum += texture(u_texture, v_uv - u_dir * 3.0) * 0.0540540541;
    sum += texture(u_texture, v_uv + u_dir * 4.0) * 0.0162162162;
    sum += texture(u_texture, v_uv - u_dir * 4.0) * 0.0162162162;
    // u_dim rides along so the same program can also scale a pass down for an
    // additive composite; at 0 the pass is exactly the blur above.
    frag_color = sum * v_color * (1.0 - u_dim);
}
