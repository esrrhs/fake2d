-- fake2d post-processing demo (--post-demo).
--
-- The whole world is drawn into a render target once per frame, then
-- presented through the post shader (assets/post_grade.*). The HUD is drawn
-- after the present, so it stays crisp while the world gets graded — which
-- is the point of a post pass: the HUD is UI, the world is the picture.
--
-- Two pieces the pass needs beyond a plain rt_begin/rt_end:
--   rt_camera(x, y)          aims the target's canvas camera at the world, so
--                            map_draw renders the *camera's* view, not the
--                            canvas origin;
--   rt_present(id, shader)   blits the target across the whole screen.
--
-- A minimap is baked once with map_draw_scaled (parallax-free, whole map at
-- a uniform scale) and blitted every frame under a viewport rectangle.
--
-- The level is assets/mario.json when it has been generated (--mario-demo
-- writes it); without it the demo falls back to a procedural skyline, so the
-- post pass itself is always demonstrated.
--
-- FakeLua note: the logical canvas is 960x540, and nothing here may draw
-- past it; math.fmod is not supported by the JIT codegen.

local SCREEN_W = 960 + 0
local SCREEN_H = 540 + 0

local scene_rt = 0 + 0
local mini_rt = 0 + 0
local fx = 0 + 0
local map_id = 0 + 0
local world_w = 2400.0 + 0.0
local world_h = 540.0 + 0.0
local mini_scale = 1.0 + 0.0
local inited = false

-- Post knobs. Every uniform is "0 = unchanged", so the pass is a straight
-- blit until a slider moves.
local post_on = true
local grey = false
local bright = 0.0 + 0.0
local vig = 0.9 + 0.0
local scan = 0.30 + 0.0

local player_x = 200.0 + 0.0
local cam_x = 0.0 + 0.0
local cam_y = 0.0 + 0.0

local function istr(n)
    return string.format("%d", n)
end

local function h01(i)
    local x = math.sin(i * 12.9898 + 78.233) * 43758.5453
    return x - math.floor(x)
end

-- Deterministic skyline. `s` scales it (1 = world pixels, ~0.1 = the
-- minimap bake); (ox, oy) shifts it, which is the camera offset when the
-- world is drawn into the scene target.
local function draw_world(s, ox, oy)
    draw_quad(ox, oy + (world_h - 24) * s, world_w * s, 24 * s,
              0.12, 0.15, 0.21, 1.0)
    local i = 0 + 0
    while i * 72 < world_w do
        local bh = 70 + h01(i) * 200
        local bx = i * 72
        draw_quad(ox + bx * s, oy + (world_h - 24 - bh) * s, 52 * s, bh * s,
                  0.13 + h01(i + 40) * 0.20, 0.17 + h01(i + 80) * 0.18,
                  0.25 + h01(i + 120) * 0.30, 1.0)
        -- a lit band per tower, so the grade has highlights to bite on
        draw_quad(ox + (bx + 10) * s, oy + (world_h - 48 - bh) * s,
                  32 * s, 8 * s, 0.95, 0.82, 0.42, 1.0)
        i = i + 1
    end
end

local function draw_player(s, ox, oy)
    local gy = (world_h - 24) * s
    draw_quad(ox + player_x * s - 14 * s, gy - 28 * s, 28 * s, 28 * s,
              1.0, 0.78, 0.25, 1.0)
    draw_quad(ox + player_x * s - 6 * s, gy - 20 * s, 12 * s, 12 * s,
              1.0, 1.0, 0.88, 1.0)
end

local function bake_minimap()
    if mini_rt == 0 then
        return
    end
    local mw = rt_width(mini_rt) + 0.0
    local mh = rt_height(mini_rt) + 0.0
    mini_scale = mw / world_w
    if world_h * mini_scale > mh then
        mini_scale = mh / world_h
    end
    rt_begin(mini_rt)
    rt_clear(0.04, 0.05, 0.09, 1.0)
    rt_camera(0.0, 0.0)  -- the bake is canvas space, not a camera view
    if map_id > 0 then
        map_draw_scaled(map_id, mini_scale)
    else
        draw_world(mini_scale, 0.0, 0.0)
        draw_player(mini_scale, 0.0, 0.0)
    end
    rt_end()
end

local function init()
    inited = true
    scene_rt = rt_create(SCREEN_W, SCREEN_H)
    mini_rt = rt_create(320, 100)
    fx = shader_load("assets/post_grade.vert", "assets/post_grade.frag")
    map_id = map_load("assets/mario.json")
    if map_id > 0 then
        world_w = map_cols(map_id) * map_tilew(map_id) + 0.0
        world_h = map_rows(map_id) * map_tileh(map_id) + 0.0
    end
    -- Ride the bottom band of the world: the camera's top-left y is world_h
    -- minus the view height, which pins the world's base to the screen base
    -- (and shows sky above a short world).
    cam_y = world_h - SCREEN_H
    bake_minimap()
end

function update(dt)
    if not inited then
        init()
    end

    if input_key_pressed("space") then
        post_on = not post_on
    end
    if input_key_pressed("g") then
        grey = not grey
    end

    -- Patrol right and wrap; the camera eases after the player and snaps on
    -- the wrap so it never sweeps the whole world in one frame.
    player_x = player_x + 140.0 * dt
    local wrapped = false
    if player_x > world_w - 160.0 then
        player_x = 160.0
        wrapped = true
    end
    local target = player_x - 330.0
    if target < 0.0 then
        target = 0.0
    end
    if target > world_w - SCREEN_W then
        target = world_w - SCREEN_W
    end
    if target < 0.0 then
        target = 0.0
    end
    if wrapped then
        cam_x = target
    else
        local k = dt * 6.0
        if k > 1.0 then
            k = 1.0
        end
        cam_x = cam_x + (target - cam_x) * k
    end

    -- 1. the world, into the target, from the camera's point of view --------
    local have_rt = scene_rt > 0 and rt_begin(scene_rt)
    if have_rt then
        rt_clear(0.05, 0.07, 0.12, 1.0)
        rt_camera(cam_x, cam_y)
        if map_id > 0 then
            map_draw(map_id)
        else
            draw_world(1.0, -cam_x, -cam_y)
        end
        draw_player(1.0, -cam_x, -cam_y)
        rt_end()
    end

    -- 2. the post pass: whole-screen present, graded or plain ---------------
    if have_rt then
        if post_on and fx > 0 then
            local d = 0.0 + 0.0
            if grey then
                d = 1.0 + 0.0
            end
            shader_set_float(fx, "u_desaturate", d)
            shader_set_float(fx, "u_brightness", bright)
            shader_set_float(fx, "u_vignette", vig)
            shader_set_float(fx, "u_scanline", scan)
            shader_set_vec2(fx, "u_resolution", SCREEN_W + 0.0, SCREEN_H + 0.0)
            shader_set_float(fx, "u_time", time_elapsed())
            rt_present(scene_rt, fx)
        else
            rt_present(scene_rt, 0)
        end
    else
        -- No render target on this context: draw straight to the screen.
        camera_set_position(cam_x, cam_y)
        if map_id > 0 then
            map_draw(map_id)
        else
            draw_world(1.0, -cam_x, -cam_y)
        end
        draw_player(1.0, -cam_x, -cam_y)
    end

    -- 3. HUD, on top of the graded picture ---------------------------------
    draw_quad(616, 32, 324, 226, 0.04, 0.06, 0.11, 0.88)
    draw_text("POST PASS", 632, 42, 0.75, 0.62, 0.86, 0.98, 1.0)
    if ui_checkbox("post", 0, 632, 66, 18, post_on, "enabled (SPACE)", 0.6) then
        post_on = not post_on
    end
    if ui_checkbox("grey", 0, 632, 92, 18, grey, "greyscale (G)", 0.6) then
        grey = not grey
    end
    bright = ui_slider("bright", 0, 632, 126, 180, 22, bright, -0.3, 0.3)
    draw_text("lift " .. istr(math.floor(bright * 100 + 0.5)), 822, 132, 0.6,
              0.75, 0.85, 0.95, 1.0)
    vig = ui_slider("vig", 0, 632, 166, 180, 22, vig, 0.0, 2.0)
    draw_text("vignette " .. istr(math.floor(vig * 50 + 0.5)) .. "%", 822, 172,
              0.6, 0.75, 0.85, 0.95, 1.0)
    scan = ui_slider("scan", 0, 632, 206, 180, 22, scan, 0.0, 1.0)
    draw_text("scanline " .. istr(math.floor(scan * 100 + 0.5)) .. "%", 822,
              212, 0.6, 0.75, 0.85, 0.95, 1.0)

    -- Minimap: the bake is one texture; the rectangle is the camera's view.
    draw_text("MINIMAP", 632, 268, 0.7, 0.75, 0.65, 0.95, 1.0)
    if mini_rt > 0 then
        draw_quad(616, 288, 324, 104, 0.04, 0.05, 0.09, 1.0)
        rt_draw(mini_rt, 618, 290, 320, 100)
        -- The camera may look at sky above a short world, so the viewport
        -- rectangle can leave the baked map: clip it to the pane.
        draw_set_clip(618, 290, 316, 100)
        draw_rect_outline(618 + cam_x * mini_scale, 290 + cam_y * mini_scale,
                          SCREEN_W * mini_scale, SCREEN_H * mini_scale,
                          1.0, 0.85, 0.35, 1.0)
        draw_clear_clip()
    end

    draw_text("POST PROCESSING", 40, 24, 1.2, 0.94, 0.96, 1.0, 1.0)
    draw_text("the world is drawn into a target, then presented through a shader",
              40, 54, 0.7, 0.55, 0.66, 0.75, 1.0)
    local where = "world: procedural skyline"
    if map_id > 0 then
        where = "world: assets/mario.json (" .. istr(map_cols(map_id)) .. "x"
                    .. istr(map_rows(map_id)) .. " tiles)"
    end
    draw_text(where, 40, 476, 0.65, 0.6, 0.7, 0.8, 1.0)
    draw_text("SPACE post on/off   G greyscale   sliders grade the pass",
              40, 500, 0.65, 0.5, 0.6, 0.7, 1.0)
    return 0
end
