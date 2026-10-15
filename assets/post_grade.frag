#version 330 core
// Fake2D custom shader slot 1 — "post grade", fragment stage.
//
// Full-screen presentation grade: desaturate, lift, vignette and scanlines,
// applied to a whole render target in one pass (see rt_present). Every knob
// defaults to "unchanged", so the pass is a straight blit until a script
// turns something up — which also makes it easy to assert on in a test.
//
// Uniforms come from shader_set_float / shader_set_vec2.
in vec2 v_uv;
in vec4 v_color;

// Every knob is "0 = unchanged", so an untouched shader is a straight blit.
uniform sampler2D u_texture;
uniform float u_desaturate;  // 0 = untouched, 1 = greyscale
uniform float u_brightness;  // additive lift, e.g. -0.2 .. 0.2
uniform float u_vignette;    // corner darkening strength, 0 = off
uniform float u_scanline;    // CRT scanline depth, 0 = off
uniform float u_time;        // seconds, for animated effects
uniform vec2 u_resolution;   // target size in pixels (scanline frequency)

out vec4 frag_color;

void main() {
    vec4 tex = texture(u_texture, v_uv) * v_color;
    vec3 c = tex.rgb;

    // Rec. 601 luma; mixing toward it desaturates.
    float lum = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(c, vec3(lum), u_desaturate);

    c += vec3(u_brightness);

    // One dark band per target row (v_uv is 0..1 across the whole surface).
    if (u_scanline > 0.0) {
        float row = 0.5 + 0.5 * sin(v_uv.y * u_resolution.y * 3.14159265);
        c *= 1.0 - u_scanline * (1.0 - row);
    }

    if (u_vignette > 0.0) {
        vec2 d = v_uv - vec2(0.5);
        float falloff = 1.0 - dot(d, d) * u_vignette * 2.0;
        c *= clamp(falloff, 0.0, 1.0);
    }

    // u_time is unused by the grade itself; animated effects (chromatic
    // wobble, grain) can key off it without recompiling.
    frag_color = vec4(c + 0.0 * u_time, tex.a);
}
