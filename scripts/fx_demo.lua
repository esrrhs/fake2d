-- fake2d pure-Lua multi-pass FX demo (--fx-demo).
--
-- The pieces Phase 24 added, put to work:
--
--   rt_copy(src, dst, shader)  one ping-pong pass: blit a target across the
--                              whole canvas of another, through an optional
--                              shader, in one call (and it refuses src == dst)
--   rt_resize(id, w, h)        reallocate a target, so a pass can run at half
--                              resolution for a quarter of the taps
--   assets/fx_blur.*           a separable Gaussian: u_dir = 0 is a straight
--                              copy, so a chain can be assembled before a
--                              single knob is turned
--
-- The chain here is a bloom: draw the world into a target, blur it
-- horizontally into a half-size target, blur that vertically, then composite
-- the glow back over the sharp scene additively. Two slider passes (radius and
-- strength), plus a split-screen mode that renders the same world from two
-- cameras side by side — the other thing a target pair is good for.
--
-- FakeLua note: chunk-level locals must be initialised, natives take an exact
-- argument count, and state lives in plain file locals.

local SCREEN_W = 960 + 0
local SCREEN_H = 540 + 0
local HALF_W = 192 + 0
local HALF_H = 108 + 0

local radius = 2.0 + 0.0
local strength = 0.65 + 0.0
local split = false
local inited = false
local scroll = 0.0 + 0.0

local scene_rt = 0 + 0
local blur_a = 0 + 0
local blur_b = 0 + 0
local blur_fx = 0 + 0
local grade_fx = 0 + 0

local function pct(v)
    return string.format("%d", math.floor(v * 100 + 0.5))
end

-- A procedural skyline: no assets, so the demo runs anywhere, and the bright
-- bands are what the bloom has something to do with.
local function draw_world(ox, oy)
    local i = 0 + 0
    while i < 30 do
        local x = i * 70.0
        local h = 120.0 + (i * 37 % 160)
        draw_quad(ox + x, oy + 420.0 - h, 60.0, h, 0.16, 0.20, 0.30, 1.0)
        -- Windows: the bright pixels the glow picks up.
        local row = 0 + 0
        while row < 6 do
            local wy = oy + 420.0 - h + 16.0 + row * 26.0
            if wy < oy + 420.0 then
                draw_quad(ox + x + 10.0, wy, 12.0, 10.0, 0.98, 0.86, 0.55, 1.0)
                draw_quad(ox + x + 34.0, wy, 12.0, 10.0, 0.55, 0.80, 0.98, 1.0)
            end
            row = row + 1
        end
        i = i + 1
    end
    draw_quad(ox, oy + 420.0, 960.0, 120.0, 0.10, 0.13, 0.18, 1.0)
end

local function init()
    inited = true
    scene_rt = rt_create(SCREEN_W, SCREEN_H)
    -- Half resolution: a blur over a quarter of the pixels costs a quarter of
    -- the taps, and the glow is blurry anyway.
    blur_a = rt_create(HALF_W, HALF_H)
    blur_b = rt_create(HALF_W, HALF_H)
    blur_fx = shader_load("assets/fx_blur.vert", "assets/fx_blur.frag")
    grade_fx = shader_load("assets/post_grade.vert", "assets/post_grade.frag")
end

function update(dt)
    if not inited then
        init()
    end

    scroll = scroll + dt * 40.0
    if scroll > 1400.0 then scroll = 0.0 end

    -- --- the chain --------------------------------------------------------
    -- 1. the world, into a target at full resolution
    rt_begin(scene_rt)
    rt_clear(0.03, 0.04, 0.07, 1.0)
    draw_world(-scroll, 0.0)
    rt_end()

    if blur_fx > 0 and scene_rt > 0 and blur_a > 0 and blur_b > 0 and not split then
        -- 2. downsample into the half-size pair and blur H then V; u_dir is
        --    in UV, so the step is radius / target_width.
        shader_set_float(blur_fx, "u_dim", 0.0)
        rt_copy(scene_rt, blur_a, 0)
        shader_set_vec2(blur_fx, "u_dir", radius / HALF_W, 0.0)
        rt_copy(blur_a, blur_b, blur_fx)
        shader_set_vec2(blur_fx, "u_dir", 0.0, radius / HALF_H)
        rt_copy(blur_b, blur_a, blur_fx)
        -- Wider glow: a second ping-pong pair over the same buffers.
        shader_set_vec2(blur_fx, "u_dir", radius * 2.0 / HALF_W, 0.0)
        rt_copy(blur_a, blur_b, blur_fx)
        shader_set_vec2(blur_fx, "u_dir", 0.0, radius * 2.0 / HALF_H)
        rt_copy(blur_b, blur_a, blur_fx)
    end

    -- --- composite --------------------------------------------------------
    if split then
        -- Split screen: the same world from two cameras, each clipped to its
        -- own half. No post pass — the point is two views in one frame.
        draw_set_clip(0.0, 0.0, 480.0, SCREEN_H)
        draw_quad(0.0, 0.0, 960.0, 540.0, 0.03, 0.04, 0.07, 1.0)
        draw_world(-scroll, 0.0)
        draw_clear_clip()
        draw_set_clip(480.0, 0.0, 480.0, SCREEN_H)
        draw_quad(480.0, 0.0, 960.0, 540.0, 0.05, 0.03, 0.06, 1.0)
        draw_world(-scroll * 0.5 - 240.0, 0.0)
        draw_clear_clip()
        draw_quad(478.0, 0.0, 4.0, SCREEN_H, 0.5, 0.55, 0.7, 1.0)
    else
        if grade_fx > 0 then
            shader_set_float(grade_fx, "u_desaturate", 0.0)
            shader_set_float(grade_fx, "u_brightness", 0.0)
            shader_set_float(grade_fx, "u_vignette", 0.35)
            shader_set_float(grade_fx, "u_scanline", 0.0)
        end
        rt_present(scene_rt, grade_fx)
        if blur_fx > 0 and blur_a > 0 and blur_b > 0 then
            -- Dim the glow into the spare buffer, then composite it additively.
            -- rt_draw (not a shader present) is what keeps the blend: a present
            -- with a shader restarts the batch, which resets it.
            shader_set_vec2(blur_fx, "u_dir", 0.0, 0.0)
            shader_set_float(blur_fx, "u_dim", 1.0 - strength)
            rt_copy(blur_a, blur_b, blur_fx)
            draw_set_blend(1)
            rt_draw(blur_b, 0.0, 0.0, SCREEN_W, SCREEN_H)
            draw_set_blend(0)
        end
    end

    -- --- panel -------------------------------------------------------------
    draw_text("MULTI-PASS FX", 40, 20, 1.1, 0.94, 0.96, 1.0, 1.0)
    draw_text("rt_copy ping-pong + a separable blur at half resolution", 40, 44,
              0.62, 0.55, 0.66, 0.75, 1.0)

    draw_quad(40, 60, 300, 118, 0.08, 0.10, 0.15, 0.85)
    radius = ui_slider("radius", 0, 52, 74, 240, 22, radius, 0.0, 6.0)
    strength = ui_slider("strength", 0, 52, 110, 240, 22, strength, 0.0, 1.0)
    draw_text("radius " .. pct(radius / 6.0) .. "%", 300, 78, 0.55,
              0.75, 0.85, 0.95, 1.0)
    draw_text("glow " .. pct(strength) .. "%", 300, 114, 0.55,
              0.75, 0.85, 0.95, 1.0)
    if ui_button("split", 0, 52, 144, 120, 24, "SPLIT", 0.55) then
        split = not split
    end
    if split then
        draw_text("two cameras", 180, 148, 0.6, 0.98, 0.85, 0.40, 1.0)
    else
        draw_text("bloom chain", 180, 148, 0.6, 0.55, 0.66, 0.75, 1.0)
    end

    -- The logical canvas is 960x540 (1920x1080 at 2x), so nothing here may
    -- sit below y=540 or past x=960.
    draw_text("scene -> blur H -> blur V -> additive composite", 40, 470, 0.7,
              0.5, 0.6, 0.7, 1.0)
    draw_text("rt_copy / rt_resize + assets/fx_blur.*  (--fx-test asserts it)",
              40, 496, 0.6, 0.45, 0.52, 0.6, 1.0)
    return 0
end
