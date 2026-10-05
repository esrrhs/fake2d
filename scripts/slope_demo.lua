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

local map_id = 0 + 0
local tex_player = 0 + 0
local inited = false

local px, py = 0.0 + 0.0, 0.0 + 0.0
local vx, vy = 0.0 + 0.0, 0.0 + 0.0
local box_w, box_h = 20.0 + 0.0, 28.0 + 0.0
local on_ground = false
local coyote_t, buffer_t = 0.0 + 0.0, 0.0 + 0.0
local cut_used = false
local frame_n = 0 + 0
local show_surface = false
local cam_x, cam_y = 0.0 + 0.0, 0.0 + 0.0
local cam_vw, cam_vh = 960.0 + 0.0, 540.0 + 0.0
local test_right = false
local test_jump = false
-- HUD readout: the last slope cell we were attached to.
local last_dir = 0 + 0
local last_rise = 0 + 0
local snap_count = 0 + 0

local function istr(n)
    return string.format("%d", n)
end

-- JIT-compatible modulo (math.fmod is not supported by the JIT codegen)
local function imod(a, b)
    return a - math.floor(a / b) * b
end

local function solid_at(c, r)
    return map_solid(map_id, c, r)
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
    local lo = map_ground_y(map_id, wx, feet - TILE)
    local hi = map_ground_y(map_id, wx, feet + TILE)
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
        local y = map_ground_y(map_id, wx, reach)
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
    local feet = py + box_h
    -- Never snap while rising. The surface below is still inside the +40px
    -- window for the first frames of a jump, so without this the snap drags the
    -- player straight back down and zeroes vy — the jump is cancelled before it
    -- gets anywhere, which is what "cannot jump" looks like. Slope walking is
    -- unaffected: there the snap itself moves the feet up, so vy stays 0.
    if vy < 0.0 then return end
    local cx = px + box_w * 0.5
    local col = math.floor(cx / TILE)
    local r_now = math.floor(feet / TILE)
    -- HUD readout only: look a couple of rows around the feet for the ramp.
    for r = r_now - 2, r_now + 1 do
        local d = map_slope_dir(map_id, col, r)
        if d ~= 0 then
            last_dir = d
            local rise = map_slope_rise(map_id, col, r)
            last_rise = rise > 0 and rise or TILE
            break
        end
    end
    local g = ground_probe(cx, feet)
    if g < 0.0 then return end
    if g <= feet - SNAP_UP or g >= feet + SNAP_DOWN then return end
    py = g - box_h
    if vy > 0.0 then vy = 0.0 end
    on_ground = true
    -- Slope contact is ground contact: re-arm the variable-jump cut here too.
    -- The solid probe below only fires on real tiles, so on a ramp cut_used
    -- stayed true after the first jump and every later jump was refused.
    cut_used = false
    snap_count = snap_count + 1
end

local function move_x(dx)
    local nx = px + dx
    if not box_hits_solid(nx, py, box_w, box_h) then
        px = nx
        return
    end
    -- Blocked. If the walkable surface just past the leading edge is a small
    -- step up, lift onto it and keep moving: this is what carries the player
    -- over a solid-capped crest instead of stopping dead against it.
    local feet = py + box_h
    local edge = nx
    if dx > 0.0 then edge = nx + box_w end
    local g = ground_probe(edge, feet)
    if g >= 0.0 and g < feet and (feet - g) <= STEP_UP then
        py = g - box_h
        if vy > 0.0 then vy = 0.0 end
        px = nx
        on_ground = true
        return
    end
    if dx > 0.0 then
        px = math.floor((nx + box_w) / TILE) * TILE - box_w
    else
        px = (math.floor(nx / TILE) + 1) * TILE
    end
    vx = 0.0
end

local function move_y(dy)
    local ny = py + dy
    if not box_hits_solid(px, ny, box_w, box_h) then
        py = ny
        return
    end
    if dy > 0.0 then
        py = math.floor((ny + box_h) / TILE) * TILE - box_h
        vy = 0.0
        on_ground = true
    else
        py = (math.floor(ny / TILE) + 1) * TILE
        vy = 0.0
    end
end

local function reset_player()
    px = 1 * TILE
    py = GROUND_TOP - box_h
    vx, vy = 0.0, 0.0
    on_ground = true
    coyote_t, buffer_t = 0.0, 0.0
    cut_used = false
    cam_x = 0.0
    snap_count = 0
end

local function step_player(dt, intent_x, running, jump_pressed, jump_held)
    local target = running and RUN_SPEED or WALK_SPEED
    if intent_x > 0.0 then
        local rate = on_ground and G_ACC or A_ACC
        if vx < target then vx = math.min(target, vx + rate * dt) end
    elseif intent_x < 0.0 then
        local rate = on_ground and G_ACC or A_ACC
        if vx > -target then vx = math.max(-target, vx - rate * dt) end
    elseif on_ground then
        if vx > 0.0 then vx = math.max(0.0, vx - FRICTION * dt) end
        if vx < 0.0 then vx = math.min(0.0, vx + FRICTION * dt) end
    end

    if jump_pressed then buffer_t = JUMP_BUFFER end
    if on_ground then coyote_t = COYOTE end

    vy = math.min(MAX_FALL, vy + GRAV * dt)

    -- substep so a fast run cannot tunnel through a tile
    local ddx, ddy = vx * dt, vy * dt
    local n = math.ceil(math.max(math.abs(ddx), math.abs(ddy)) / 8.0)
    if n < 1 then n = 1 end
    for _ = 1, n do
        move_x(ddx / n)
        on_ground = false
        move_y(ddy / n)
    end

    local launched = false
    if buffer_t > 0.0 and coyote_t > 0.0 and not cut_used then
        vy = 0.0 - JUMP_V
        on_ground = false
        coyote_t = 0.0
        buffer_t = 0.0
        cut_used = true
        launched = true
        log_number(910000 + math.floor(px))
    end
    if not launched and not jump_held and vy < 0.0 and cut_used then
        vy = vy * JUMP_CUT
        cut_used = false
    end

    -- solid ground contact re-arms the jump cut
    if vy >= 0.0 and box_hits_solid(px, py + 2.0, box_w, box_h) then
        on_ground = true
        cut_used = false
    end
    snap_to_slope()

    if py > GROUND_TOP + 140.0 then
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
    if test_right then
        move = 1.0
        running = true
    end
    if test_jump then
        jp = true
        jh = true
    end
    return move, running, jp, jh
end

local function draw_surface()
    local feet = py + box_h
    local x0 = math.floor(cam_x)
    local x1 = x0 + cam_vw
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
        if s.x > cam_x - 240.0 and s.x < cam_x + cam_vw + 240.0 then
            draw_text(s.label, s.x, cam_y + 62.0, 0.5, 1.0, 0.95, 0.6, 1.0)
        end
    end
end

local function draw_hud()
    local sx = cam_x
    local sy = cam_y
    draw_text("SLOPE LAB   A/D move   SPACE jump   SHIFT run   V surface   R reset",
              sx + 16.0, sy + 12.0, 0.45, 1.0, 1.0, 1.0, 1.0)
    local feet = py + box_h
    local col = math.floor((px + box_w * 0.5) / TILE)
    local r = math.floor(feet / TILE)
    local dir = map_slope_dir(map_id, col, r)
    if dir == 0 then dir = last_dir end
    local word = "flat"
    if dir == 1 then word = "up" elseif dir == 2 then word = "down" end
    draw_text("col " .. istr(col) .. "   feet " .. istr(math.floor(feet))
                  .. "   slope " .. word .. "   rise " .. istr(last_rise)
                  .. "   snaps " .. istr(snap_count),
              sx + 16.0, sy + 32.0, 0.45, 0.75, 0.95, 1.0, 1.0)
    if show_surface then
        draw_text("surface overlay on: red dots are map_ground_y samples",
                  sx + 16.0, sy + 50.0, 0.42, 1.0, 0.55, 0.55, 1.0)
    end
end

function update(dt)
    frame_n = frame_n + 1
    local sdt = dt
    if sdt > 0.034 then sdt = 0.034 end

    if not inited then
        inited = true
        map_id = map_load("assets/slope.json")
        tex_player = sprite_load("assets/s_player.png")
        test_right = os.getenv("SLOPE_TEST_RIGHT") ~= nil
        test_jump = os.getenv("SLOPE_TEST_JUMP") ~= nil
        show_surface = os.getenv("SLOPE_TEST_SURFACE") ~= nil
        reset_player()
        log_number(900001)
    end

    if input_key_pressed("v") then
        show_surface = not show_surface
        log_number(900100 + (show_surface and 1 or 0))
    end
    if input_key_pressed("r") then
        reset_player()
        log_number(900200)
    end

    local mx, run, jp, jh = read_input()
    step_player(sdt, mx, run, jp, jh)

    cam_vw = camera_viewport_w()
    cam_vh = camera_viewport_h()
    local want = px + box_w * 0.5 - cam_vw * 0.4
    if want > cam_x then cam_x = want end
    local max_cam = MAP_W - cam_vw
    if max_cam < 0.0 then max_cam = 0.0 end
    if cam_x > max_cam then cam_x = max_cam end
    if cam_x < 0.0 then cam_x = 0.0 end
    cam_y = 0.0
    -- The engine camera has to be told where we are looking. Without this it
    -- stays at the origin while the sky quad, the map and the HUD are all drawn
    -- in world coordinates, so the view never scrolls and an unpainted strip
    -- shows along the top and left edges.
    camera_set_position(cam_x, cam_y)
    -- Both ends are walls. Without the right one the player walks off the
    -- level, falls out of the world, gets teleported back to the start, and the
    -- whole view jumps with them.
    if px < cam_x then px = cam_x end
    if px > MAP_W - box_w then px = MAP_W - box_w end

    -- headless climb trace: feet height every 30 frames
    if imod(frame_n, 30.0) == 0.0 then
        log_number(900000 + math.floor(py + box_h))
    end
    draw_quad(cam_x, cam_y, cam_vw, cam_vh, 0.36, 0.58, 0.95, 1.0)
    map_draw(map_id)
    if show_surface then draw_surface() end
    sprite_draw(tex_player, px, py, box_w, box_h)
    draw_labels()
    draw_hud()
    return 0
end
