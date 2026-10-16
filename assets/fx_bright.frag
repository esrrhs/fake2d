#version 330 core
// Fake2D custom shader slot — "fx bright", fragment stage.
//
// The bright-pass (threshold) every bloom starts with: keep the highlights,
// drop everything else, so the glow is built from what actually glows instead
// of smearing the whole frame. Uniforms follow the house rule that an
// untouched shader is a straight copy — u_threshold = 0 and u_knee = 0 pass
// the frame through unchanged, which is also what makes the pass assertable.
//
// The knee is the width of the ramp around the threshold: 0 cuts hard (and
// blooms tend to flicker as pixels cross it), anything wider fades in softly.
// It is clamped a hair above zero inside so smoothstep's two edges can never
// coincide, which would be a division by zero on some drivers.
in vec2 v_uv;
in vec4 v_color;

uniform sampler2D u_texture;
uniform float u_threshold; // 0 = keep everything
uniform float u_knee;      // 0 = hard cut; > 0 = soft ramp this wide

out vec4 frag_color;

void main() {
    vec4 c = texture(u_texture, v_uv);
    // Rec.709 luma, so a saturated colour is judged by how bright it reads.
    float luma = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));
    float k = max(u_knee, 0.0001);
    float keep = smoothstep(u_threshold - k, u_threshold + k, luma);
    // Alpha rides through untouched: a threshold is about brightness, and
    // dropping alpha here would punch holes in later passes.
    frag_color = vec4(c.rgb * keep, c.a) * v_color;
}
