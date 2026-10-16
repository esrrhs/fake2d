-- fake2d pure-Lua animated-tile demo (--tile-demo).
--
-- Tiled tilesets can carry a per-tile `animation`: a list of frames with a
-- duration in milliseconds, looped forever from the moment the map loads.
-- This demo shows what the engine does with them:
--
--   * no bookkeeping - the engine advances every loaded map's clock once per
--     frame, so the pond ripples and the torches flicker with no script tick.
--   * per tile id    - Tiled animates a tile *id*, not a cell, so every water
--     cell is in lockstep (three ponds would still ripple together).
--   * read-only      - map_anim_time / _index / _count / _cycle / _gid report
--     what is playing; the HUD below is built from those.
--   * collision      - the grid never moves: map_solid keeps answering from
--     the base gid, so an animated tile cannot open a hole in the floor.
--
-- FakeLua notes: file-level locals must be initialised (bare literals would
-- be constants), and native calls are strict-arity.

local WATER = 1 + 0 -- gid 1: the animated water tile
local TORCH = 4 + 0 -- gid 4: the animated torch tile

local map_id = 0 + 0

local function istr(n)
    return string.format("%d", n)
end

-- Two decimals without %.2f: 0.542 -> "0.54".
local function two_dp(v)
    local hundredths = math.floor(v * 100.0 + 0.5)
    local whole = math.floor(hundredths / 100)
    local frac = hundredths - whole * 100
    if frac < 10 then
        return istr(whole) .. ".0" .. istr(frac)
    end
    return istr(whole) .. "." .. istr(frac)
end

local function yesno(v)
    if v then
        return "yes"
    end
    return "no"
end

function update(dt)
    if map_id == 0 then
        map_id = map_load("assets/ta_level.json")
        if map_id == 0 then
            draw_text("failed to load assets/ta_level.json", 40, 40, 1.0,
                      1.0, 0.4, 0.4, 1.0)
            return 0
        end
    end

    map_draw(map_id)

    -- The clock, straight from the engine's per-frame tick.
    local t = map_anim_time(map_id)
    local wi = map_anim_index(map_id, WATER)
    local ti = map_anim_index(map_id, TORCH)
    local wc = map_anim_count(map_id, WATER)
    local tc = map_anim_count(map_id, TORCH)
    local wcycle = map_anim_cycle(map_id, WATER)
    local tcycle = map_anim_cycle(map_id, TORCH)

    -- HUD panel. The map is 30x12 tiles of 32 px = 960x384, so the panel
    -- starts below it and stays inside the 960x540 canvas.
    draw_quad(0, 384, 960, 156, 0.05, 0.07, 0.12, 0.92)
    draw_text("TILE ANIMATION", 40, 396, 1.0, 0.95, 0.86, 0.52, 1.0)
    draw_text("Tiled tileset cycles, ticked by the engine once per frame",
              40, 418, 0.6, 0.62, 0.68, 0.76, 1.0)

    -- Water: a 3-frame cycle. The bar is the phase inside the cycle.
    draw_text("pond", 40, 448, 0.75, 0.47, 0.78, 0.96, 1.0)
    draw_text("frame " .. istr(wi + 1) .. "/" .. istr(wc) .. "  " ..
                  two_dp(wcycle) .. "s", 110, 448, 0.6, 0.72, 0.82, 0.92, 1.0)
    draw_quad(300, 442, 160, 10, 0.13, 0.16, 0.24, 1.0)
    if wcycle > 0.0 then
        local wphase = (t % wcycle) / wcycle
        draw_quad(300, 442, 160 * wphase, 10, 0.36, 0.72, 0.96, 1.0)
    end

    -- Torch: a 2-frame cycle of a different length, so the two bars drift
    -- apart — one clock, two cycles.
    draw_text("torch", 40, 470, 0.75, 1.0, 0.72, 0.36, 1.0)
    draw_text("frame " .. istr(ti + 1) .. "/" .. istr(tc) .. "  " ..
                  two_dp(tcycle) .. "s", 110, 470, 0.6, 0.72, 0.82, 0.92, 1.0)
    draw_quad(300, 464, 160, 10, 0.13, 0.16, 0.24, 1.0)
    if tcycle > 0.0 then
        local tphase = (t % tcycle) / tcycle
        draw_quad(300, 464, 160 * tphase, 10, 0.96, 0.64, 0.30, 1.0)
    end

    draw_text("clock " .. two_dp(t) .. "s", 480, 448, 0.6, 0.72, 0.82, 0.92, 1.0)
    draw_text("pond tile shows gid " .. istr(map_anim_gid(map_id, WATER)),
              480, 470, 0.6, 0.72, 0.82, 0.92, 1.0)

    -- Collisions are read from the base gid: the grass line stays solid while
    -- the water under it cycles, and the pond cell itself never turns solid.
    draw_text("solid grid: grass " .. yesno(map_solid(map_id, 2, 7)) ..
                  ", pond " .. yesno(map_solid(map_id, 5, 8)) ..
                  " - unchanged by the cycle", 40, 500, 0.6,
              0.55, 0.62, 0.7, 1.0)
    draw_text("every water cell shares one clock, so the whole pond moves",
              40, 522, 0.6, 0.55, 0.62, 0.7, 1.0)

    -- A click rewinds the clock: the pond jumps back to frame 1.
    if ui_button("ta_reset", 0, 800, 436, 120, 28, "RESET CLOCK", 0.55) then
        map_anim_reset(map_id)
    end

    return 0
end
