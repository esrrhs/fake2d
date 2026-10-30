-- Phase 8 slope lab.
--
-- Three hills on flat ground, each exercising a different thing about Tiled
-- slope tiles, plus a surface overlay that draws what map_ground_y actually
-- reports. Controls:
--
--   A/D or arrows  move          Shift  run
--   Space/W/Up     jump          R      reset to the start
--   V              toggle the surface overlay
--
-- Headless hooks: SLOPE_TEST_RIGHT=1 holds right, SLOPE_TEST_JUMP=1 holds jump,
-- SLOPE_TEST_SURFACE=1 starts with the overlay on. The feet height is logged as
-- 900xxx every 30 frames and each jump launch as 910xxx + px, so a climb (or a
-- "cannot jump" regression) can be verified without a human at the keyboard.
--
-- Two rules decide whether a hill is walkable, and both were learned the hard
-- way by the first slope terrain that shipped in the mario demo:
--
--   1. A ramp cell carries `slope` and nothing else, and the dirt packed under
--      it is decorative — no `solid`. A solid cell under the ramp is what
--      makes a slope unwalkable: box_hits_solid sees the body, so the player's
--      horizontal sweep is blocked at the first column boundary and they walk
--      two steps and stall. Support on a ramp comes from map_ground_y.
--
--   2. Do not cap a crest with a solid cell. An `up` ramp's upper end and the
--      `down` ramp's lower end already meet exactly across a column boundary
--      (col5 ends at 320, col6 starts at 320), so no cap is needed — and a cap
--      actively stalls a walker, because the 28px box still covers the crest's
--      row when the feet are at ramp height, so the horizontal sweep is blocked
--      by the very cell it stands on. On the 45-degree hills that step is only
--      ~10px and the step-up assist in move_x can carry it; on the rise-64 hill
--      it is 20px, over the 16px assist, and the player stops dead. Hence no
--      section here uses a cap; the assist stays for hand-made stepped crests.

local TILE = 32
local MAP_W = 48 * TILE
local GROUND_TOP = 13 * TILE

-- section anchors, used for the labels
local SECTIONS = {
    { x = 3 * TILE, label = "A  RISE 32  45 deg" },
    { x = 14 * TILE, label = "B  RISE 64  63 deg" },
    { x = 22 * TILE, label = "C  RISE 32  LONG" }
}

local WALK_SPEED, RUN_SPEED = 150.0, 235.0
local G_ACC, A_ACC = 2100.0, 1250.0
local FRICTION = 1900.0
local GRAV, MAX_FALL = 1500.0, 700.0
local JUMP_V = 590.0
local JUMP_CUT = 0.42
local COYOTE, JUMP_BUFFER = 0.12, 0.14
-- How far the feet may be lifted / dropped onto a ramp in one frame. A 45
-- degree hill moves 1px per px and a rise-64 hill 2px per px, so at RUN_SPEED
-- the surface moves up to ~8px per frame; the snap has to keep up with that.
local SNAP_UP, SNAP_DOWN = 26.0, 40.0
-- How far a blocked move may lift the player onto a higher surface. This is
-- what carries the player over a solid-capped crest (section B).
local STEP_UP = 16.0


local box_w, box_h = 20.0 + 0.0, 28.0 + 0.0
-- walk-cycle phase for the 4-frame strip (idle/stride/stride/airborne)
-- HUD readout: the last slope cell we were attached to.

-- FakeLua binds file-level locals as const, so everything that changes
-- across frames lives in a host-side NativeObject instead (see
-- docs/SCRIPTING.md rule 3). Fields read and write like a plain table.
local S = new_global_obj("slope_demo", "state")

local function reset_state()
    S.map_id = 0 + 0
    S.tex_player = 0 + 0
    S.tex_sky = 0 + 0  -- AI-illustrated night sky behind the slope ridges
    S.anim_player = 0 + 0
    S.inited = false
    S.px = 0.0 + 0.0
    S.py = 0.0 + 0.0
    S.vx = 0.0 + 0.0
    S.vy = 0.0 + 0.0
    S.facing = 1 + 0
    S.walk_t = 0.0 + 0.0
    S.on_ground = false
    S.coyote_t = 0.0 + 0.0
    S.buffer_t = 0.0 + 0.0
    S.cut_used = false
    S.frame_n = 0 + 0
    S.show_surface = false
    S.cam_x = 0.0 + 0.0
    S.cam_y = 0.0 + 0.0
    S.cam_vw = 960.0 + 0.0
    S.cam_vh = 540.0 + 0.0
    S.test_right = false
    S.test_jump = false
    S.last_dir = 0 + 0
    S.last_rise = 0 + 0
    S.snap_count = 0 + 0
end

local function istr(n)
    return string.format("%d", n)
end

-- JIT-compatible modulo (math.fmod is not supported by the JIT codegen)
local function imod(a, b)
    return a - math.floor(a / b) * b
end

local function solid_at(c, r)
    return map_solid(S.map_id, c, r)
end

local function box_hits_solid(nx, ny, w, h)
    local c0 = math.floor(nx / TILE)
    local c1 = math.floor((nx + w - 0.01) / TILE)
    local r0 = math.floor(ny / TILE)
    local r1 = math.floor((ny + h - 0.01) / TILE)
    for r = r0, r1 do
        for c = c0, c1 do
            if solid_at(c, r) then return true end
        end
    end
    return false
end

-- Walkable surface under a world x, or -1 for none.
--
-- GroundYAt only scans the rows within one tile of `reach_y` and keeps the
-- lowest candidate in that window, so a single query can miss the ramp cell one
-- row above the feet — which is exactly the cell being climbed onto. Probing
-- one tile up and one tile down and keeping the nearer answer covers both the
-- climb and the descent.
local function ground_probe(wx, feet)
    local lo = map_ground_y(S.map_id, wx, feet - TILE)
    local hi = map_ground_y(S.map_id, wx, feet + TILE)
    if lo >= 0.0 and hi >= 0.0 then
        if hi < lo then return hi end
        return lo
    end
    if lo >= 0.0 then return lo end
    return hi
end

-- Surface sample for the overlay: sweep the reach so a column can be read no
-- matter how far its surface is from the player.
local function surface_at(wx, near_y)
    for k = 0, 8 do
        local reach = near_y + (k - 4) * TILE
        local y = map_ground_y(S.map_id, wx, reach)
        if y >= 0.0 then return y end
    end
    return -1.0
end

-- Put the feet on the ramp. Called every frame: reads the interpolated surface
-- and follows it, so walking right carries the player up the near side and down
-- the far side with no per-tile math. Any leftover fall speed is zeroed so it
-- cannot fight the snap.
--
-- The snap window is the only guard. An earlier version also required a slope
-- cell in the feet's own row (or the one above) and bailed out otherwise, which
-- silently disabled the whole thing on a rise-64 hill: that ramp's surface
-- spans two cells, so its cell sits two rows ABOVE the feet while the player is
-- still down at the bottom of the climb. The window (±26/+40px) already rejects
-- the cases a slope-cell check was meant to catch — a jump arc passing over a
-- hill is far more than 40px above it, and a surface more than 26px above the
-- feet is a wall, not a ramp.
local function snap_to_slope()
    local feet = S.py + box_h
    -- Never snap while rising. The surface below is still inside the +40px
    -- window for the first frames of a jump, so without this the snap drags the
    -- player straight back down and zeroes vy — the jump is cancelled before it
    -- gets anywhere, which is what "cannot jump" looks like. Slope walking is
    -- unaffected: there the snap itself moves the feet up, so vy stays 0.
    if S.vy < 0.0 then return end
    local cx = S.px + box_w * 0.5
    local col = math.floor(cx / TILE)
    local r_now = math.floor(feet / TILE)
    -- HUD readout only: look a couple of rows around the feet for the ramp.
    for r = r_now - 2, r_now + 1 do
        local d = map_slope_dir(S.map_id, col, r)
        if d ~= 0 then
            S.last_dir = d
            local rise = map_slope_rise(S.map_id, col, r)
            S.last_rise = rise > 0 and rise or TILE
            break
        end
    end
    local g = ground_probe(cx, feet)
    if g < 0.0 then return end
    if g <= feet - SNAP_UP or g >= feet + SNAP_DOWN then return end
    S.py = g - box_h
    if S.vy > 0.0 then S.vy = 0.0 end
    S.on_ground = true
    -- Slope contact is ground contact: re-arm the variable-jump cut here too.
    -- The solid probe below only fires on real tiles, so on a ramp cut_used
    -- stayed true after the first jump and every later jump was refused.
    S.cut_used = false
    S.snap_count = S.snap_count + 1
end

local function move_x(dx)
    local nx = S.px + dx
    if not box_hits_solid(nx, S.py, box_w, box_h) then
        S.px = nx
        return
    end
    -- Blocked. If the walkable surface just past the leading edge is a small
    -- step up, lift onto it and keep moving: this is what carries the player
    -- over a solid-capped crest instead of stopping dead against it.
    local feet = S.py + box_h
    local edge = nx
    if dx > 0.0 then edge = nx + box_w end
    local g = ground_probe(edge, feet)
    if g >= 0.0 and g < feet and (feet - g) <= STEP_UP then
        S.py = g - box_h
        if S.vy > 0.0 then S.vy = 0.0 end
        S.px = nx
        S.on_ground = true
        return
    end
    if dx > 0.0 then
        S.px = math.floor((nx + box_w) / TILE) * TILE - box_w
    else
        S.px = (math.floor(nx / TILE) + 1) * TILE
    end
    S.vx = 0.0
end

local function move_y(dy)
    local ny = S.py + dy
    if not box_hits_solid(S.px, ny, box_w, box_h) then
        S.py = ny
        return
    end
    if dy > 0.0 then
        S.py = math.floor((ny + box_h) / TILE) * TILE - box_h
        S.vy = 0.0
        S.on_ground = true
    else
        S.py = (math.floor(ny / TILE) + 1) * TILE
        S.vy = 0.0
    end
end

local function reset_player()
    S.px = 1 * TILE
    S.py = GROUND_TOP - box_h
    S.vx, S.vy = 0.0, 0.0
    S.on_ground = true
    S.coyote_t, S.buffer_t = 0.0, 0.0
    S.cut_used = false
    S.cam_x = 0.0
    camera_snap(0.0, 0.0)
    S.snap_count = 0
end

local function step_player(dt, intent_x, running, jump_pressed, jump_held)
    local target = running and RUN_SPEED or WALK_SPEED
    if intent_x > 0.0 then
        local rate = S.on_ground and G_ACC or A_ACC
        if S.vx < target then S.vx = math.min(target, S.vx + rate * dt) end
    elseif intent_x < 0.0 then
        local rate = S.on_ground and G_ACC or A_ACC
        if S.vx > -target then S.vx = math.max(-target, S.vx - rate * dt) end
    elseif S.on_ground then
        if S.vx > 0.0 then S.vx = math.max(0.0, S.vx - FRICTION * dt) end
        if S.vx < 0.0 then S.vx = math.min(0.0, S.vx + FRICTION * dt) end
    end

    if jump_pressed then S.buffer_t = JUMP_BUFFER end
    if S.on_ground then S.coyote_t = COYOTE end

    S.vy = math.min(MAX_FALL, S.vy + GRAV * dt)

    -- substep so a fast run cannot tunnel through a tile
    local ddx, ddy = S.vx * dt, S.vy * dt
    local n = math.ceil(math.max(math.abs(ddx), math.abs(ddy)) / 8.0)
    if n < 1 then n = 1 end
    for _ = 1, n do
        move_x(ddx / n)
        S.on_ground = false
        move_y(ddy / n)
    end

    local launched = false
    if S.buffer_t > 0.0 and S.coyote_t > 0.0 and not S.cut_used then
        S.vy = 0.0 - JUMP_V
        S.on_ground = false
        S.coyote_t = 0.0
        S.buffer_t = 0.0
        S.cut_used = true
        launched = true
        log_number(910000 + math.floor(S.px))
    end
    if not launched and not jump_held and S.vy < 0.0 and S.cut_used then
        S.vy = S.vy * JUMP_CUT
        S.cut_used = false
    end

    -- solid ground contact re-arms the jump cut
    if S.vy >= 0.0 and box_hits_solid(S.px, S.py + 2.0, box_w, box_h) then
        S.on_ground = true
        S.cut_used = false
    end
    snap_to_slope()

    if S.py > GROUND_TOP + 140.0 then
        reset_player()
    end
end

local function read_input()
    local move = 0.0
    if input_key_down("left") or input_key_down("a") then move = -1.0 end
    if input_key_down("right") or input_key_down("d") then move = 1.0 end
    local running = input_key_down("left_shift") or input_key_down("right_shift")
    local jp = input_key_pressed("space") or input_key_pressed("w")
        or input_key_pressed("up")
    local jh = input_key_down("space") or input_key_down("w") or input_key_down("up")
    -- Phase 9: gamepad controls mirror the keyboard layout.
    if input_pad_connected(0) then
        local lx = input_pad_axis(0, "left_x")
        if lx < -0.5 or input_pad_down(0, "dpad_left") then move = -1.0 end
        if lx > 0.5 or input_pad_down(0, "dpad_right") then move = 1.0 end
        if input_pad_down(0, "x") then running = true end
        if input_pad_pressed(0, "a") then jp = true end
        if input_pad_down(0, "a") then jh = true end
    end
    if S.test_right then
        move = 1.0
        running = true
    end
    if S.test_jump then
        jp = true
        jh = true
    end
    return move, running, jp, jh
end

local function draw_surface()
    local feet = S.py + box_h
    local x0 = math.floor(S.cam_x)
    local x1 = x0 + S.cam_vw
    local wx = x0
    while wx <= x1 do
        local y = surface_at(wx + 0.5, feet)
        if y >= 0.0 then
            draw_quad(wx - 1.0, y - 1.0, 3.0, 3.0, 1.0, 0.35, 0.35, 0.95)
        end
        wx = wx + 12
    end
end

local function draw_labels()
    for i = 1, #SECTIONS do
        local s = SECTIONS[i]
        if s.x > S.cam_x - 240.0 and s.x < S.cam_x + S.cam_vw + 240.0 then
            draw_text(s.label, s.x, S.cam_y + 62.0, 0.5, 1.0, 0.95, 0.6, 1.0)
        end
    end
end

local function draw_hud()
    local sx = S.cam_x
    local sy = S.cam_y
    draw_text("SLOPE LAB   A/D move   SPACE jump   SHIFT run   V surface   R reset",
              sx + 16.0, sy + 12.0, 0.45, 1.0, 1.0, 1.0, 1.0)
    local feet = S.py + box_h
    local col = math.floor((S.px + box_w * 0.5) / TILE)
    local r = math.floor(feet / TILE)
    local dir = map_slope_dir(S.map_id, col, r)
    if dir == 0 then dir = S.last_dir end
    local word = "flat"
    if dir == 1 then word = "up" elseif dir == 2 then word = "down" end
    draw_text("col " .. istr(col) .. "   feet " .. istr(math.floor(feet))
                  .. "   slope " .. word .. "   rise " .. istr(S.last_rise)
                  .. "   snaps " .. istr(S.snap_count),
              sx + 16.0, sy + 32.0, 0.45, 0.75, 0.95, 1.0, 1.0)
    if S.show_surface then
        draw_text("surface overlay on: red dots are map_ground_y samples",
                  sx + 16.0, sy + 50.0, 0.42, 1.0, 0.55, 0.55, 1.0)
    end
end

function update(dt)
    if S.seeded == nil then
        S.seeded = true
        reset_state()
    end

    S.frame_n = S.frame_n + 1
    local sdt = dt
    if sdt > 0.034 then sdt = 0.034 end

    if not S.inited then
        S.inited = true
        S.map_id = map_load("assets/slope.json")
        S.tex_player = sprite_load("assets/s_player.png")
        S.tex_sky = sprite_load("assets/art/night_sky.png")
        -- 4-frame walk strip (idle / stride A / stride B / airborne)
        S.anim_player = anim_create()
        anim_frame(S.anim_player, S.tex_player, 0.0, 0.0, box_w, box_h)
        anim_frame(S.anim_player, S.tex_player, box_w, 0.0, box_w, box_h)
        anim_frame(S.anim_player, S.tex_player, box_w * 2.0, 0.0, box_w, box_h)
        anim_frame(S.anim_player, S.tex_player, box_w * 3.0, 0.0, box_w, box_h)
        anim_fps(S.anim_player, 10.0)
        anim_loop(S.anim_player, true)
        anim_pause(S.anim_player)
        S.test_right = os.getenv("SLOPE_TEST_RIGHT") ~= nil
        S.test_jump = os.getenv("SLOPE_TEST_JUMP") ~= nil
        S.show_surface = os.getenv("SLOPE_TEST_SURFACE") ~= nil
        reset_player()
        log_number(900001)
    end

    if input_key_pressed("v") then
        S.show_surface = not S.show_surface
        log_number(900100 + (S.show_surface and 1 or 0))
    end
    if input_key_pressed("r") then
        reset_player()
        log_number(900200)
    end
    -- Esc / gamepad Back leaves the lab.
    if input_key_pressed("escape") or input_pad_pressed(0, "back") then
        window_quit()
    end

    local mx, run, jp, jh = read_input()
    if mx > 0.1 then S.facing = 1 end
    if mx < -0.1 then S.facing = -1 end
    step_player(sdt, mx, run, jp, jh)

    S.cam_vw = camera_viewport_w()
    S.cam_vh = camera_viewport_h()
    -- Engine-owned SMB scroll: never moves left, clamped to the level, x
    -- bounds keep the view inside [0, MAP_W] (y bounds stay wide open).
    camera_forward_only(true)
    camera_smoothing(0.0)
    camera_set_bounds(0.0, -100000.0, MAP_W, 200000.0)
    camera_follow(S.px + box_w * 0.5 + S.cam_vw * 0.1, S.cam_vh * 0.5)
    S.cam_x = camera_x()
    S.cam_y = camera_y()
    -- Both ends are walls. Without the right one the player walks off the
    -- level, falls out of the world, gets teleported back to the start, and the
    -- whole view jumps with them.
    if S.px < S.cam_x then S.px = S.cam_x end
    if S.px > MAP_W - box_w then S.px = MAP_W - box_w end

    -- headless climb trace: feet height every 30 frames
    if imod(S.frame_n, 30.0) == 0.0 then
        log_number(900000 + math.floor(S.py + box_h))
    end
    if S.tex_sky ~= 0 then
        sprite_draw(S.tex_sky, S.cam_x, S.cam_y, S.cam_vw, S.cam_vh)
    else
        draw_quad(S.cam_x, S.cam_y, S.cam_vw, S.cam_vh, 0.36, 0.58, 0.95, 1.0)
    end
    map_draw(S.map_id)
    if S.show_surface then draw_surface() end
    -- face the direction of travel; the engine clip plays the walk strip
    -- (idle / stride A / stride B / airborne), state logic pins the frame.
    local pframe = 0 + 0
    if not S.on_ground then
        pframe = 3
    elseif math.abs(S.vx) > 12.0 then
        S.walk_t = S.walk_t + sdt * 8.0
        local phase = math.floor(S.walk_t) - math.floor(math.floor(S.walk_t) / 4.0) * 4.0
        if phase == 0.0 or phase == 2.0 then pframe = 1 else pframe = 2 end
    end
    anim_set_frame(S.anim_player, pframe)
    anim_draw_flip(S.anim_player, S.px, S.py, box_w, box_h, S.facing < 0, false)
    draw_labels()
    draw_hud()
    return 0
end
