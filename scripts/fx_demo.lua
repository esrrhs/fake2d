-- fake2d pure-Lua multi-pass FX demo (--fx-demo).
--
-- The pieces Phase 24 and 27 added, put to work:
--
--   rt_copy(src, dst, shader)  one ping-pong pass: blit a target across the
--                              whole canvas of another, through an optional
--                              shader, in one call (and it refuses src == dst)
--   rt_resize(id, w, h)        reallocate a target, so a pass can run at half
--                              resolution for a quarter of the taps
--   assets/fx_blur.*           a separable Gaussian: u_dir = 0 is a straight
--                              copy, so a chain can be assembled before a
--                              single knob is turned
--   assets/fx_bright.*         the threshold a bloom starts with: keep the
--                              highlights, drop everything else (u_threshold
--                              and u_knee; 0/0 is still a straight copy)
--   assets/fx_wave.*           an animated pass — rows displaced by a
--                              travelling sine. It declares u_time and
--                              u_resolution, so the renderer drives the
--                              animation and the script sets no clock
--
-- The chain here is a bloom: draw the world into a target, wobble it through
-- the wave pass, take the highlights with the bright pass, blur those at half
-- resolution horizontally then vertically, and composite the glow back over
-- the sharp scene additively. Plus a split-screen mode that renders the same
-- world from two cameras side by side — the other thing a target pair is good
-- for.
--
-- FakeLua note: chunk-level locals must be initialised, natives take an exact
-- argument count, and state lives in plain file locals.

local SCREEN_W = 960 + 0
local SCREEN_H = 540 + 0
local HALF_W = 192 + 0
local HALF_H = 108 + 0

local knee = 0.18 + 0.0


-- FakeLua binds file-level locals as const, so everything that changes
-- across frames lives in a host-side NativeObject instead (see
-- docs/SCRIPTING.md rule 3). Fields read and write like a plain table.
local S = new_global_obj("fx_demo", "state")

local function reset_state()
    S.radius = 2.0 + 0.0
    S.strength = 0.65 + 0.0
    S.threshold = 0.62 + 0.0
    S.wamp = 6.0 + 0.0
    S.wfreq = 9.0 + 0.0
    S.bright_on = true
    S.wave_on = false
    S.split = false
    S.inited = false
    S.scroll = 0.0 + 0.0
    S.scene_rt = 0 + 0
    S.wave_rt = 0 + 0
    S.bright_rt = 0 + 0
    S.blur_a = 0 + 0
    S.blur_b = 0 + 0
    S.blur_fx = 0 + 0
    S.bright_fx = 0 + 0
    S.wave_fx = 0 + 0
    S.grade_fx = 0 + 0
end

local function pct(v)
    return string.format("%d", math.floor(v * 100 + 0.5))
end

local function istr(n)
    return string.format("%d", n)
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
    S.inited = true
    S.scene_rt = rt_create(SCREEN_W, SCREEN_H)
    -- The wave and the bright pass both run at full resolution: the wave
    -- displaces pixels (half-res would halve the displacement's meaning, were
    -- it not for u_resolution) and a threshold picks out highlights that are
    -- a few pixels wide.
    S.wave_rt = rt_create(SCREEN_W, SCREEN_H)
    S.bright_rt = rt_create(SCREEN_W, SCREEN_H)
    -- Half resolution: a blur over a quarter of the pixels costs a quarter of
    -- the taps, and the glow is blurry anyway.
    S.blur_a = rt_create(HALF_W, HALF_H)
    S.blur_b = rt_create(HALF_W, HALF_H)
    S.blur_fx = shader_load("assets/fx_blur.vert", "assets/fx_blur.frag")
    S.bright_fx = shader_load("assets/fx_bright.vert", "assets/fx_bright.frag")
    S.wave_fx = shader_load("assets/fx_wave.vert", "assets/fx_wave.frag")
    S.grade_fx = shader_load("assets/post_grade.vert", "assets/post_grade.frag")
end

function update(dt)
    if S.seeded == nil then
        S.seeded = true
        reset_state()
    end

    if not S.inited then
        reset_state()
        init()
    end

    S.scroll = S.scroll + dt * 40.0
    if S.scroll > 1400.0 then S.scroll = 0.0 end

    -- --- the chain --------------------------------------------------------
    -- 1. the world, into a target at full resolution
    rt_begin(S.scene_rt)
    rt_clear(0.03, 0.04, 0.07, 1.0)
    draw_world(-S.scroll, 0.0)
    rt_end()

    -- 2. the animated wobble. u_time and u_resolution are stamped on every
    --    loaded shader by the renderer, so there is no clock to feed here —
    --    and u_amp is in pixels, so it reads the same at any resolution.
    local sharp = S.scene_rt
    if S.wave_on and S.wave_fx > 0 and S.wave_rt > 0 then
        shader_set_float(S.wave_fx, "u_amp", S.wamp)
        shader_set_float(S.wave_fx, "u_freq", S.wfreq)
        shader_set_float(S.wave_fx, "u_speed", 2.0)
        rt_copy(S.scene_rt, S.wave_rt, S.wave_fx)
        sharp = S.wave_rt
    end

    -- 3. the bright pass decides what is allowed to glow: without it the bloom
    --    smears the whole frame into a haze.
    local glow_src = sharp
    if S.bright_on and S.bright_fx > 0 and S.bright_rt > 0 then
        shader_set_float(S.bright_fx, "u_threshold", S.threshold)
        shader_set_float(S.bright_fx, "u_knee", knee)
        rt_copy(sharp, S.bright_rt, S.bright_fx)
        glow_src = S.bright_rt
    end

    if S.blur_fx > 0 and glow_src > 0 and S.blur_a > 0 and S.blur_b > 0 and not S.split then
        -- 4. downsample into the half-size pair and blur H then V; u_dir is
        --    in UV, so the step is radius / target_width.
        shader_set_float(S.blur_fx, "u_dim", 0.0)
        rt_copy(glow_src, S.blur_a, 0)
        shader_set_vec2(S.blur_fx, "u_dir", S.radius / HALF_W, 0.0)
        rt_copy(S.blur_a, S.blur_b, S.blur_fx)
        shader_set_vec2(S.blur_fx, "u_dir", 0.0, S.radius / HALF_H)
        rt_copy(S.blur_b, S.blur_a, S.blur_fx)
        -- Wider glow: a second ping-pong pair over the same buffers.
        shader_set_vec2(S.blur_fx, "u_dir", S.radius * 2.0 / HALF_W, 0.0)
        rt_copy(S.blur_a, S.blur_b, S.blur_fx)
        shader_set_vec2(S.blur_fx, "u_dir", 0.0, S.radius * 2.0 / HALF_H)
        rt_copy(S.blur_b, S.blur_a, S.blur_fx)
    end

    -- --- composite --------------------------------------------------------
    if S.split then
        -- Split screen: the same world from two cameras, each clipped to its
        -- own half. No post pass — the point is two views in one frame.
        draw_set_clip(0.0, 0.0, 480.0, SCREEN_H)
        draw_quad(0.0, 0.0, 960.0, 540.0, 0.03, 0.04, 0.07, 1.0)
        draw_world(-S.scroll, 0.0)
        draw_clear_clip()
        draw_set_clip(480.0, 0.0, 480.0, SCREEN_H)
        draw_quad(480.0, 0.0, 960.0, 540.0, 0.05, 0.03, 0.06, 1.0)
        draw_world(-S.scroll * 0.5 - 240.0, 0.0)
        draw_clear_clip()
        draw_quad(478.0, 0.0, 4.0, SCREEN_H, 0.5, 0.55, 0.7, 1.0)
    else
        if S.grade_fx > 0 then
            shader_set_float(S.grade_fx, "u_desaturate", 0.0)
            shader_set_float(S.grade_fx, "u_brightness", 0.0)
            shader_set_float(S.grade_fx, "u_vignette", 0.35)
            shader_set_float(S.grade_fx, "u_scanline", 0.0)
        end
        rt_present(sharp, S.grade_fx)
        if S.blur_fx > 0 and S.blur_a > 0 and S.blur_b > 0 then
            -- Dim the glow into the spare buffer, then composite it additively.
            -- rt_draw (not a shader present) is what keeps the blend: a present
            -- with a shader restarts the batch, which resets it.
            shader_set_vec2(S.blur_fx, "u_dir", 0.0, 0.0)
            shader_set_float(S.blur_fx, "u_dim", 1.0 - S.strength)
            rt_copy(S.blur_a, S.blur_b, S.blur_fx)
            draw_set_blend(1)
            rt_draw(S.blur_b, 0.0, 0.0, SCREEN_W, SCREEN_H)
            draw_set_blend(0)
        end
    end

    -- --- panel -------------------------------------------------------------
    draw_text("MULTI-PASS FX", 40, 20, 1.1, 0.94, 0.96, 1.0, 1.0)
    draw_text("rt_copy ping-pong + a separable blur at half resolution", 40, 44,
              0.62, 0.55, 0.66, 0.75, 1.0)

    draw_quad(40, 60, 300, 292, 0.08, 0.10, 0.15, 0.85)
    S.radius = ui_slider("radius", 0, 52, 74, 240, 22, S.radius, 0.0, 6.0)
    S.strength = ui_slider("strength", 0, 52, 110, 240, 22, S.strength, 0.0, 1.0)
    draw_text("radius " .. pct(S.radius / 6.0) .. "%", 300, 78, 0.55,
              0.75, 0.85, 0.95, 1.0)
    draw_text("glow " .. pct(S.strength) .. "%", 300, 114, 0.55,
              0.75, 0.85, 0.95, 1.0)
    if ui_button("split", 0, 52, 144, 120, 24, "SPLIT", 0.55) then
        S.split = not S.split
    end
    if S.split then
        draw_text("two cameras", 180, 148, 0.6, 0.98, 0.85, 0.40, 1.0)
    else
        draw_text("bloom chain", 180, 148, 0.6, 0.55, 0.66, 0.75, 1.0)
    end

    -- The two Phase 27 passes, each with its own switches. The bright pass is
    -- on by default because a bloom without a threshold is just a haze; the
    -- wave is off because a wobbling skyline is not for everyone.
    if ui_checkbox("bright", 0, 52, 180, 18, S.bright_on, "bright pass", 0.6) then
        S.bright_on = not S.bright_on
    end
    S.threshold = ui_slider("cut", 0, 52, 208, 240, 22, S.threshold, 0.0, 1.0)
    draw_text("cut " .. pct(S.threshold) .. "%", 300, 212, 0.55,
              0.75, 0.85, 0.95, 1.0)
    if ui_checkbox("wave", 0, 52, 244, 18, S.wave_on, "wave pass (u_time)", 0.6) then
        S.wave_on = not S.wave_on
    end
    S.wamp = ui_slider("wamp", 0, 52, 272, 240, 22, S.wamp, 0.0, 20.0)
    S.wfreq = ui_slider("wfreq", 0, 52, 308, 240, 22, S.wfreq, 0.0, 24.0)
    draw_text("amp " .. istr(math.floor(S.wamp)) .. "px", 300, 276, 0.55,
              0.75, 0.85, 0.95, 1.0)
    draw_text("freq " .. istr(math.floor(S.wfreq)), 300, 312, 0.55,
              0.75, 0.85, 0.95, 1.0)

    -- The logical canvas is 960x540 (1920x1080 at 2x), so nothing here may
    -- sit below y=540 or past x=960.
    draw_text("scene -> wave -> bright -> blur H/V -> additive composite", 40,
              470, 0.7, 0.5, 0.6, 0.7, 1.0)
    draw_text("u_time and u_resolution are stamped by the renderer", 40, 496,
              0.6, 0.45, 0.52, 0.6, 1.0)
    return 0
end
