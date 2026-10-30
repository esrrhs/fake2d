-- fake2d pure-Lua fog-of-war demo (--fog-demo).
--
-- The pieces Phase 26 added, put to work:
--
--   draw_set_blend(2)   multiply: result = source * destination, so a target
--                       becomes a per-pixel coverage mask. White is a no-op,
--                       black erases, and anything in between dims — the
--                       primitive that turns "how much have I seen here" into
--                       a picture. Compositing happens inside a bound target,
--                       which rt_copy cannot do (it refuses any nesting)
--   rt_sample(id,x,y)   read one pixel back out as a scalar, so exploration
--                       is queryable game state rather than invisible pixels
--
-- How the fog works, in four steps:
--
--   1. reveal   every frame stamp the viewer's footprint into the mask target
--               additively. Nothing clears it, so exploration accumulates:
--               once seen, always remembered
--   2. scene    draw the visible tiles into a second target through that
--               target's own camera (rt_camera), so what lands there is
--               exactly what a camera at this offset would see
--   3. composite onto that same target, still under the world camera, draw
--               the mask stretched over the whole world with multiply. Tiles
--               and mask share one transform, so they line up for free
--   4. present  blit the target across the screen, then draw the HUD
--
-- The minimap is the same trick at 1/8 scale, with two extra touches: the
-- tile art is baked once into its own target instead of being redrawn every
-- frame, and clicking the map jumps the view (and the explorer) there.
--
-- FakeLua notes: chunk-level locals must be initialised, natives take an
-- exact argument count, % works but math.fmod does not, and int64 parameters
-- (rt_create, rt_sample, draw_set_blend, mouse buttons) reject floats — hence
-- the `128 + 0` idiom and integer-only index arithmetic.

local SCREEN_W = 960 + 0
local SCREEN_H = 540 + 0

local TILE = 32 + 0
local COLS = 32 + 0
local ROWS = 24 + 0
local WORLD_W = 1024 + 0
local WORLD_H = 768 + 0

-- One mask pixel covers 8x8 world pixels (1024/128 = 8), and the minimap is
-- drawn 4px per tile into a canvas of the same size, so a mask blit over it
-- is aligned without any extra arithmetic.
local MASK_W = 128 + 0
local MASK_H = 96 + 0

local PANEL_X = 792 + 0
local PANEL_Y = 380 + 0
local VIEW_W = 120 + 0
local VIEW_H = 68 + 0

local SPEED = 190 + 0



-- FakeLua binds file-level locals as const, so everything that changes
-- across frames lives in a host-side NativeObject instead (see
-- docs/SCRIPTING.md rule 3). Fields read and write like a plain table.
local S = new_global_obj("fog_demo", "state")

local function reset_state()
    S.px = 512.0 + 0.0
    S.py = 384.0 + 0.0
    S.camx = 0.0 + 0.0
    S.camy = 0.0 + 0.0
    S.fog_on = true
    S.baked = false
    S.frame = 0 + 0
    S.explored = 0.0 + 0.0
    S.mask_rt = 0 + 0
    S.scene_rt = 0 + 0
    S.mini_base = 0 + 0
    S.mini_rt = 0 + 0
    S.has_rt = false
end

local function fract(v)
    return v - math.floor(v)
end

local function istr(n)
    return string.format("%d", n)
end

-- Deterministic per-tile noise: no assets, no RNG, same world every run.
local function tile_rand(cx, cy)
    return fract(math.sin(cx * 12.9898 + cy * 78.233) * 43758.5453)
end

-- 0 grass, 1 rock, 2 water, 3 wall. The border is always wall, which is also
-- what keeps the explorer inside the world.
local function tile_kind(cx, cy)
    if cx <= 0 or cy <= 0 or cx >= COLS - 1 or cy >= ROWS - 1 then
        return 3
    end
    local r = tile_rand(cx, cy)
    if r < 0.12 then
        return 2
    end
    if r < 0.30 then
        return 1
    end
    if r < 0.36 then
        return 3
    end
    return 0
end

local function blocked(wx, wy)
    local cx = math.floor(wx / 32.0)
    local cy = math.floor(wy / 32.0)
    return tile_kind(cx, cy) >= 2
end

local function draw_tile(cx, cy, size, ox, oy)
    local kind = tile_kind(cx, cy)
    local r = 0.26
    local g = 0.44
    local b = 0.26
    if kind == 1 then
        r = 0.42
        g = 0.40
        b = 0.36
    end
    if kind == 2 then
        r = 0.20
        g = 0.38
        b = 0.62
    end
    if kind == 3 then
        r = 0.30
        g = 0.28
        b = 0.30
    end
    draw_quad(ox + cx * size, oy + cy * size, size - 1.0, size - 1.0,
              r, g, b, 1.0)
end

local function draw_range(cx0, cy0, cx1, cy1, size, ox, oy)
    local cx = cx0
    while cx < cx1 do
        local cy = cy0
        while cy < cy1 do
            draw_tile(cx, cy, size, ox, oy)
            cy = cy + 1
        end
        cx = cx + 1
    end
end

-- Only the tiles the camera can see are drawn; `ox`/`oy` let the same routine
-- serve the target (0, 0 — the camera does the shifting) and the no-framebuffer
-- fallback (-camx, -camy — the script does it).
local function draw_world(ox, oy, size)
    local cx0 = math.floor(S.camx / 32.0) - 1
    local cy0 = math.floor(S.camy / 32.0) - 1
    local cx1 = math.floor((S.camx + SCREEN_W) / 32.0) + 2
    local cy1 = math.floor((S.camy + SCREEN_H) / 32.0) + 2
    if cx0 < 0 then
        cx0 = 0
    end
    if cy0 < 0 then
        cy0 = 0
    end
    if cx1 > COLS then
        cx1 = COLS
    end
    if cy1 > ROWS then
        cy1 = ROWS
    end
    draw_range(cx0, cy0, cx1, cy1, size, ox, oy)
    return 0
end

local function clamp_view()
    if S.camx < 0.0 then
        S.camx = 0.0
    end
    if S.camy < 0.0 then
        S.camy = 0.0
    end
    if S.camx > WORLD_W - SCREEN_W then
        S.camx = WORLD_W - SCREEN_W
    end
    if S.camy > WORLD_H - SCREEN_H then
        S.camy = WORLD_H - SCREEN_H
    end
    return 0
end

local function center_view()
    S.camx = S.px - SCREEN_W * 0.5
    S.camy = S.py - SCREEN_H * 0.5
    clamp_view()
    return 0
end

-- Coverage sampling: how much of the world has light on it. Cheap because it
-- runs every few frames on a coarse grid; rt_sample reads a real pixel back.
local function measure_explored()
    local hits = 0 + 0
    local i = 0 + 0
    while i < 8 do
        local j = 0 + 0
        while j < 6 do
            if rt_sample(S.mask_rt, i * 16 + 8, j * 16 + 8) > 0.1 then
                hits = hits + 1
            end
            j = j + 1
        end
        i = i + 1
    end
    S.explored = hits / 48.0
    return 0
end

local function bake_minimap()
    rt_begin(S.mini_base)
    rt_clear(0.04, 0.05, 0.08, 1.0)
    draw_range(0, 0, COLS, ROWS, 4.0, 0.0, 0.0)
    rt_end()
    S.baked = true
    return 0
end

local function move_viewer(dt)
    local mx = 0.0
    local my = 0.0
    if input_key_down("left") or input_key_down("a") then
        mx = -1.0
    end
    if input_key_down("right") or input_key_down("d") then
        mx = 1.0
    end
    if input_key_down("up") or input_key_down("w") then
        my = -1.0
    end
    if input_key_down("down") or input_key_down("s") then
        my = 1.0
    end
    if mx ~= 0.0 and my ~= 0.0 then
        mx = mx * 0.7071
        my = my * 0.7071
    end
    local step = SPEED * dt
    if mx ~= 0.0 and not blocked(S.px + mx * step, S.py) then
        S.px = S.px + mx * step
    end
    if my ~= 0.0 and not blocked(S.px, S.py + my * step) then
        S.py = S.py + my * step
    end
    center_view()
    return 0
end

-- Click-to-jump: the panel is 1/8 scale, so panel pixels are world pixels
-- going the other way. Only walkable destinations are accepted.
local function jump_from_click()
    local cx = input_mouse_x()
    local cy = input_mouse_y()
    if input_mouse_pressed(0) then
        if cx >= PANEL_X and cx <= PANEL_X + MASK_W then
            if cy >= PANEL_Y and cy <= PANEL_Y + MASK_H then
                local wx = (cx - PANEL_X) * 8.0
                local wy = (cy - PANEL_Y) * 8.0
                if not blocked(wx, wy) then
                    S.px = wx
                    S.py = wy
                    center_view()
                end
            end
        end
    end
    return 0
end

local function draw_hud()
    draw_text("FOG OF WAR", 40, 24, 1.2, 0.94, 0.96, 1.0, 1.0)
    draw_text("accumulating coverage mask, multiplied over the scene", 40, 52,
              0.7, 0.55, 0.66, 0.75, 1.0)
    draw_text("pos " .. istr(math.floor(S.px)) .. "," .. istr(math.floor(S.py)),
              40, 80, 0.62, 0.62, 0.70, 0.80, 1.0)
    if not S.fog_on then
        draw_text("fog OFF", 40, 100, 0.62, 0.95, 0.70, 0.45, 1.0)
    end
    draw_text("WASD move   F fog   R forget   click the minimap to jump",
              40, 470, 0.7, 0.5, 0.6, 0.7, 1.0)
    draw_text("explored " .. istr(math.floor(S.explored * 100.0)) .. "%",
              792, 356, 0.62, 0.80, 0.86, 0.95, 1.0)
    return 0
end

function update(dt)
    if S.seeded == nil then
        S.seeded = true
        reset_state()
    end

    S.frame = S.frame + 1

    if not S.has_rt then
        S.mask_rt = rt_create(MASK_W, MASK_H)
        S.scene_rt = rt_create(SCREEN_W, SCREEN_H)
        S.mini_base = rt_create(MASK_W, MASK_H)
        S.mini_rt = rt_create(MASK_W, MASK_H)
        S.has_rt = S.mask_rt > 0 and S.scene_rt > 0 and S.mini_base > 0 and S.mini_rt > 0
        if S.has_rt then
            rt_begin(S.mask_rt)
            rt_clear(0.0, 0.0, 0.0, 1.0)
            rt_end()
        end
    end

    move_viewer(dt)
    if input_key_pressed("f") then
        S.fog_on = not S.fog_on
    end
    if S.has_rt and input_key_pressed("r") then
        rt_begin(S.mask_rt)
        rt_clear(0.0, 0.0, 0.0, 1.0)
        rt_end()
    end
    jump_from_click()

    if not S.has_rt then
        -- No framebuffers on this host: explore the world with no fog, so the
        -- demo is still playable and the failure is visible rather than silent.
        draw_world(-S.camx, -S.camy, 32.0)
        draw_quad(S.px - 8.0 - S.camx, S.py - 8.0 - S.camy, 16.0, 16.0,
                  0.95, 0.85, 0.35, 1.0)
        draw_hud()
        draw_text("no render targets here - running without fog", 40, 496, 0.62,
                  0.75, 0.60, 0.40, 1.0)
        return 0
    end

    -- --- 1. reveal into the accumulating mask ----------------------------
    -- Additive, never cleared: three visits saturate a cell to full white.
    -- The stamp is the viewer's footprint in mask pixels (world / 8).
    rt_begin(S.mask_rt)
    draw_set_blend(1)
    draw_quad(S.px / 8.0 - 13.0, S.py / 8.0 - 13.0, 26.0, 26.0, 0.35, 0.35, 0.35, 1.0)
    draw_set_blend(0)
    rt_end()

    -- --- 2 & 3. the scene, then the mask over it -------------------------
    rt_begin(S.scene_rt)
    rt_clear(0.02, 0.03, 0.05, 1.0)
    rt_camera(S.camx, S.camy)
    draw_world(0.0, 0.0, 32.0)
    draw_quad(S.px - 8.0, S.py - 8.0, 16.0, 16.0, 0.95, 0.85, 0.35, 1.0)
    if S.fog_on then
        -- Same camera transform as the tiles above, so this blit is aligned
        -- with them by construction: one draw covers the whole world.
        draw_set_blend(2)
        rt_draw(S.mask_rt, 0.0, 0.0, WORLD_W, WORLD_H)
        draw_set_blend(0)
    end
    rt_end()

    if S.frame % 12 == 0 then
        measure_explored()
    end
    if not S.baked then
        bake_minimap()
    end

    -- --- the minimap: baked art multiplied by the same mask --------------
    rt_begin(S.mini_rt)
    rt_clear(0.0, 0.0, 0.0, 1.0)
    draw_set_blend(0)
    rt_draw(S.mini_base, 0.0, 0.0, MASK_W, MASK_H)
    draw_set_blend(2)
    rt_draw(S.mask_rt, 0.0, 0.0, MASK_W, MASK_H)
    draw_set_blend(0)
    rt_end()

    -- --- 4. present, then the HUD ----------------------------------------
    rt_present(S.scene_rt, 0)

    draw_set_blend(0)
    draw_quad(PANEL_X - 2.0, PANEL_Y - 2.0, MASK_W + 4.0, MASK_H + 4.0,
              0.10, 0.12, 0.18, 1.0)
    rt_draw(S.mini_rt, PANEL_X, PANEL_Y, MASK_W, MASK_H)
    draw_rect_outline(PANEL_X - 2.0, PANEL_Y - 2.0, MASK_W + 4.0, MASK_H + 4.0,
                      0.45, 0.55, 0.70, 1.0)
    -- Where the camera is looking, in panel pixels.
    draw_rect_outline(PANEL_X + S.camx * 0.125, PANEL_Y + S.camy * 0.125,
                      VIEW_W, VIEW_H, 0.95, 0.90, 0.55, 1.0)
    draw_quad(PANEL_X + S.px * 0.125 - 2.0, PANEL_Y + S.py * 0.125 - 2.0, 4.0, 4.0,
              0.98, 0.95, 0.70, 1.0)
    draw_hud()
    return 0
end
