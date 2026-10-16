#version 330 core
// Fake2D custom shader slot — "fx wave", fragment stage.
//
// An animated pass: rows are displaced sideways by a travelling sine, which is
// what heat shimmer, water refraction and VHS wobble are all made of. Two of
// its uniforms the renderer writes for it:
//
//   u_time        seconds, stamped on every loaded shader each frame, so the
//                 animation advances with no per-script bookkeeping
//   u_resolution  pixels of the surface being drawn into — the target's own
//                 size inside a pass, the window's outside one
//
// That second one is why u_amp is in *pixels*: the same number displaces by
// the same number of pixels whether the pass runs full size or at half
// resolution. u_amp = 0 is a straight copy (the house rule again), and
// u_time = 0 puts every row at phase zero, so an un-animated frame is the
// original one.
in vec2 v_uv;
in vec4 v_color;

uniform sampler2D u_texture;
uniform vec2 u_resolution; // stamped by the renderer
uniform float u_time;      // stamped by the renderer
uniform float u_amp;       // 0 = unchanged; peak displacement in PIXELS
uniform float u_freq;      // waves across the surface (0 = every row in step)
uniform float u_speed;     // radians per second

out vec4 frag_color;

void main() {
    float phase = v_uv.y * u_freq * 6.2831853 + u_time * u_speed;
    float shift_px = u_amp * sin(phase);
    // Pixels -> UV through the resolution the renderer reported, so the
    // displacement above is resolution independent. The sign is "minus"
    // because moving the *sample point* right slides the picture left — with
    // the intuitive sign, a positive u_amp pushes the image to the right.
    vec2 uv = v_uv - vec2(shift_px / max(u_resolution.x, 1.0), 0.0);
    frag_color = texture(u_texture, uv) * v_color;
}
