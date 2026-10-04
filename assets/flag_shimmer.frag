#version 330 core
// Fake2D custom shader slot 0 — "flag shimmer" fragment stage.
//
// A diagonal band sweeps down the quad, brightening texels as it passes. The
// band position is driven by u_time (seconds) and u_speed, both uploaded from
// Lua via shader_set_float; u_tint is a shader_set_vec4 colour so the effect
// can be re-tinted per level without recompiling.
in vec2 v_uv;
in vec4 v_color;

uniform sampler2D u_texture;
uniform float u_time;
uniform float u_speed;
uniform float u_width;
uniform vec4 u_tint;

out vec4 frag_color;

void main() {
    vec4 tex = texture(u_texture, v_uv) * v_color;

    // Distance from the moving band, measured along the quad diagonal.
    float d = (v_uv.x + (1.0 - v_uv.y)) * 0.5;
    float phase = fract(u_time * u_speed);
    // Signed distance to the band, wrapped into [-0.5, 0.5].
    float rel = fract(d - phase + 0.5) - 0.5;
    float band = 1.0 - smoothstep(0.0, u_width, abs(rel));

    // Tint the base color, then add the highlight on top.
    vec3 base = tex.rgb * u_tint.rgb;
    vec3 lit = base + vec3(0.45, 0.40, 0.22) * band;
    frag_color = vec4(lit, tex.a * u_tint.a);
}
