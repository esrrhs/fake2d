-- fake2d pure-Lua offscreen render target demo (--rt-demo).
--
-- Two classic FBO uses, side by side:
--
--   * BAKE ONCE — procedural art (grid + starfield + caption) is rendered
--     into a target on the first frame and blitted every frame afterwards, at
--     any size. The expensive part happens exactly once; a per-frame rebuild
--     would redraw ~180 quads that never change.
--
--   * FEEDBACK TRAIL — the trail target is *not* cleared: it is covered by a
--     translucent quad each frame and the mover is drawn on top, so motion
--     leaves a decaying tail. The right pane runs the same motion in
--     immediate mode for contrast.
--
-- Keys: WASD / arrows steer, SPACE toggles the trail layer, C clears it,
-- R respawns the mover. With no input the mover keeps bouncing on its own, so
-- a headless run shows the effect without interaction.
--
-- FakeLua note: per-frame tables are reset at the frame boundary, so all
-- state lives in plain numeric file locals (see scripts/map_demo.lua).

local BAKE_W = 256 + 0
local BAKE_H = 256 + 0
local TRAIL_W = 480 + 0
local TRAIL_H = 320 + 0
local COMP_W = 300 + 0
local COMP_H = 200 + 0



-- Deterministic LCG: the baked starfield is identical on every run, so CI
-- screenshots stay comparable. Kept in a range doubles represent exactly.
-- FakeLua binds file-level locals as const, so everything that changes
-- across frames lives in a host-side NativeObject instead (see
-- docs/SCRIPTING.md rule 3). Fields read and write like a plain table.
local S = new_global_obj("rt_demo", "state")

local function reset_state()
    S.bake_id = 0 + 0
    S.trail_id = 0 + 0
    S.inited = false
    S.supported = false
    S.show_trail = true
    S.px = 240.0 + 0.0
    S.py = 160.0 + 0.0
    S.vx = 165.0 + 0.0
    S.vy = 118.0 + 0.0
    S.seed = 20240607 + 0
end

local function rnd()
    S.seed = math.fmod(S.seed * 7 + 13, 1000000)
    return S.seed / 1000000
end

-- Rendered exactly once (first frame). Everything below is drawn in the
-- target's own pixel space: (0,0) top-left, 1 unit = 1 pixel.
local function bake_background()
    rt_begin(S.bake_id)
    rt_clear(0.035, 0.045, 0.09, 1.0)

    local g = 0 + 0
    while g <= BAKE_W do
        draw_quad(g, 0, 1, BAKE_H, 0.13, 0.26, 0.38, 1.0)
        draw_quad(0, g, BAKE_W, 1, 0.13, 0.26, 0.38, 1.0)
        g = g + 32
    end

    local i = 0 + 0
    while i < 90 do
        local sx = math.floor(rnd() * BAKE_W)
        local sy = math.floor(rnd() * BAKE_H)
        local b = 0.35 + rnd() * 0.65
        draw_quad(sx, sy, 2, 2, b, b, b * 0.92, 1.0)
        i = i + 1
    end

    -- Captions baked into the texture itself: blitting the target later also
    -- reproduces the text, at whatever size the blit uses.
    draw_text("BAKED ONCE", 56, 116, 1.0, 0.55, 0.92, 0.94, 1.0)
    draw_quad(56, 144, 148, 2, 0.55, 0.92, 0.94, 0.70)
    draw_text("256x256 target", 56, 154, 0.7, 0.45, 0.62, 0.72, 1.0)
    rt_end()
end

local function clear_trail()
    rt_begin(S.trail_id)
    rt_clear(0.02, 0.03, 0.06, 1.0)
    rt_end()
end

local function update_motion(dt)
    local ax = 0.0 + 0.0
    local ay = 0.0 + 0.0
    if input_key_down("left") or input_key_down("a") then ax = ax - 900.0 end
    if input_key_down("right") or input_key_down("d") then ax = ax + 900.0 end
    if input_key_down("up") or input_key_down("w") then ay = ay - 900.0 end
    if input_key_down("down") or input_key_down("s") then ay = ay + 900.0 end

    if ax ~= 0.0 or ay ~= 0.0 then
        S.vx = S.vx + ax * dt
        S.vy = S.vy + ay * dt
        -- Clamp so steering cannot fling the mover off the canvas.
        if S.vx > 420.0 then S.vx = 420.0 end
        if S.vx < -420.0 then S.vx = -420.0 end
        if S.vy > 420.0 then S.vy = 420.0 end
        if S.vy < -420.0 then S.vy = -420.0 end
    end

    S.px = S.px + S.vx * dt
    S.py = S.py + S.vy * dt

    if S.px < 8.0 then
        S.px = 8.0
        S.vx = -S.vx
    end
    if S.px > TRAIL_W - 8.0 then
        S.px = TRAIL_W - 8.0
        S.vx = -S.vx
    end
    if S.py < 8.0 then
        S.py = 8.0
        S.vy = -S.vy
    end
    if S.py > TRAIL_H - 8.0 then
        S.py = TRAIL_H - 8.0
        S.vy = -S.vy
    end
end

function update(dt)
    if S.seeded == nil then
        S.seeded = true
        reset_state()
    end

    if not S.inited then
        S.inited = true
        S.bake_id = rt_create(BAKE_W, BAKE_H)
        S.trail_id = rt_create(TRAIL_W, TRAIL_H)
        S.supported = rt_valid(S.bake_id) and rt_valid(S.trail_id)
        if S.supported then
            bake_background()
            clear_trail()
        end
    end

    if not S.supported then
        draw_text("RENDER TARGETS UNSUPPORTED ON THIS GL CONTEXT", 40, 40, 1.0,
                  1.0, 0.4, 0.4, 1.0)
        return 0
    end

    if input_key_pressed("space") then S.show_trail = not S.show_trail end
    if input_key_pressed("c") then clear_trail() end
    if input_key_pressed("r") then
        S.px = 240.0
        S.py = 160.0
        S.vx = 165.0
        S.vy = 118.0
        clear_trail()
    end

    update_motion(dt)

    -- Accumulate: fade the previous frame instead of clearing it. That decay
    -- is the whole effect, and it can only live in a persistent surface.
    rt_begin(S.trail_id)
    draw_quad(0, 0, TRAIL_W, TRAIL_H, 0.02, 0.03, 0.06, 0.09)
    draw_quad(S.px - 7.0, S.py - 7.0, 14, 14, 1.0, 0.78, 0.25, 1.0)
    rt_end()

    draw_text("OFFSCREEN RENDER TARGETS", 40, 24, 1.2, 0.94, 0.96, 1.0, 1.0)
    draw_text("draw into a target with rt_begin / rt_end, blit it with rt_draw",
              40, 52, 0.7, 0.55, 0.66, 0.75, 1.0)

    -- Left: the baked surface, blitted at two sizes from one texture.
    draw_text("BAKE ONCE", 60, 82, 0.75, 0.55, 0.92, 0.94, 1.0)
    rt_draw(S.bake_id, 60, 100, BAKE_W, BAKE_H)
    draw_rect_outline(60, 100, BAKE_W, BAKE_H, 0.30, 0.45, 0.55, 1.0)
    draw_text("same texture at half size", 60, 372, 0.65, 0.45, 0.62, 0.72, 1.0)
    rt_draw(S.bake_id, 60, 392, 128, 128)
    draw_rect_outline(60, 392, 128, 128, 0.30, 0.45, 0.55, 1.0)

    -- Middle: the accumulating trail.
    draw_text("FEEDBACK TRAIL (accumulating)", 400, 82, 0.75, 1.0, 0.80, 0.35, 1.0)
    if S.show_trail then
        rt_draw(S.trail_id, 400, 100, TRAIL_W, TRAIL_H)
    else
        draw_quad(400, 100, TRAIL_W, TRAIL_H, 0.02, 0.03, 0.06, 1.0)
    end
    draw_rect_outline(400, 100, TRAIL_W, TRAIL_H, 0.55, 0.45, 0.20, 1.0)

    -- Right: identical motion, drawn straight to the screen (no history).
    draw_text("IMMEDIATE MODE (no history)", 900, 82, 0.75, 0.62, 0.86, 0.98, 1.0)
    draw_quad(900, 100, COMP_W, COMP_H, 0.02, 0.03, 0.06, 1.0)
    local cx = 900 + S.px / TRAIL_W * COMP_W
    local cy = 100 + S.py / TRAIL_H * COMP_H
    draw_quad(cx - 5.0, cy - 5.0, 10, 10, 1.0, 0.78, 0.25, 1.0)
    draw_rect_outline(900, 100, COMP_W, COMP_H, 0.30, 0.45, 0.55, 1.0)

    draw_text("WASD / arrows steer   SPACE toggle trail   C clear   R respawn",
              40, 660, 0.7, 0.5, 0.6, 0.7, 1.0)
    return 0
end
