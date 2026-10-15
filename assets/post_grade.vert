#version 330 core
// Fake2D custom shader slot 1 — "post grade", vertex stage.
//
// The post pass blits one render target across the whole screen (rt_present),
// so the vertex stage is identical to the engine default: the batch layout is
// fixed (loc 0 = pos, 1 = uv, 2 = color) and u_view_projection is uploaded by
// the renderer.
layout (location = 0) in vec2 a_pos;
layout (location = 1) in vec2 a_uv;
layout (location = 2) in vec4 a_color;

uniform mat4 u_view_projection;

out vec2 v_uv;
out vec4 v_color;

void main() {
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = u_view_projection * vec4(a_pos, 0.0, 1.0);
}
