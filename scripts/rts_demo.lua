-- fake2d RTS demo (--rts-demo), Red Alert-style.
--
-- A complete top-down RTS skirmish: two mirrored bases, harvesters that
-- mine ore and return it to refineries, power plants that gate production,
-- infantry / rocket soldiers / tanks, construction of new structures, a
-- selection box, right-click orders, an enemy commander that builds and
-- attacks, and a battle map with lakes, rocks, ore fields and a clickable
-- minimap.
--
-- Controls: left click selects, drag-box selects units, right click moves /
-- attacks / harvests. 1-5 build structures, 6-9 build units. WASD/arrows
-- pan, wheel zooms, M minimap, Esc cancels placement. An attract commander
-- plays the player side whenever the controls are idle, so headless runs
-- fight a full autonomous battle.
--
-- All simulation runs on a fixed 1/60 s frame step so headless runs are
-- deterministic even though the engine's wall-clock dt is tiny.

local COLS = 64
local ROWS = 48
local TILE = 32
local VIEW_W = 960.0
local VIEW_H = 540.0

-- Terrain gids (match assets/rts/tiles.png).
local G_GRASS, G_FLOWER, G_DIRT, G_ROCK = 1, 2, 3, 4
local G_WATER, G_ORE, G_ORE_POOR, G_CONCRETE = 5, 6, 7, 8
local G_TREE, G_SAND, G_CRATER, G_RUBBLE = 9, 10, 11, 12

local P, E = 0, 1
local U_INF, U_ROCKET, U_TANK, U_HARV = 0, 1, 2, 3
local B_CY, B_POWER, B_BARRACKS, B_FACTORY, B_REFINERY, B_TURRET = 0, 1, 2, 3, 4, 5

local TAG_UNIT, TAG_BLD, TAG_PROJ = 1, 2, 3

-- Entity slots.
local S_X, S_Y, S_HP, S_OWNER = 0, 1, 2, 3
local S_TYPE, S_STATE, S_TIMER, S_CD = 4, 5, 6, 7
local S_TARGET, S_RANGE, S_DMG, S_AUX = 8, 9, 10, 11
local S_FLASH, S_GX, S_GY, S_SELECT = 12, 13, 14, 15

-- Unit states.
local ST_IDLE, ST_MOVE, ST_ATTACK = 0, 1, 2
local ST_HARV_MOVE, ST_HARV_WORK, ST_RETURN, ST_UNLOAD, ST_ATTACK_MOVE = 3, 4, 5, 6, 7

-- Building states.
local BST_BUILDING, BST_ACTIVE = 0, 1

local MAX_ARMY = 20
local MAX_ARMY_E = 6
local WAVE_INTERVAL = 360
local WAVE_INTERVAL_E = 900
local UNIT_CAP = 56
local POWER_GEN = 160

local map_id = 0 + 0
local inited = false

local tex_inf, tex_rocket, tex_tank, tex_harv = 0 + 0, 0 + 0, 0 + 0, 0 + 0
local tex_cy, tex_power, tex_barracks, tex_factory = 0 + 0, 0 + 0, 0 + 0, 0 + 0
local tex_refinery, tex_turret = 0 + 0, 0 + 0
local boom_fx = 0 + 0

local credits_p = 3500.0 + 0.0
local credits_e = 3500.0 + 0.0
local power_gen_p, power_use_p = 0.0 + 0.0, 0.0 + 0.0
local power_gen_e, power_use_e = 0.0 + 0.0, 0.0 + 0.0
local power_ratio_p = 1.0 + 0.0
local power_ratio_e = 1.0 + 0.0

local prod_p_type = -1 + 0
local prod_p_timer = 0.0 + 0.0
local prod_p_total = 1.0 + 0.0
local prod_e_type = -1 + 0
local prod_e_timer = 0.0 + 0.0
local prod_e_total = 1.0 + 0.0

local wave_p_timer = 0 + 0
local wave_e_timer = 0 + 0
local wave_p_count = 0 + 0
local wave_e_count = 0 + 0

local placing_type = -1 + 0
local drag_active = false
local drag_sx = 0.0 + 0.0
local drag_sy = 0.0 + 0.0
local drag_wx = 0.0 + 0.0
local drag_wy = 0.0 + 0.0
local minimap_on = true
local game_over = 0 + 0
local alert_code = 0 + 0
local alert_timer = 0 + 0

local first_contact = false
local first_blood = false
local first_structure = false
local first_damage = false
local attract = false
local idle_frames = 0 + 0

local frame_no = 0 + 0
local match_frames = 0 + 0
local cam_left = 0.0 + 0.0
local cam_top = 0.0 + 0.0
local inv_zoom = 1.0 + 0.0
local vw = VIEW_W + 0.0
local vh = VIEW_H + 0.0
local mouse_x = 0.0 + 0.0
local mouse_y = 0.0 + 0.0
local hover_c = -1.0 + 0.0
local hover_r = -1.0 + 0.0
local over_hud = false

local function clampf(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

local function idx(c, r)
    return r * COLS + c
end

local function tile_at(c, r)
    return map_tile(map_id, c, r)
end

local function terrain_blocked(g)
    return g == G_ROCK or g == G_WATER or g == G_TREE
end

local function fmt_int(v)
    return string.format("%d", math.floor(v + 0.5))
end

local function in_rect(mx, my, x, y, w, h)
    return mx >= x and mx <= x + w and my >= y and my <= y + h
end

local function hrect(x, y, w, h, r, g, b, a)
    draw_rect_outline(cam_left + x * inv_zoom, cam_top + y * inv_zoom,
                      w * inv_zoom, h * inv_zoom, r, g, b, a)
end

local function team_r(owner)
    if owner == P then return 0.55 end
    return 1.0
end

local function team_g(owner)
    if owner == P then return 0.78 end
    return 0.56
end

local function team_b(owner)
    if owner == P then return 1.0 end
    return 0.50
end

-- ---------------------------------------------------------------------------
-- stats tables (functions keep the persistent state rules simple)
-- ---------------------------------------------------------------------------

local function unit_hp(t)
    if t == U_INF then return 50.0 end
    if t == U_ROCKET then return 42.0 end
    if t == U_TANK then return 180.0 end
    return 200.0
end

local function unit_speed(t)
    if t == U_INF then return 1.75 end
    if t == U_ROCKET then return 1.55 end
    if t == U_TANK then return 1.35 end
    return 1.15
end

local function unit_range(t)
    if t == U_INF then return 150.0 end
    if t == U_ROCKET then return 300.0 end
    if t == U_TANK then return 220.0 end
    return 0.0
end

local function unit_dmg(t)
    if t == U_INF then return 8.0 end
    if t == U_ROCKET then return 30.0 end
    if t == U_TANK then return 36.0 end
    return 0.0
end

local function unit_cd(t)
    if t == U_INF then return 26.0 end
    if t == U_ROCKET then return 86.0 end
    if t == U_TANK then return 74.0 end
    return 999.0
end

local function unit_cost(t)
    if t == U_INF then return 120.0 end
    if t == U_ROCKET then return 180.0 end
    if t == U_TANK then return 350.0 end
    return 500.0
end

local function unit_build(t)
    if t == U_INF then return 100.0 end
    if t == U_ROCKET then return 160.0 end
    if t == U_TANK then return 260.0 end
    return 320.0
end

local function unit_name(t)
    if t == U_INF then return "INFANTRY" end
    if t == U_ROCKET then return "ROCKETEER" end
    if t == U_TANK then return "TANK" end
    return "HARVESTER"
end

local function unit_tex(t)
    if t == U_INF then return tex_inf end
    if t == U_ROCKET then return tex_rocket end
    if t == U_TANK then return tex_tank end
    return tex_harv
end

local function bld_hp(t)
    if t == B_CY then return 800.0 end
    if t == B_POWER then return 600.0 end
    if t == B_BARRACKS then return 700.0 end
    if t == B_FACTORY then return 800.0 end
    if t == B_REFINERY then return 750.0 end
    return 600.0
end

local function bld_cost(t)
    if t == B_POWER then return 350.0 end
    if t == B_BARRACKS then return 500.0 end
    if t == B_FACTORY then return 700.0 end
    if t == B_REFINERY then return 600.0 end
    if t == B_TURRET then return 400.0 end
    return 0.0
end

local function bld_build_frames(t)
    if t == B_POWER then return 200.0 end
    if t == B_BARRACKS then return 280.0 end
    if t == B_FACTORY then return 320.0 end
    if t == B_REFINERY then return 300.0 end
    return 220.0
end

local function bld_use(t)
    if t == B_POWER then return 0.0 end
    if t == B_CY then return 20.0 end
    if t == B_BARRACKS then return 20.0 end
    if t == B_FACTORY then return 30.0 end
    if t == B_REFINERY then return 30.0 end
    return 30.0
end

local function bld_name(t)
    if t == B_CY then return "COMMAND YARD" end
    if t == B_POWER then return "POWER PLANT" end
    if t == B_BARRACKS then return "BARRACKS" end
    if t == B_FACTORY then return "WAR FACTORY" end
    if t == B_REFINERY then return "REFINERY" end
    return "TURRET"
end

local function bld_tex(t)
    if t == B_CY then return tex_cy end
    if t == B_POWER then return tex_power end
    if t == B_BARRACKS then return tex_barracks end
    if t == B_FACTORY then return tex_factory end
    if t == B_REFINERY then return tex_refinery end
    return tex_turret
end

local function alert_text(code)
    if code == 1 then return "NOT ENOUGH CREDITS" end
    if code == 2 then return "REQUIRES BARRACKS / WAR FACTORY" end
    if code == 3 then return "CANNOT BUILD THERE" end
    if code == 4 then return "LOW POWER - PRODUCTION SLOWED" end
    if code == 5 then return "ENEMY ATTACK WAVE INCOMING" end
    if code == 6 then return "SELECT UNITS, THEN RIGHT-CLICK" end
    if code == 7 then return "UNIT READY" end
    if code == 8 then return "PLACE THE STRUCTURE" end
    return ""
    end

local function notify(code)
    alert_code = code
    alert_timer = 150
end

-- ---------------------------------------------------------------------------
-- entity helpers
-- ---------------------------------------------------------------------------

local function ent_cx(id)
    local e = to_int(id)
    local tag = ent_tag(e)
    if tag == TAG_BLD then return ent_num(e, S_X) + 32.0 end
    return ent_num(e, S_X)
end

local function ent_cy(id)
    local e = to_int(id)
    local tag = ent_tag(e)
    if tag == TAG_BLD then return ent_num(e, S_Y) + 32.0 end
    return ent_num(e, S_Y)
end

local function own_count(owner, combat_only)
    local n = ent_count(TAG_UNIT)
    local count = 0
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_UNIT)
        if ent_num(id, S_OWNER) == owner then
            if not combat_only or ent_num(id, S_TYPE) < U_HARV then
                count = count + 1
            end
        end
        i = i + 1
    end
    return count
end

local function has_active(owner, btype)
    local n = ent_count(TAG_BLD)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_BLD)
        if ent_num(id, S_OWNER) == owner and ent_num(id, S_TYPE) == btype and
           ent_num(id, S_STATE) == BST_ACTIVE then
            return true
        end
        i = i + 1
    end
    return false
end

local function find_active(owner, btype)
    local n = ent_count(TAG_BLD)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_BLD)
        if ent_num(id, S_OWNER) == owner and ent_num(id, S_TYPE) == btype and
           ent_num(id, S_STATE) == BST_ACTIVE then
            return id
        end
        i = i + 1
    end
    return 0
end

local function find_bld_type(owner, btype, x, y)
    local best = 0
    local bestd = 1.0e9
    local n = ent_count(TAG_BLD)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_BLD)
        if ent_num(id, S_OWNER) == owner and ent_num(id, S_TYPE) == btype and
           ent_num(id, S_STATE) == BST_ACTIVE then
            local dx = ent_num(id, S_X) + 32.0 - x
            local dy = ent_num(id, S_Y) + 32.0 - y
            local d = dx * dx + dy * dy
            if d < bestd then
                bestd = d
                best = id
            end
        end
        i = i + 1
    end
    return best
end

local function unit_at(wx, wy)
    local n = ent_count(TAG_UNIT)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_UNIT)
        local dx = ent_num(id, S_X) - wx
        local dy = ent_num(id, S_Y) - wy
        if dx * dx + dy * dy < 256.0 then
            return id
        end
        i = i + 1
    end
    return 0
end

local function building_at(wx, wy)
    local n = ent_count(TAG_BLD)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_BLD)
        local bx = ent_num(id, S_X)
        local by = ent_num(id, S_Y)
        if wx >= bx and wx <= bx + 64.0 and wy >= by and wy <= by + 64.0 then
            return id
        end
        i = i + 1
    end
    return 0
end

local function selected_count()
    local n = ent_count(TAG_UNIT)
    local count = 0
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_UNIT)
        if ent_num(id, S_SELECT) > 0.0 then count = count + 1 end
        i = i + 1
    end
    return count
end

local function clear_selection()
    local n = ent_count(TAG_UNIT)
    local i = 0
    while i < n do
        ent_set_num(ent_at(i, TAG_UNIT), S_SELECT, 0.0)
        i = i + 1
    end
    n = ent_count(TAG_BLD)
    i = 0
    while i < n do
        ent_set_num(ent_at(i, TAG_BLD), S_SELECT, 0.0)
        i = i + 1
    end
end

local function first_selected_unit()
    local n = ent_count(TAG_UNIT)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_UNIT)
        if ent_num(id, S_SELECT) > 0.0 then return id end
        i = i + 1
    end
    return 0
end

local function first_selected_bld()
    local n = ent_count(TAG_BLD)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_BLD)
        if ent_num(id, S_SELECT) > 0.0 then return id end
        i = i + 1
    end
    return 0
end

local function nearest_ore(x, y)
    local best = -1
    local bestd = 1.0e12
    local r = 0
    while r < ROWS do
        local c = 0
        while c < COLS do
            local g = tile_at(c, r)
            if g == G_ORE or g == G_ORE_POOR then
                local dx = c * TILE + 16.0 - x
                local dy = r * TILE + 16.0 - y
                local d = dx * dx + dy * dy
                if d < bestd then
                    bestd = d
                    best = idx(c, r)
                end
            end
            c = c + 1
        end
        r = r + 1
    end
    return best
end

local function make_occupancy()
    local occ = {}
    local n = ent_count(TAG_BLD)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_BLD)
        local c = ent_num(id, S_GX)
        local r = ent_num(id, S_GY)
        occ[idx(c, r)] = 1
        occ[idx(c + 1, r)] = 1
        occ[idx(c, r + 1)] = 1
        occ[idx(c + 1, r + 1)] = 1
        i = i + 1
    end
    return occ
end

local function cell_blocked(occ, c, r)
    if c < 0 or r < 0 or c >= COLS or r >= ROWS then return true end
    if terrain_blocked(tile_at(c, r)) then return true end
    return occ[idx(c, r)] ~= nil
end

local function pos_blocked(occ, x, y)
    -- Only the destination cell is tested. Testing a radius against tile
    -- corners traps units on the edge of a single tree or building; the
    -- slide/tangent steering in move_toward then handles the corners.
    return cell_blocked(occ, math.floor(x / TILE), math.floor(y / TILE))
end

local function move_toward(id, tx, ty, speed, occ)
    local x = ent_num(id, S_X)
    local y = ent_num(id, S_Y)
    local dx = tx - x
    local dy = ty - y
    local d = math.sqrt(dx * dx + dy * dy)
    if d < 7.0 then
        ent_set_num(id, S_X, tx)
        ent_set_num(id, S_Y, ty)
        return true
    end
    local step = speed
    if d < step then step = d end
    local nx = x + dx / d * step
    local ny = y + dy / d * step
    if not pos_blocked(occ, nx, ny) then
        ent_set_num(id, S_X, nx)
        ent_set_num(id, S_Y, ny)
    elseif not pos_blocked(occ, nx, y) then
        ent_set_num(id, S_X, nx)
    elseif not pos_blocked(occ, x, ny) then
        ent_set_num(id, S_Y, ny)
    else
        -- Wall-following: slide tangentially around the obstruction.
        local sx = x - dy / d * step
        local sy = y + dx / d * step
        if not pos_blocked(occ, sx, sy) then
            ent_set_num(id, S_X, sx)
            ent_set_num(id, S_Y, sy)
        else
            sx = x + dy / d * step
            sy = y - dx / d * step
            if not pos_blocked(occ, sx, sy) then
                ent_set_num(id, S_X, sx)
                ent_set_num(id, S_Y, sy)
            else
                return false
            end
        end
    end
    return false
end

-- ---------------------------------------------------------------------------
-- spawning / destruction
-- ---------------------------------------------------------------------------

local function boom(x, y, size)
    part_emit(boom_fx, math.floor(6 + size * 6), x, y)
end

local function spawn_unit(owner, utype, x, y)
    if ent_count(TAG_UNIT) >= UNIT_CAP then return 0 end
    local id = ent_create(TAG_UNIT)
    if id == 0 then return 0 end
    ent_set_num(id, S_X, x)
    ent_set_num(id, S_Y, y)
    ent_set_num(id, S_HP, unit_hp(utype))
    ent_set_num(id, S_OWNER, owner)
    ent_set_num(id, S_TYPE, utype)
    ent_set_num(id, S_STATE, ST_IDLE)
    ent_set_num(id, S_TIMER, 0.0)
    ent_set_num(id, S_CD, 0.0)
    ent_set_num(id, S_TARGET, 0.0)
    ent_set_num(id, S_RANGE, unit_range(utype))
    ent_set_num(id, S_DMG, unit_dmg(utype))
    ent_set_num(id, S_AUX, 0.0)
    ent_set_num(id, S_FLASH, 0.0)
    ent_set_num(id, S_GX, x)
    ent_set_num(id, S_GY, y)
    ent_set_num(id, S_SELECT, 0.0)
    return id
end

local function spawn_building(owner, btype, col, row, complete)
    local id = ent_create(TAG_BLD)
    if id == 0 then return 0 end
    local hp = bld_hp(btype)
    if not complete then hp = hp * 0.3 end
    ent_set_num(id, S_X, col * TILE)
    ent_set_num(id, S_Y, row * TILE)
    ent_set_num(id, S_HP, hp)
    ent_set_num(id, S_OWNER, owner)
    ent_set_num(id, S_TYPE, btype)
    if complete then
        ent_set_num(id, S_STATE, BST_ACTIVE)
    else
        ent_set_num(id, S_STATE, BST_BUILDING)
    end
    ent_set_num(id, S_TIMER, 0.0)
    ent_set_num(id, S_CD, 0.0)
    ent_set_num(id, S_TARGET, 0.0)
    ent_set_num(id, S_RANGE, 192.0)
    ent_set_num(id, S_DMG, 24.0)
    ent_set_num(id, S_AUX, 0.0)
    ent_set_num(id, S_FLASH, 0.0)
    ent_set_num(id, S_GX, col)
    ent_set_num(id, S_GY, row)
    ent_set_num(id, S_SELECT, 0.0)
    return id
end

local function finish_match(who)
    if game_over ~= 0 then return end
    game_over = who
    if who == 1 then
        log_info(string.format("rts: player victory - enemy construction yard destroyed (frame %d)", frame_no))
    else
        log_info(string.format("rts: defeat - construction yard destroyed (frame %d)", frame_no))
    end
end

local function destroy_building(id)
    local owner = ent_num(id, S_OWNER)
    local btype = ent_num(id, S_TYPE)
    local c = ent_num(id, S_GX)
    local r = ent_num(id, S_GY)
    local cx = ent_num(id, S_X) + 32.0
    local cy = ent_num(id, S_Y) + 32.0
    boom(cx, cy, 1.6)
    audio_play("rts_boom", 0.5)
    map_set_tile(map_id, c, r, G_CRATER)
    map_set_tile(map_id, c + 1, r, G_CRATER)
    map_set_tile(map_id, c, r + 1, G_CRATER)
    map_set_tile(map_id, c + 1, r + 1, G_CRATER)
    ent_destroy(id)
    if not first_structure then
        first_structure = true
        log_info("rts: structure destroyed")
    end
    if btype == B_CY then
        if owner == E then finish_match(1) else finish_match(2) end
    end
end

local function damage(id, amount)
    local e = to_int(id)
    if not ent_active(e) then return end
    local hp = ent_num(e, S_HP) - amount
    ent_set_num(e, S_FLASH, 6.0)
    if hp > 0.0 then
        ent_set_num(e, S_HP, hp)
        if ent_tag(e) == TAG_BLD and not first_damage then
            first_damage = true
            log_info("rts: structure damaged")
        end
        return
    end
    if ent_tag(e) == TAG_UNIT then
        boom(ent_num(e, S_X), ent_num(e, S_Y), 1.0)
        ent_destroy(e)
        if not first_blood then
            first_blood = true
            log_info("rts: first blood")
        end
    else
        destroy_building(e)
    end
end

local function spawn_projectile(owner, kind, x, y, target, dmg, speed)
    local id = ent_create(TAG_PROJ)
    if id == 0 then return end
    ent_set_num(id, S_X, x)
    ent_set_num(id, S_Y, y)
    ent_set_num(id, S_OWNER, owner)
    ent_set_num(id, S_TYPE, kind)
    ent_set_num(id, S_HP, 0.0)
    ent_set_num(id, S_STATE, 0.0)
    ent_set_num(id, S_TIMER, 160.0)
    ent_set_num(id, S_TARGET, target)
    ent_set_num(id, S_DMG, dmg)
    ent_set_num(id, S_AUX, speed)
    ent_set_num(id, S_FLASH, 0.0)
    if not first_contact then
        first_contact = true
        log_info("rts: first contact")
    end
end

local function unit_fire(id, target)
    local owner = ent_num(id, S_OWNER)
    local utype = ent_num(id, S_TYPE)
    local x = ent_num(id, S_X)
    local y = ent_num(id, S_Y)
    local dx = ent_cx(target) - x
    local dy = ent_cy(target) - y
    local d = math.sqrt(dx * dx + dy * dy)
    if d < 1.0 then d = 1.0 end
    local kind = 0
    local speed = 9.0
    if utype == U_ROCKET then kind = 1; speed = 7.0 end
    if utype == U_TANK then kind = 2; speed = 6.0 end
    local dmg = ent_num(id, S_DMG)
    if ent_tag(to_int(target)) == TAG_BLD then
        if owner == P then dmg = dmg * 3.0 else dmg = dmg * 1.4 end
    end
    local mx = x + dx / d * 8.0
    local my = y + dy / d * 8.0
    spawn_projectile(owner, kind, mx, my, target, dmg, speed)
    ent_set_num(id, S_CD, unit_cd(utype))
    if kind == 0 then
        audio_play("rts_rifle", 0.18)
    elseif kind == 1 then
        audio_play("rts_rocket", 0.25)
    else
        audio_play("rts_cannon", 0.3)
    end
end

local function turret_fire(id, target)
    local x = ent_num(id, S_X) + 32.0
    local y = ent_num(id, S_Y) + 32.0
    local dx = ent_cx(target) - x
    local dy = ent_cy(target) - y
    local d = math.sqrt(dx * dx + dy * dy)
    if d < 1.0 then d = 1.0 end
    spawn_projectile(ent_num(id, S_OWNER), 2, x + dx / d * 20.0,
                     y + dy / d * 20.0, target, ent_num(id, S_DMG), 7.0)
    ent_set_num(id, S_CD, 62.0)
    audio_play("rts_cannon", 0.28)
end

-- ---------------------------------------------------------------------------
-- targeting
-- ---------------------------------------------------------------------------

local function find_enemy(x, y, owner, range)
    local best = 0
    local bestd = range * range
    local n = ent_count(TAG_UNIT)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_UNIT)
        if ent_num(id, S_OWNER) ~= owner then
            local dx = ent_num(id, S_X) - x
            local dy = ent_num(id, S_Y) - y
            local d = dx * dx + dy * dy
            if d < bestd then bestd = d; best = id end
        end
        i = i + 1
    end
    n = ent_count(TAG_BLD)
    i = 0
    while i < n do
        local id = ent_at(i, TAG_BLD)
        if ent_num(id, S_OWNER) ~= owner then
            local dx = ent_num(id, S_X) + 32.0 - x
            local dy = ent_num(id, S_Y) + 32.0 - y
            local d = dx * dx + dy * dy
            if d < bestd then bestd = d; best = id end
        end
        i = i + 1
    end
    return best
end

local function enemy_target(owner)
    local mine = E
    if owner == E then mine = P end
    local cy = find_bld_type(mine, B_CY, 0.0, 0.0)
    if cy ~= 0 then return cy end
    local n = ent_count(TAG_BLD)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_BLD)
        if ent_num(id, S_OWNER) == mine then return id end
        i = i + 1
    end
    return 0
end

-- ---------------------------------------------------------------------------
-- orders
-- ---------------------------------------------------------------------------

local function order_move(id, tx, ty)
    ent_set_num(id, S_STATE, ST_MOVE)
    ent_set_num(id, S_TARGET, 0.0)
    ent_set_num(id, S_GX, tx)
    ent_set_num(id, S_GY, ty)
end

local function order_attack(id, target)
    ent_set_num(id, S_STATE, ST_ATTACK)
    ent_set_num(id, S_TARGET, target)
end

local function order_attack_move(id, target, tx, ty)
    ent_set_num(id, S_STATE, ST_ATTACK_MOVE)
    ent_set_num(id, S_TARGET, target)
    ent_set_num(id, S_GX, tx)
    ent_set_num(id, S_GY, ty)
end

local function order_harvest(id, col, row)
    ent_set_num(id, S_STATE, ST_HARV_MOVE)
    ent_set_num(id, S_TARGET, 0.0)
    ent_set_num(id, S_TIMER, 0.0)
    ent_set_num(id, S_GX, col * TILE + 16.0)
    ent_set_num(id, S_GY, row * TILE + 16.0)
end

local function order_return(id, refinery)
    ent_set_num(id, S_STATE, ST_RETURN)
    ent_set_num(id, S_TARGET, refinery)
end

-- ---------------------------------------------------------------------------
-- unit update
-- ---------------------------------------------------------------------------

local function update_combat_unit(id, occ)
    local owner = ent_num(id, S_OWNER)
    local state = ent_num(id, S_STATE)
    local target = to_int(ent_num(id, S_TARGET))
    local range = ent_num(id, S_RANGE)
    local x = ent_num(id, S_X)
    local y = ent_num(id, S_Y)

    if target ~= 0.0 and not ent_active(target) then
        target = 0.0
        ent_set_num(id, S_TARGET, 0.0)
    end

    if state == ST_ATTACK_MOVE then
        -- Attack-move pushes toward the destination and fires at anything
        -- within weapon range on the way without chasing it. On reaching
        -- the target building the unit switches to a focused attack.
        if target ~= 0.0 and not ent_active(target) then
            target = 0.0
            ent_set_num(id, S_TARGET, 0.0)
        end
        if (frame_no + math.floor(id % 24)) % 24 == 0 then
            local tid = find_enemy(x, y, owner, range)
            target = tid
            ent_set_num(id, S_TARGET, tid)
        end
        if target ~= 0.0 then
            local tx = ent_cx(target)
            local ty = ent_cy(target)
            local dx = tx - x
            local dy = ty - y
            local d = math.sqrt(dx * dx + dy * dy)
            if d <= range and ent_num(id, S_CD) <= 0.0 then unit_fire(id, target) end
        end
        local gx = ent_num(id, S_GX)
        local gy = ent_num(id, S_GY)
        move_toward(id, gx, gy, unit_speed(ent_num(id, S_TYPE)), occ)
        local ddx = gx - ent_num(id, S_X)
        local ddy = gy - ent_num(id, S_Y)
        local dd = math.sqrt(ddx * ddx + ddy * ddy)
        if dd < 460.0 then
            local b = building_at(gx, gy)
            if b ~= 0 then
                ent_set_num(id, S_STATE, ST_ATTACK)
                ent_set_num(id, S_TARGET, b)
            elseif dd < 10.0 then
                ent_set_num(id, S_STATE, ST_IDLE)
            end
        end
    elseif state == ST_ATTACK then
        if target == 0 and (frame_no + math.floor(id % 24)) % 24 == 0 then
            target = find_enemy(x, y, owner, range + 40.0)
            if target ~= 0 then ent_set_num(id, S_TARGET, target) end
        end
        if target ~= 0.0 then
            local tx = ent_cx(target)
            local ty = ent_cy(target)
            local dx = tx - x
            local dy = ty - y
            local d = math.sqrt(dx * dx + dy * dy)
            if d <= range then
                if ent_num(id, S_CD) <= 0.0 then unit_fire(id, target) end
            else
                move_toward(id, tx, ty, unit_speed(ent_num(id, S_TYPE)), occ)
            end
        else
            ent_set_num(id, S_STATE, ST_IDLE)
        end
    elseif state == ST_MOVE then
        if move_toward(id, ent_num(id, S_GX), ent_num(id, S_GY),
                       unit_speed(ent_num(id, S_TYPE)), occ) then
            ent_set_num(id, S_STATE, ST_IDLE)
        end
    else
        -- Idle guard: acquire a nearby enemy every half second.
        if (frame_no + math.floor(id % 30)) % 30 == 0 then
            local tid = find_enemy(x, y, owner, range + 30.0)
            if tid ~= 0 then
                ent_set_num(id, S_STATE, ST_ATTACK)
                ent_set_num(id, S_TARGET, tid)
            end
        end
    end
end

local function update_harvester(id, occ)
    local owner = ent_num(id, S_OWNER)
    local state = ent_num(id, S_STATE)
    local cargo = ent_num(id, S_AUX)
    local x = ent_num(id, S_X)
    local y = ent_num(id, S_Y)

    if state == ST_IDLE then
        if cargo >= 300.0 then
            local ref = find_bld_type(owner, B_REFINERY, x, y)
            if ref ~= 0 then order_return(id, ref) else ent_set_num(id, S_TIMER, 0.0) end
        else
            local timer = ent_num(id, S_TIMER)
            if timer > 0.0 then
                ent_set_num(id, S_TIMER, timer - 1.0)
            else
                local ore = nearest_ore(x, y)
                if ore >= 0 then
                    order_harvest(id, math.floor(ore % COLS), math.floor(ore / COLS))
                end
                ent_set_num(id, S_TIMER, 90.0)
            end
        end
    elseif state == ST_HARV_MOVE then
        if move_toward(id, ent_num(id, S_GX), ent_num(id, S_GY), unit_speed(U_HARV), occ) then
            ent_set_num(id, S_STATE, ST_HARV_WORK)
            ent_set_num(id, S_TIMER, 0.0)
        end
    elseif state == ST_HARV_WORK then
        ent_set_num(id, S_TIMER, ent_num(id, S_TIMER) + 1.0)
        if ent_num(id, S_TIMER) >= 70.0 then
            local c = math.floor(ent_num(id, S_GX) / TILE)
            local r = math.floor(ent_num(id, S_GY) / TILE)
            local g = tile_at(c, r)
            if g == G_ORE then
                map_set_tile(map_id, c, r, G_ORE_POOR)
            elseif g == G_ORE_POOR then
                map_set_tile(map_id, c, r, G_DIRT)
            end
            cargo = cargo + 100.0
            ent_set_num(id, S_AUX, cargo)
            if cargo >= 300.0 then
                local ref = find_bld_type(owner, B_REFINERY, x, y)
                if ref ~= 0 then
                    order_return(id, ref)
                else
                    ent_set_num(id, S_STATE, ST_IDLE)
                end
            else
                local ore = nearest_ore(x, y)
                if ore >= 0 then
                    ent_set_num(id, S_STATE, ST_HARV_MOVE)
                    ent_set_num(id, S_GX, math.floor(ore % COLS) * TILE + 16.0)
                    ent_set_num(id, S_GY, math.floor(ore / COLS) * TILE + 16.0)
                    ent_set_num(id, S_TIMER, 0.0)
                else
                    ent_set_num(id, S_STATE, ST_IDLE)
                    ent_set_num(id, S_TIMER, 0.0)
                end
            end
        end
    elseif state == ST_RETURN then
        local ref = to_int(ent_num(id, S_TARGET))
        if ref == 0.0 or not ent_active(ref) then
            ent_set_num(id, S_STATE, ST_IDLE)
            ent_set_num(id, S_TIMER, 0.0)
        else
            local rx = ent_num(ref, S_X) + 32.0
            local ry = ent_num(ref, S_Y) + 32.0
            if move_toward(id, rx, ry + 34.0, unit_speed(U_HARV), occ) then
                ent_set_num(id, S_STATE, ST_UNLOAD)
                ent_set_num(id, S_TIMER, 0.0)
            end
        end
    elseif state == ST_UNLOAD then
        ent_set_num(id, S_TIMER, ent_num(id, S_TIMER) + 1.0)
        if ent_num(id, S_TIMER) >= 90.0 then
            if owner == P then
                credits_p = credits_p + cargo
                audio_play("rts_cash", 0.3)
            else
                credits_e = credits_e + cargo
            end
            ent_set_num(id, S_AUX, 0.0)
            ent_set_num(id, S_STATE, ST_IDLE)
            ent_set_num(id, S_TIMER, 0.0)
        end
    end
end

local function update_units(occ)
    local n = ent_count(TAG_UNIT)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_UNIT)
        local flash = ent_num(id, S_FLASH)
        if flash > 0.0 then ent_set_num(id, S_FLASH, flash - 1.0) end
        local cd = ent_num(id, S_CD)
        if cd > 0.0 then ent_set_num(id, S_CD, cd - 1.0) end
        if ent_num(id, S_TYPE) == U_HARV then
            update_harvester(id, occ)
        else
            update_combat_unit(id, occ)
        end
        i = i + 1
    end
end

-- ---------------------------------------------------------------------------
-- buildings / production
-- ---------------------------------------------------------------------------

local function update_buildings()
    local n = ent_count(TAG_BLD)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_BLD)
        local owner = ent_num(id, S_OWNER)
        local btype = ent_num(id, S_TYPE)
        local cd = ent_num(id, S_CD)
        local flash = ent_num(id, S_FLASH)
        if flash > 0.0 then ent_set_num(id, S_FLASH, flash - 1.0) end
        if cd > 0.0 then ent_set_num(id, S_CD, cd - 1.0) end
        if ent_num(id, S_STATE) == BST_BUILDING then
            local ratio = power_ratio_p
            if owner == E then ratio = power_ratio_e end
            if ratio < 1.0 then ratio = 0.65 end
            ent_set_num(id, S_TIMER, ent_num(id, S_TIMER) + ratio)
            local need = bld_build_frames(btype)
            if ent_num(id, S_TIMER) >= need then
                ent_set_num(id, S_STATE, BST_ACTIVE)
                ent_set_num(id, S_HP, bld_hp(btype))
                if owner == P then
                    audio_play("rts_done", 0.3)
                    notify(7)
                end
            end
        elseif btype == B_TURRET then
            local target = to_int(ent_num(id, S_TARGET))
            if target ~= 0.0 and not ent_active(target) then
                target = 0.0
                ent_set_num(id, S_TARGET, 0.0)
            end
            if target == 0.0 and (frame_no + math.floor(id % 30)) % 30 == 0 then
                target = find_enemy(ent_num(id, S_X) + 32.0,
                                    ent_num(id, S_Y) + 32.0,
                                    owner, ent_num(id, S_RANGE))
                if target ~= 0 then ent_set_num(id, S_TARGET, target) end
            end
            if target ~= 0.0 then
                local dx = ent_cx(target) - (ent_num(id, S_X) + 32.0)
                local dy = ent_cy(target) - (ent_num(id, S_Y) + 32.0)
                local d = math.sqrt(dx * dx + dy * dy)
                if d > ent_num(id, S_RANGE) * 1.35 then
                    ent_set_num(id, S_TARGET, 0.0)
                elseif d <= ent_num(id, S_RANGE) and ent_num(id, S_CD) <= 0.0 then
                    turret_fire(id, target)
                end
            end
        end
        i = i + 1
    end
end

local function update_power()
    power_gen_p = 0.0
    power_use_p = 0.0
    power_gen_e = 0.0
    power_use_e = 0.0
    local n = ent_count(TAG_BLD)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_BLD)
        if ent_num(id, S_STATE) == BST_ACTIVE then
            local owner = ent_num(id, S_OWNER)
            local btype = ent_num(id, S_TYPE)
            if btype == B_POWER then
                if owner == P then power_gen_p = power_gen_p + POWER_GEN
                else power_gen_e = power_gen_e + POWER_GEN end
            else
                if owner == P then power_use_p = power_use_p + bld_use(btype)
                else power_use_e = power_use_e + bld_use(btype) end
            end
        end
        i = i + 1
    end
    if power_use_p <= 0.0 then
        power_ratio_p = 1.0
    else
        power_ratio_p = power_gen_p / power_use_p
        if power_ratio_p > 1.0 then power_ratio_p = 1.0 end
    end
    if power_use_e <= 0.0 then
        power_ratio_e = 1.0
    else
        power_ratio_e = power_gen_e / power_use_e
        if power_ratio_e > 1.0 then power_ratio_e = 1.0 end
    end
end

local function production_start(owner, utype)
    if owner == P then
        if prod_p_type >= 0 then notify(1); return false end
        if credits_p < unit_cost(utype) then notify(1); return false end
        if utype == U_INF or utype == U_ROCKET then
            if not has_active(P, B_BARRACKS) then notify(2); return false end
        else
            if not has_active(P, B_FACTORY) then notify(2); return false end
            if utype == U_HARV and not has_active(P, B_REFINERY) then notify(2); return false end
        end
        credits_p = credits_p - unit_cost(utype)
        prod_p_type = utype
        prod_p_timer = 0.0
        prod_p_total = unit_build(utype)
        audio_play("rts_build", 0.25)
        return true
    end
    if prod_e_type >= 0 then return false end
    if credits_e < unit_cost(utype) then return false end
    if utype == U_INF or utype == U_ROCKET then
        if not has_active(E, B_BARRACKS) then return false end
    else
        if not has_active(E, B_FACTORY) then return false end
        if utype == U_HARV and not has_active(E, B_REFINERY) then return false end
    end
    credits_e = credits_e - unit_cost(utype)
    prod_e_type = utype
    prod_e_timer = 0.0
    prod_e_total = unit_build(utype)
    return true
end

local function spawn_production_unit(owner, utype)
    local need = B_BARRACKS
    if utype == U_TANK or utype == U_HARV then need = B_FACTORY end
    local bid = find_active(owner, need)
    if bid == 0 then return end
    local bx = ent_num(bid, S_X) + 32.0
    local by = ent_num(bid, S_Y) + 96.0
    if by > (ROWS - 1) * TILE then by = ent_num(bid, S_Y) - 40.0 end
    local id = spawn_unit(owner, utype, bx, by)
    if id == 0 then return end
    if utype == U_HARV then return end
    -- New combat units gather at a rally point in front of the base.
    local rx = 20.0 * TILE
    local ry = 41.0 * TILE
    if owner == E then rx = 48.0 * TILE; ry = 19.0 * TILE end
    order_move(id, rx, ry)
end

local function update_production()
    if prod_p_type >= 0 then
        local ratio = power_ratio_p
        if ratio < 1.0 then ratio = 0.65 end
        prod_p_timer = prod_p_timer + ratio * 1.2
        if prod_p_timer >= prod_p_total then
            spawn_production_unit(P, prod_p_type)
            prod_p_type = -1
            audio_play("rts_ready", 0.35)
            notify(7)
        end
    end
    if prod_e_type >= 0 then
        local ratio = power_ratio_e
        if ratio < 1.0 then ratio = 0.65 end
        prod_e_timer = prod_e_timer + ratio
        if prod_e_timer >= prod_e_total then
            spawn_production_unit(E, prod_e_type)
            prod_e_type = -1
        end
    end
end

-- ---------------------------------------------------------------------------
-- projectiles
-- ---------------------------------------------------------------------------

local function update_projectiles()
    local i = ent_count(TAG_PROJ) - 1
    while i >= 0 do
        local id = ent_at(i, TAG_PROJ)
        if id ~= 0 then
            local target = to_int(ent_num(id, S_TARGET))
            local life = ent_num(id, S_TIMER) - 1.0
            if target == 0.0 or not ent_active(target) or life <= 0.0 then
                if life <= 0.0 and ent_active(target) then
                    damage(target, ent_num(id, S_DMG))
                end
                ent_destroy(id)
            else
                local x = ent_num(id, S_X)
                local y = ent_num(id, S_Y)
                local tx = ent_cx(target)
                local ty = ent_cy(target)
                local dx = tx - x
                local dy = ty - y
                local d = math.sqrt(dx * dx + dy * dy)
                local hit = 13.0
                if ent_tag(target) == TAG_BLD then hit = 34.0 end
                if d <= hit then
                    damage(target, ent_num(id, S_DMG))
                    boom(tx, ty, 0.55)
                    ent_destroy(id)
                else
                    local speed = ent_num(id, S_AUX)
                    ent_set_num(id, S_X, x + dx / d * speed)
                    ent_set_num(id, S_Y, y + dy / d * speed)
                    ent_set_num(id, S_TIMER, life)
                end
            end
        end
        i = i - 1
    end
end

-- ---------------------------------------------------------------------------
-- commander AI
-- ---------------------------------------------------------------------------

local function can_build_at(col, row, occ)
    local c = col
    while c <= col + 1 do
        local r = row
        while r <= row + 1 do
            if c < 0 or r < 0 or c >= COLS or r >= ROWS then return false end
            local g = tile_at(c, r)
            if terrain_blocked(g) or g == G_ORE or g == G_ORE_POOR then return false end
            if occ[idx(c, r)] ~= nil then return false end
            r = r + 1
        end
        c = c + 1
    end
    return true
end

local function ai_place(owner, btype, occ)
    local cost = bld_cost(btype)
    if owner == P and credits_p < cost then return false end
    if owner == E and credits_e < cost then return false end
    local k = 0
    local col = 0
    local row = 0
    while k < 35 do
        if owner == P then
            col = math.floor(rng_float() * 14.0) + 4
            row = math.floor(rng_float() * 6.0) + 30
        else
            col = math.floor(rng_float() * 14.0) + 44
            row = math.floor(rng_float() * 6.0) + 5
        end
        if can_build_at(col, row, occ) then
            if owner == P then credits_p = credits_p - cost
            else credits_e = credits_e - cost end
            spawn_building(owner, btype, col, row, false)
            if owner == P then audio_play("rts_build", 0.3) end
            return true
        end
        k = k + 1
    end
    return false
end

local function ai_owner(owner)
    if game_over ~= 0 then return end
    local occ = make_occupancy()
    local counts = {}
    local i = 0
    while i < 6 do counts[i] = 0; i = i + 1 end
    local n = ent_count(TAG_BLD)
    i = 0
    while i < n do
        local id = ent_at(i, TAG_BLD)
        if ent_num(id, S_OWNER) == owner then
            local t = math.floor(ent_num(id, S_TYPE))
            counts[t] = counts[t] + 1
        end
        i = i + 1
    end

    local ratio = power_ratio_p
    if owner == E then ratio = power_ratio_e end
    if counts[B_POWER] == 0 then
        ai_place(owner, B_POWER, occ)
    elseif counts[B_REFINERY] == 0 then
        ai_place(owner, B_REFINERY, occ)
    elseif counts[B_BARRACKS] == 0 then
        ai_place(owner, B_BARRACKS, occ)
    elseif counts[B_FACTORY] == 0 then
        ai_place(owner, B_FACTORY, occ)
    elseif ratio < 1.0 and counts[B_POWER] < 3 then
        ai_place(owner, B_POWER, occ)
    elseif frame_no > 600 then
        if owner == P and counts[B_TURRET] < 3 then
            ai_place(owner, B_TURRET, occ)
        elseif owner == E and counts[B_TURRET] < 1 then
            ai_place(owner, B_TURRET, occ)
        end
    end

    if owner == P then
        if prod_p_type < 0 and own_count(P, false) < MAX_ARMY then
            local pick = U_INF
            if own_count(P, true) < 4 then
                pick = U_TANK
            elseif rng_float() < 0.25 then
                pick = U_HARV
                if own_count(P, false) >= 8 then pick = U_ROCKET end
            elseif rng_float() < 0.4 then
                pick = U_ROCKET
            end
            production_start(P, pick)
        end
    else
        if prod_e_type < 0 and own_count(E, false) < MAX_ARMY_E then
            local pick = U_INF
            if own_count(E, true) < 4 then
                pick = U_TANK
            elseif rng_float() < 0.25 then
                pick = U_HARV
                if own_count(E, false) >= 8 then pick = U_ROCKET end
            elseif rng_float() < 0.4 then
                pick = U_ROCKET
            end
            production_start(E, pick)
        end
    end
end

local function order_army(owner)
    local target = enemy_target(owner)
    if target == 0.0 then return false end
    local tx = ent_cx(target)
    local ty = ent_cy(target)
    local n = ent_count(TAG_UNIT)

    -- The player commander keeps two units home as a guard force; the
    -- enemy commander sends everything.
    local g1 = 0
    local g2 = 0
    if owner == P then
        local cy = find_bld_type(P, B_CY, 0.0, 0.0)
        if cy ~= 0 then
            local cx = ent_num(cy, S_X) + 32.0
            local cyy = ent_num(cy, S_Y) + 32.0
            local d1 = 1.0e9
            local d2 = 1.0e9
            local i = 0
            while i < n do
                local id = ent_at(i, TAG_UNIT)
                if ent_num(id, S_OWNER) == P and ent_num(id, S_TYPE) < U_HARV then
                    local dx = ent_num(id, S_X) - cx
                    local dy = ent_num(id, S_Y) - cyy
                    local d = dx * dx + dy * dy
                    if d < d1 then
                        d2 = d1
                        g2 = g1
                        d1 = d
                        g1 = id
                    elseif d < d2 then
                        d2 = d
                        g2 = id
                    end
                end
                i = i + 1
            end
        end
    end

    local i = 0
    local sent = 0
    while i < n do
        local id = ent_at(i, TAG_UNIT)
        if ent_num(id, S_OWNER) == owner and ent_num(id, S_TYPE) < U_HARV and
           id ~= g1 and id ~= g2 then
            order_attack_move(id, 0.0, tx, ty)
            sent = sent + 1
        end
        i = i + 1
    end
    return sent > 0
end

local function update_ai()
    if game_over ~= 0 then return end
    wave_p_timer = wave_p_timer + 1
    wave_e_timer = wave_e_timer + 1
    if attract and wave_p_timer >= WAVE_INTERVAL and own_count(P, true) >= 4 then
        if order_army(P) then
            wave_p_count = wave_p_count + 1
            wave_p_timer = 0
            log_info(string.format("rts: player attack wave %d", wave_p_count))
        end
    end
    if wave_e_timer >= WAVE_INTERVAL_E and own_count(E, true) >= 4 then
        if order_army(E) then
            wave_e_count = wave_e_count + 1
            wave_e_timer = 0
            log_info(string.format("rts: enemy attack wave %d", wave_e_count))
            if wave_e_count <= 3 then notify(5) end
        end
    end
    if frame_no % 30 == 0 then
        if attract then ai_owner(P) end
        ai_owner(E)
    end
end

-- ---------------------------------------------------------------------------
-- input helpers
-- ---------------------------------------------------------------------------

local function screen_selected_units(wx0, wy0, wx1, wy1)
    local n = ent_count(TAG_UNIT)
    local i = 0
    local count = 0
    while i < n do
        local id = ent_at(i, TAG_UNIT)
        if ent_num(id, S_OWNER) == P then
            local x = ent_num(id, S_X)
            local y = ent_num(id, S_Y)
            if x >= wx0 and x <= wx1 and y >= wy0 and y <= wy1 then
                ent_set_num(id, S_SELECT, 1.0)
                count = count + 1
            end
        end
        i = i + 1
    end
    return count
end

local function try_place_building(col, row)
    local occ = make_occupancy()
    if not can_build_at(col, row, occ) then
        notify(3)
        audio_play("rts_alarm", 0.2)
        return false
    end
    local cost = bld_cost(placing_type)
    if credits_p < cost then
        notify(1)
        audio_play("rts_alarm", 0.2)
        return false
    end
    credits_p = credits_p - cost
    spawn_building(P, placing_type, col, row, false)
    audio_play("rts_build", 0.4)
    notify(8)
    return true
end

local function issue_order(wx, wy)
    local nsel = selected_count()
    if nsel < 1 then
        local bsel = first_selected_bld()
        if bsel == 0 then
            notify(6)
            return
        end
    end
    local hit_unit = unit_at(wx, wy)
    local hit_bld = building_at(wx, wy)
    local target = 0
    if hit_unit ~= 0 and ent_num(hit_unit, S_OWNER) ~= P then target = hit_unit end
    if target == 0 and hit_bld ~= 0 and ent_num(hit_bld, S_OWNER) ~= P then target = hit_bld end
    local n = ent_count(TAG_UNIT)
    local i = 0
    local slot = 0
    if target ~= 0 then
        while i < n do
            local id = ent_at(i, TAG_UNIT)
            if ent_num(id, S_SELECT) > 0.0 and ent_num(id, S_TYPE) < U_HARV then
                order_attack(id, target)
            end
            i = i + 1
        end
        audio_play("rts_ready", 0.22)
        return
    end

    local c = math.floor(wx / TILE)
    local r = math.floor(wy / TILE)
    local g = tile_at(c, r)
    if hit_bld ~= 0 and ent_num(hit_bld, S_OWNER) == P and
       ent_num(hit_bld, S_TYPE) == B_REFINERY then
        local rx = ent_num(hit_bld, S_X) + 32.0
        local ry = ent_num(hit_bld, S_Y) + 32.0
        i = 0
        while i < n do
            local id = ent_at(i, TAG_UNIT)
            if ent_num(id, S_SELECT) > 0.0 then
                if ent_num(id, S_TYPE) == U_HARV then
                    order_return(id, hit_bld)
                else
                    order_move(id, rx, ry + 70.0)
                end
            end
            i = i + 1
        end
        return
    end
    if g == G_ORE or g == G_ORE_POOR then
        i = 0
        while i < n do
            local id = ent_at(i, TAG_UNIT)
            if ent_num(id, S_SELECT) > 0.0 then
                if ent_num(id, S_TYPE) == U_HARV then
                    order_harvest(id, c, r)
                else
                    order_move(id, c * TILE + 16.0, r * TILE + 16.0)
                end
            end
            i = i + 1
        end
        return
    end

    i = 0
    while i < n do
        local id = ent_at(i, TAG_UNIT)
        if ent_num(id, S_SELECT) > 0.0 then
            local dx = ((slot % 3) - 1) * 30.0
            local dy = (math.floor(slot / 3) % 3 - 1) * 30.0
            order_move(id, wx + dx, wy + dy)
            slot = slot + 1
        end
        i = i + 1
    end
    audio_play("rts_ready", 0.2)
end

local function finish_selection(wx, wy)
    local x0 = drag_wx
    local y0 = drag_wy
    local x1 = wx
    local y1 = wy
    if x0 > x1 then local t = x0; x0 = x1; x1 = t end
    if y0 > y1 then local t = y0; y0 = y1; y1 = t end
    clear_selection()
    if x1 - x0 > 10.0 or y1 - y0 > 10.0 then
        screen_selected_units(x0, y0, x1, y1)
    else
        local uid = unit_at(wx, wy)
        if uid ~= 0 and ent_num(uid, S_OWNER) == P then
            ent_set_num(uid, S_SELECT, 1.0)
        else
            local bid = building_at(wx, wy)
            if bid ~= 0 and ent_num(bid, S_OWNER) == P then
                ent_set_num(bid, S_SELECT, 1.0)
            end
        end
    end
end

local function handle_command_press()
    local pressed = input_mouse_pressed(0)
    if not pressed then return false end
    local by = vh - 132.0
    local i = 0
    while i < 5 do
        local x = 8.0 + i * 94.0
        if in_rect(mouse_x, mouse_y, x, by + 8.0, 86.0, 54.0) then
            local t = i + 1
            if placing_type == t then
                placing_type = -1
            else
                placing_type = t
                notify(8)
            end
            return true
        end
        i = i + 1
    end
    i = 0
    while i < 4 do
        local x = 8.0 + i * 94.0
        if in_rect(mouse_x, mouse_y, x, by + 70.0, 86.0, 54.0) then
            production_start(P, i)
            return true
        end
        i = i + 1
    end
    return false
end

local function update_keys()
    local activity = false
    if input_key_down("space") then activity = true end
    if math.abs(input_mouse_dx()) + math.abs(input_mouse_dy()) > 1.0 then activity = true end
    if input_mouse_down(0) or input_mouse_down(2) then activity = true end
    if math.abs(input_mouse_wheel()) > 0.0 then activity = true end
    local k = 1
    while k <= 9 do
        local key = string.format("%d", k)
        if input_key_pressed(key) then activity = true end
        k = k + 1
    end
    if input_key_pressed("m") or input_key_pressed("escape") then activity = true end
    if activity then
        idle_frames = 0
        if attract then attract = false end
    else
        idle_frames = idle_frames + 1
        if idle_frames > 120 then attract = true end
    end

    if input_key_pressed("m") then minimap_on = not minimap_on end
    if input_key_pressed("escape") then
        placing_type = -1
        clear_selection()
        drag_active = false
    end
    local shortcut = 0
    if input_key_pressed("1") then shortcut = 1 end
    if input_key_pressed("2") then shortcut = 2 end
    if input_key_pressed("3") then shortcut = 3 end
    if input_key_pressed("4") then shortcut = 4 end
    if input_key_pressed("5") then shortcut = 5 end
    if shortcut > 0 then placing_type = shortcut; notify(8) end
    if input_key_pressed("6") then production_start(P, U_INF) end
    if input_key_pressed("7") then production_start(P, U_ROCKET) end
    if input_key_pressed("8") then production_start(P, U_TANK) end
    if input_key_pressed("9") then production_start(P, U_HARV) end
end

local function update_camera()
    local zoom = camera_zoom()
    if input_key_down("a") or input_key_down("left") then camera_move(-11.0 / zoom, 0.0) end
    if input_key_down("d") or input_key_down("right") then camera_move(11.0 / zoom, 0.0) end
    if input_key_down("w") or input_key_down("up") then camera_move(0.0, -11.0 / zoom) end
    if input_key_down("s") or input_key_down("down") then camera_move(0.0, 11.0 / zoom) end
    local wheel = input_mouse_wheel()
    if wheel ~= 0.0 then
        zoom = clampf(zoom - wheel * 0.10, 0.7, 1.4)
        camera_set_zoom(zoom)
        camera_set_position(camera_x(), camera_y())
    end
    zoom = camera_zoom()
    cam_left = camera_screen_to_world_x(0.0, 0.0)
    cam_top = camera_screen_to_world_y(0.0, 0.0)
    inv_zoom = 1.0 / zoom
end

local function auto_camera()
    if not attract then return end
    if not first_contact then return end
    if frame_no % 24 ~= 0 then return end
    local n = ent_count(TAG_UNIT)
    local i = 0

    -- Follow the assault unit closest to the enemy command yard, not the
    -- average of every unit (which stays pinned to the base by new recruits).
    local enemy_cy = find_bld_type(E, B_CY, 0.0, 0.0)
    local focus_x = -1.0
    local focus_y = -1.0
    if enemy_cy ~= 0 then
        local cx = ent_num(enemy_cy, S_X) + 32.0
        local cy = ent_num(enemy_cy, S_Y) + 32.0
        local bestd = 1.0e12
        while i < n do
            local id = ent_at(i, TAG_UNIT)
            local state = ent_num(id, S_STATE)
            if ent_num(id, S_OWNER) == P and ent_num(id, S_TYPE) < U_HARV and
               (state == ST_ATTACK or state == ST_ATTACK_MOVE) then
                local dx = ent_num(id, S_X) - cx
                local dy = ent_num(id, S_Y) - cy
                local d = dx * dx + dy * dy
                if d < bestd then
                    bestd = d
                    focus_x = ent_num(id, S_X)
                    focus_y = ent_num(id, S_Y)
                end
            end
            i = i + 1
        end
    end

    if focus_x < 0.0 then
        local sx = 0.0
        local sy = 0.0
        local count = 0
        i = 0
        while i < n do
            local id = ent_at(i, TAG_UNIT)
            if ent_num(id, S_OWNER) == P and ent_num(id, S_TYPE) < U_HARV then
                sx = sx + ent_num(id, S_X)
                sy = sy + ent_num(id, S_Y)
                count = count + 1
            end
            i = i + 1
        end
        if count == 0 then return end
        focus_x = sx / count
        focus_y = sy / count
    end

    local want_x = focus_x - vw * inv_zoom * 0.5
    local want_y = focus_y - vh * inv_zoom * 0.5
    camera_set_position(camera_x() + (want_x - camera_x()) * 0.5,
                        camera_y() + (want_y - camera_y()) * 0.5)
end

local function update_pointer()
    local wx = camera_screen_to_world_x(mouse_x, mouse_y)
    local wy = camera_screen_to_world_y(mouse_x, mouse_y)
    hover_c = math.floor(wx / TILE)
    hover_r = math.floor(wy / TILE)

    over_hud = false
    if in_rect(mouse_x, mouse_y, 0, 0, vw, 42.0) then over_hud = true end
    if in_rect(mouse_x, mouse_y, 0, vh - 132.0, vw, 132.0) then over_hud = true end
    if minimap_on and in_rect(mouse_x, mouse_y, vw - 196.0, vh - 286.0, 190.0, 150.0) then over_hud = true end
    if (selected_count() > 0 or first_selected_bld() ~= 0) and
       in_rect(mouse_x, mouse_y, 8.0, vh - 216.0, 344.0, 80.0) then
        over_hud = true
    end

    if game_over == 0 and input_mouse_pressed(0) then
        if handle_command_press() then
            -- handled by the command panel
        elseif minimap_on and in_rect(mouse_x, mouse_y, vw - 196.0, vh - 286.0, 190.0, 150.0) then
            local fx = clampf((mouse_x - (vw - 190.0)) / 128.0, 0.0, 1.0)
            local fy = clampf((mouse_y - (vh - 280.0)) / 96.0, 0.0, 1.0)
            camera_set_position(fx * COLS * TILE - vw * inv_zoom * 0.5,
                                fy * ROWS * TILE - vh * inv_zoom * 0.5)
        elseif not over_hud then
            if placing_type >= 0 then
                if try_place_building(hover_c, hover_r) then placing_type = -1 end
            else
                clear_selection()
                drag_active = true
                drag_sx = mouse_x
                drag_sy = mouse_y
                drag_wx = wx
                drag_wy = wy
            end
        end
    end

    if game_over == 0 and input_mouse_pressed(2) then
        if placing_type >= 0 then
            placing_type = -1
        elseif not over_hud then
            issue_order(wx, wy)
        end
    end

    if drag_active and not input_mouse_down(0) then
        drag_active = false
        finish_selection(wx, wy)
    end
end

-- ---------------------------------------------------------------------------
-- init
-- ---------------------------------------------------------------------------

local function add_initial_base(owner)
    if owner == P then
        -- Command yard faces the centre so attacking waves meet it first.
        spawn_building(P, B_CY, 17, 33, true)
        spawn_building(P, B_POWER, 13, 33, true)
        spawn_building(P, B_TURRET, 9, 33, true)
        spawn_building(P, B_BARRACKS, 17, 37, true)
        spawn_building(P, B_REFINERY, 13, 37, true)
        spawn_building(P, B_FACTORY, 9, 37, true)
    else
        spawn_building(E, B_CY, 46, 14, true)
        spawn_building(E, B_POWER, 50, 14, true)
        spawn_building(E, B_TURRET, 54, 14, true)
        spawn_building(E, B_BARRACKS, 46, 10, true)
        spawn_building(E, B_REFINERY, 50, 10, true)
        spawn_building(E, B_FACTORY, 54, 10, true)
    end
end

local function add_initial_units(owner)
    if owner == P then
        spawn_unit(P, U_INF, 15.0 * TILE + 16.0, 42.0 * TILE + 16.0)
        spawn_unit(P, U_INF, 16.0 * TILE + 16.0, 42.0 * TILE + 16.0)
        spawn_unit(P, U_INF, 17.0 * TILE + 16.0, 42.0 * TILE + 16.0)
        spawn_unit(P, U_ROCKET, 16.0 * TILE + 16.0, 41.0 * TILE + 16.0)
        spawn_unit(P, U_TANK, 14.0 * TILE + 16.0, 41.0 * TILE + 16.0)
        spawn_unit(P, U_HARV, 17.0 * TILE + 16.0, 40.0 * TILE + 16.0)
    else
        spawn_unit(E, U_INF, 48.0 * TILE + 16.0, 18.0 * TILE + 16.0)
        spawn_unit(E, U_INF, 49.0 * TILE + 16.0, 18.0 * TILE + 16.0)
        spawn_unit(E, U_INF, 50.0 * TILE + 16.0, 18.0 * TILE + 16.0)
        spawn_unit(E, U_ROCKET, 49.0 * TILE + 16.0, 19.0 * TILE + 16.0)
        spawn_unit(E, U_TANK, 47.0 * TILE + 16.0, 18.0 * TILE + 16.0)
        spawn_unit(E, U_HARV, 52.0 * TILE + 16.0, 18.0 * TILE + 16.0)
    end
end

local function setup_scene()
    map_id = map_load("assets/rts/map.json")
    tex_inf = sprite_load_nearest("assets/rts/units/infantry.png")
    tex_rocket = sprite_load_nearest("assets/rts/units/rocket.png")
    tex_tank = sprite_load_nearest("assets/rts/units/tank.png")
    tex_harv = sprite_load_nearest("assets/rts/units/harvester.png")
    tex_cy = sprite_load_nearest("assets/rts/buildings/cy.png")
    tex_power = sprite_load_nearest("assets/rts/buildings/power.png")
    tex_barracks = sprite_load_nearest("assets/rts/buildings/barracks.png")
    tex_factory = sprite_load_nearest("assets/rts/buildings/factory.png")
    tex_refinery = sprite_load_nearest("assets/rts/buildings/refinery.png")
    tex_turret = sprite_load_nearest("assets/rts/buildings/turret.png")
    boom_fx = part_create()
    part_set_lifetime(boom_fx, 0.35, 0.9)
    part_set_speed(boom_fx, 40, 210)
    part_set_direction(boom_fx, -1.5708, 3.1416)
    part_set_size(boom_fx, 3, 9)
    part_set_gravity(boom_fx, 0, 30)
    part_set_drag(boom_fx, 1.2)
    part_set_spin(boom_fx, 6)
    part_set_color(boom_fx, 1.0, 0.72, 0.32, 1.0, 0.0)
    camera_set_bounds(0.0, 0.0, COLS * TILE, ROWS * TILE)
    camera_set_zoom(1.0)
    camera_set_position(14.0 * TILE - VIEW_W * 0.5, 38.0 * TILE - VIEW_H * 0.5)
    add_initial_base(P)
    add_initial_base(E)
    add_initial_units(P)
    add_initial_units(E)
    audio_loop("rts_theme", 0.12, 1.0)
    log_info("rts: match start")
end

-- ---------------------------------------------------------------------------
-- drawing
-- ---------------------------------------------------------------------------

local function hquad(x, y, w, h, r, g, b, a)
    draw_quad(cam_left + x * inv_zoom, cam_top + y * inv_zoom,
              w * inv_zoom, h * inv_zoom, r, g, b, a)
end

local function htext(text, x, y, s, r, g, b, a)
    draw_text(text, cam_left + x * inv_zoom, cam_top + y * inv_zoom,
              s * inv_zoom, r, g, b, a)
end

local function draw_building_sprites()
    local n = ent_count(TAG_BLD)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_BLD)
        local x = ent_num(id, S_X)
        local y = ent_num(id, S_Y)
        local owner = ent_num(id, S_OWNER)
        local btype = ent_num(id, S_TYPE)
        local tex = bld_tex(btype)
        local tr, tg, tb = team_r(owner), team_g(owner), team_b(owner)
        if ent_num(id, S_FLASH) > 0.0 then tr = 1.0; tg = 1.0; tb = 1.0 end
        draw_quad(x + 5.0, y + 52.0, 54.0, 10.0, 0.0, 0.0, 0.0, 0.22)
        local alpha = 1.0
        if ent_num(id, S_STATE) == BST_BUILDING then alpha = 0.62 end
        sprite_draw_tinted(tex, x, y, 64.0, 64.0, tr, tg, tb, alpha)
        if ent_num(id, S_STATE) == BST_BUILDING then
            local btotal = bld_build_frames(btype)
            local p = ent_num(id, S_TIMER) / btotal
            draw_quad(x + 8.0, y + 66.0, 48.0, 4.0, 0.0, 0.0, 0.0, 0.7)
            draw_quad(x + 8.0, y + 66.0, 48.0 * p, 4.0, 0.55, 0.85, 1.0, 0.9)
        end
        if ent_num(id, S_SELECT) > 0.0 then
            draw_rect_outline(x, y, 64.0, 64.0, 0.95, 0.95, 0.80, 0.95)
        end
        local hp = ent_num(id, S_HP)
        local maxhp = bld_hp(btype)
        if hp < maxhp - 0.1 or ent_num(id, S_SELECT) > 0.0 then
            draw_quad(x + 6.0, y - 8.0, 52.0, 4.0, 0.0, 0.0, 0.0, 0.7)
            local ratio = hp / maxhp
            if ratio < 0.0 then ratio = 0.0 end
            draw_quad(x + 6.0, y - 8.0, 52.0 * ratio, 4.0,
                      clampf(1.4 - ratio, 0.2, 1.0), 0.8, 0.25, 0.95)
        end
        if btype == B_TURRET and ent_num(id, S_STATE) == BST_ACTIVE then
            local tx = x + 32.0
            local ty = y + 40.0
            local target = to_int(ent_num(id, S_TARGET))
            local bx = x + 32.0
            local by2 = y + 40.0
            if target ~= 0.0 and ent_active(target) then
                local dx = ent_cx(target) - bx
                local dy = ent_cy(target) - by2
                local d = math.sqrt(dx * dx + dy * dy)
                if d < 1.0 then d = 1.0 end
                local len = d
                if len > 30.0 then len = 30.0 end
                tx = bx + dx / d * len
                ty = by2 + dy / d * len
            else
                tx = bx
                ty = by2 - 26.0
            end
            draw_line(x + 32.0, y + 40.0, tx, ty, 0.22, 0.22, 0.20, 1.0)
            draw_line(x + 32.0, y + 40.0, tx + 1.0, ty, 0.42, 0.42, 0.38, 1.0)
        end
        i = i + 1
    end
end

local function draw_unit_sprites()
    local row_counts = {}
    local row_ids = {}
    local r = 0
    while r < ROWS do
        row_counts[r] = 0
        r = r + 1
    end
    local n = ent_count(TAG_UNIT)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_UNIT)
        local row = math.floor(ent_num(id, S_Y) / TILE)
        if row < 0 then row = 0 end
        if row >= ROWS then row = ROWS - 1 end
        local k = row_counts[row] + 1
        row_counts[row] = k
        row_ids[row * 40 + k] = id
        i = i + 1
    end
    r = 0
    while r < ROWS do
        local k = row_counts[r]
        while k >= 1 do
            local id = row_ids[r * 40 + k]
            if id ~= nil and id ~= 0 then
                local x = ent_num(id, S_X)
                local y = ent_num(id, S_Y)
                local owner = ent_num(id, S_OWNER)
                local utype = ent_num(id, S_TYPE)
                local state = ent_num(id, S_STATE)
                local dx = 0.0
                local dy = -1.0
                if state == ST_ATTACK or state == ST_ATTACK_MOVE then
                    local target = to_int(ent_num(id, S_TARGET))
                    if target ~= 0.0 and ent_active(target) then
                        dx = ent_cx(target) - x
                        dy = ent_cy(target) - y
                    end
                elseif state == ST_MOVE or state == ST_HARV_MOVE or state == ST_RETURN then
                    dx = ent_num(id, S_GX) - x
                    dy = ent_num(id, S_GY) - y
                end
                local ang = 0.0
                if math.abs(dx) + math.abs(dy) > 0.1 then
                    ang = math.atan2(dy, dx)
                    if utype == U_TANK or utype == U_HARV then
                        ang = ang + math.pi * 0.5
                    end
                end
                local tr, tg, tb = team_r(owner), team_g(owner), team_b(owner)
                if ent_num(id, S_FLASH) > 0.0 then tr = 1.0; tg = 1.0; tb = 1.0 end
                local size = 26.0
                if utype == U_TANK then size = 28.0 end
                if utype == U_HARV then size = 30.0 end
                draw_quad(x - 12.0, y + 10.0, 24.0, 6.0, 0.0, 0.0, 0.0, 0.2)
                draw_quad(x - 9.0, y + 11.0, 18.0, 3.0, tr, tg, tb, 0.85)
                sprite_draw_rotated_tinted(unit_tex(utype), 0.0, 0.0, 32.0, 32.0,
                                           x - size * 0.5, y - size * 0.5,
                                           size, size, ang, size * 0.5, size * 0.5,
                                           tr, tg, tb, 1.0)
                if ent_num(id, S_SELECT) > 0.0 then
                    draw_circle_outline(x, y, 15.0, 16, 0.95, 0.95, 0.80, 0.9)
                end
                local maxhp = unit_hp(utype)
                local hp = ent_num(id, S_HP)
                if hp < maxhp - 0.1 or ent_num(id, S_SELECT) > 0.0 then
                    local ratio = hp / maxhp
                    if ratio < 0.0 then ratio = 0.0 end
                    draw_quad(x - 11.0, y - 17.0, 22.0, 3.0, 0.0, 0.0, 0.0, 0.75)
                    draw_quad(x - 11.0, y - 17.0, 22.0 * ratio, 3.0,
                              clampf(1.4 - ratio, 0.2, 1.0), 0.78, 0.24, 1.0)
                end
                if utype == U_HARV and ent_num(id, S_AUX) > 0.0 then
                    local cargo = ent_num(id, S_AUX) / 300.0
                    draw_quad(x - 10.0, y + 13.0, 20.0 * cargo, 2.0,
                              0.95, 0.78, 0.25, 1.0)
                end
            end
            k = k - 1
        end
        r = r + 1
    end
end

local function draw_projectiles()
    local n = ent_count(TAG_PROJ)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_PROJ)
        local x = ent_num(id, S_X)
        local y = ent_num(id, S_Y)
        local kind = ent_num(id, S_TYPE)
        if kind == 0 then
            draw_line(x, y, x - 8.0, y, 1.0, 0.95, 0.55, 0.9)
            draw_quad(x - 2.0, y - 1.0, 4.0, 2.0, 1.0, 0.92, 0.55, 1.0)
        elseif kind == 1 then
            draw_quad(x - 3.0, y - 3.0, 6.0, 6.0, 1.0, 0.65, 0.30, 1.0)
            draw_quad(x - 1.0, y - 1.0, 2.0, 2.0, 1.0, 0.95, 0.70, 1.0)
        else
            draw_quad(x - 3.0, y - 3.0, 6.0, 6.0, 0.95, 0.82, 0.42, 1.0)
        end
        i = i + 1
    end
end

local function draw_world()
    map_draw(map_id)
    draw_building_sprites()
    draw_unit_sprites()
    draw_projectiles()
    part_draw()

    if placing_type >= 0 and hover_c >= 0.0 and hover_r >= 0.0 and
       hover_c < COLS and hover_r < ROWS then
        local occ = make_occupancy()
        local ok = can_build_at(hover_c, hover_r, occ)
        local col = hover_c
        local row = hover_r
        local alpha = 0.16
        local cr, cg, cb = 0.35, 0.85, 0.45
        if not ok then
            alpha = 0.30
            cr = 0.90
            cg = 0.30
            cb = 0.25
        end
        local c = 0
        while c < 2 do
            local r = 0
            while r < 2 do
                draw_quad((col + c) * TILE, (row + r) * TILE, TILE, TILE,
                          cr, cg, cb, alpha)
                r = r + 1
            end
            c = c + 1
        end
        draw_rect_outline(col * TILE, row * TILE, 64.0, 64.0,
                          ok and 0.5 or 1.0, ok and 1.0 or 0.4, 0.4, 1.0)
    end

    if drag_active then
        local x0 = drag_wx
        local y0 = drag_wy
        local x1 = camera_screen_to_world_x(mouse_x, mouse_y)
        local y1 = camera_screen_to_world_y(mouse_x, mouse_y)
        if x0 > x1 then local tx = x0; x0 = x1; x1 = tx end
        if y0 > y1 then local ty = y0; y0 = y1; y1 = ty end
        draw_quad(x0, y0, x1 - x0, y1 - y0, 0.4, 0.85, 1.0, 0.12)
        draw_rect_outline(x0, y0, x1 - x0, y1 - y0, 0.6, 0.95, 1.0, 0.9)
    end
end

local function draw_minimap()
    local mx = vw - 190.0
    local my = vh - 280.0
    hquad(mx - 5.0, my - 20.0, 184.0, 146.0, 0.04, 0.06, 0.08, 0.92)
    hrect(mx - 5.0, my - 20.0, 184.0, 146.0, 0.28, 0.38, 0.32, 0.9)
    htext("TACTICAL MAP", mx, my - 16.0, 0.34, 0.72, 0.86, 0.64, 1.0)

    local r = 0
    while r < ROWS do
        local c = 0
        while c < COLS do
            local g = tile_at(c, r)
            local cr, cg, cb = 0.20, 0.38, 0.18
            if g == G_DIRT then cr, cg, cb = 0.42, 0.36, 0.24 end
            if g == G_ROCK then cr, cg, cb = 0.48, 0.46, 0.42 end
            if g == G_WATER then cr, cg, cb = 0.14, 0.32, 0.52 end
            if g == G_ORE then cr, cg, cb = 0.90, 0.76, 0.22 end
            if g == G_ORE_POOR then cr, cg, cb = 0.68, 0.56, 0.24 end
            if g == G_CONCRETE then cr, cg, cb = 0.44, 0.46, 0.44 end
            if g == G_TREE then cr, cg, cb = 0.12, 0.28, 0.12 end
            if g == G_SAND then cr, cg, cb = 0.66, 0.60, 0.44 end
            if g == G_CRATER then cr, cg, cb = 0.24, 0.20, 0.18 end
            hquad(mx + c * 2.0, my + r * 2.0, 4.0, 4.0, cr, cg, cb, 1.0)
            c = c + 2
        end
        r = r + 2
    end

    local n = ent_count(TAG_BLD)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_BLD)
        local owner = ent_num(id, S_OWNER)
        local x = mx + (ent_num(id, S_X) + 32.0) / (COLS * TILE) * 128.0
        local y = my + (ent_num(id, S_Y) + 32.0) / (ROWS * TILE) * 96.0
        hquad(x - 2.5, y - 2.5, 5.0, 5.0, team_r(owner), team_g(owner), team_b(owner), 1.0)
        i = i + 1
    end
    n = ent_count(TAG_UNIT)
    i = 0
    while i < n do
        local id = ent_at(i, TAG_UNIT)
        local owner = ent_num(id, S_OWNER)
        local x = mx + ent_num(id, S_X) / (COLS * TILE) * 128.0
        local y = my + ent_num(id, S_Y) / (ROWS * TILE) * 96.0
        hquad(x - 1.0, y - 1.0, 2.0, 2.0, team_r(owner), team_g(owner), team_b(owner), 1.0)
        i = i + 1
    end

    local vx = mx + cam_left / (COLS * TILE) * 128.0
    local vy = my + cam_top / (ROWS * TILE) * 96.0
    local vw2 = (vw * inv_zoom) / (COLS * TILE) * 128.0
    local vh2 = (vh * inv_zoom) / (ROWS * TILE) * 96.0
    hrect(vx, vy, vw2, vh2, 0.94, 0.94, 0.80, 0.9)
end

local function draw_topbar()
    hquad(0.0, 0.0, vw, 42.0, 0.05, 0.07, 0.06, 0.95)
    hquad(0.0, 40.0, vw, 2.0, 0.32, 0.48, 0.28, 0.95)
    htext("RED ALERT // FAKE2D", 10.0, 8.0, 0.46, 0.92, 0.86, 0.52, 1.0)
    htext("COMMAND", 10.0, 28.0, 0.30, 0.50, 0.62, 0.42, 0.9)

    htext("CREDITS", 180.0, 8.0, 0.30, 0.56, 0.68, 0.58, 1.0)
    htext("$" .. fmt_int(credits_p), 180.0, 22.0, 0.48, 1.0, 0.88, 0.40, 1.0)

    htext("POWER", 300.0, 8.0, 0.30, 0.56, 0.68, 0.58, 1.0)
    local ratio = power_ratio_p
    local pr, pg, pb = 0.30, 0.82, 0.40
    if ratio < 1.0 then pr, pg, pb = 0.92, 0.45, 0.24 end
    hquad(300.0, 27.0, 110.0, 9.0, 0.10, 0.14, 0.10, 1.0)
    hquad(300.0, 27.0, 110.0 * ratio, 9.0, pr, pg, pb, 1.0)
    hrect(300.0, 27.0, 110.0, 9.0, 0.30, 0.40, 0.32, 0.9)
    htext(fmt_int(ratio * 100.0) .. "%", 416.0, 24.0, 0.30,
          0.74, 0.86, 0.72, 1.0)

    htext("BLUE " .. fmt_int(own_count(P, false)), 506.0, 10.0, 0.32,
          0.56, 0.82, 1.0, 1.0)
    htext("RED " .. fmt_int(own_count(E, false)), 506.0, 25.0, 0.32,
          1.0, 0.60, 0.52, 1.0)

    htext("DESTROY ENEMY COMMAND YARD", 610.0, 10.0, 0.30,
          0.70, 0.78, 0.64, 0.9)
    local secs = math.floor(match_frames / 60.0)
    local mm = math.floor(secs / 60.0)
    local ss = secs % 60
    htext(string.format("%02d:%02d", mm, ss), vw - 66.0, 22.0, 0.40,
          0.80, 0.88, 0.78, 1.0)
end

local function draw_selection_panel()
    local x = 8.0
    local y = vh - 216.0
    local uid = first_selected_unit()
    local bid = first_selected_bld()
    if uid == 0 and bid == 0 then return end
    hquad(x, y, 344.0, 80.0, 0.05, 0.07, 0.06, 0.92)
    hrect(x, y, 344.0, 80.0, 0.28, 0.40, 0.28, 0.9)
    if uid ~= 0 then
        local utype = ent_num(uid, S_TYPE)
        local hp = ent_num(uid, S_HP)
        local maxhp = unit_hp(utype)
        local count = selected_count()
        htext(unit_name(utype) .. "  x" .. fmt_int(count), x + 10.0, y + 8.0,
              0.36, 0.90, 0.94, 0.84, 1.0)
        hquad(x + 10.0, y + 32.0, 318.0, 8.0, 0.10, 0.13, 0.10, 1.0)
        local ratio = hp / maxhp
        if ratio < 0.0 then ratio = 0.0 end
        hquad(x + 10.0, y + 32.0, 318.0 * ratio, 8.0,
              0.34, 0.80, 0.38, 1.0)
        hrect(x + 10.0, y + 32.0, 318.0, 8.0, 0.30, 0.42, 0.32, 0.9)
        htext("HP " .. fmt_int(hp) .. " / " .. fmt_int(maxhp), x + 10.0, y + 48.0,
              0.32, 0.72, 0.82, 0.74, 1.0)
        if utype == U_HARV then
            htext("ORE " .. fmt_int(ent_num(uid, S_AUX)) .. " / 300", x + 190.0,
                  y + 48.0, 0.32, 0.92, 0.82, 0.36, 1.0)
        end
    else
        local btype = ent_num(bid, S_TYPE)
        local hp = ent_num(bid, S_HP)
        local maxhp = bld_hp(btype)
        htext(bld_name(btype), x + 10.0, y + 8.0, 0.36, 0.90, 0.94, 0.84, 1.0)
        local ratio = hp / maxhp
        if ratio < 0.0 then ratio = 0.0 end
        hquad(x + 10.0, y + 32.0, 318.0, 8.0, 0.10, 0.13, 0.10, 1.0)
        hquad(x + 10.0, y + 32.0, 318.0 * ratio, 8.0, 0.34, 0.80, 0.38, 1.0)
        hrect(x + 10.0, y + 32.0, 318.0, 8.0, 0.30, 0.42, 0.32, 0.9)
        if ent_num(bid, S_STATE) == BST_BUILDING then
            local p = ent_num(bid, S_TIMER) / bld_build_frames(btype)
            htext("CONSTRUCTION " .. fmt_int(p * 100.0) .. "%", x + 10.0, y + 48.0,
                  0.32, 0.72, 0.88, 0.94, 1.0)
        else
            htext("HP " .. fmt_int(hp) .. " / " .. fmt_int(maxhp), x + 10.0,
                  y + 48.0, 0.32, 0.72, 0.82, 0.74, 1.0)
        end
    end
end

local function draw_command_panel()
    local by = vh - 132.0
    hquad(0.0, by, vw, 132.0, 0.05, 0.07, 0.06, 0.95)
    hquad(0.0, by, vw, 2.0, 0.32, 0.48, 0.28, 0.95)
    local i = 0
    while i < 5 do
        local x = 8.0 + i * 94.0
        local y = by + 8.0
        local btype = i + 1
        local cost = bld_cost(btype)
        local hot = string.format("%d", i + 1)
        local active = placing_type == btype
        local bg = 0.10
        if active then bg = 0.20 end
        if in_rect(mouse_x, mouse_y, x, y, 86.0, 54.0) then bg = bg + 0.08 end
        hquad(x, y, 86.0, 54.0, bg, bg + 0.02, bg, 0.95)
        local br, bgc, bb = 0.72, 0.72, 0.62
        if btype == B_POWER then br, bgc, bb = 0.94, 0.78, 0.28 end
        if btype == B_REFINERY then br, bgc, bb = 0.36, 0.74, 0.70 end
        if btype == B_BARRACKS then br, bgc, bb = 0.48, 0.76, 0.40 end
        if btype == B_FACTORY then br, bgc, bb = 0.82, 0.58, 0.30 end
        hrect(x, y, 86.0, 54.0, br, bgc, bb, active and 1.0 or 0.55)
        hquad(x + 6.0, y + 6.0, 12.0, 12.0, br, bgc, bb, 1.0)
        htext(hot, x + 72.0, y + 4.0, 0.28, 0.86, 0.88, 0.72, 1.0)
        htext(bld_name(btype), x + 6.0, y + 24.0, 0.34, 0.92, 0.94, 0.88, 1.0)
        htext("$" .. fmt_int(cost), x + 6.0, y + 39.0, 0.30,
              0.92, 0.82, 0.40, 1.0)
        i = i + 1
    end
    i = 0
    while i < 4 do
        local x = 8.0 + i * 94.0
        local y = by + 70.0
        local utype = i
        local cost = unit_cost(utype)
        local hot = string.format("%d", i + 6)
        local bg = 0.10
        if in_rect(mouse_x, mouse_y, x, y, 86.0, 54.0) then bg = bg + 0.08 end
        hquad(x, y, 86.0, 54.0, bg, bg + 0.02, bg, 0.95)
        local br, bgc, bb = 0.52, 0.68, 0.52
        if utype == U_ROCKET then br, bgc, bb = 0.90, 0.58, 0.26 end
        if utype == U_TANK then br, bgc, bb = 0.60, 0.64, 0.60 end
        if utype == U_HARV then br, bgc, bb = 0.92, 0.78, 0.30 end
        hrect(x, y, 86.0, 54.0, br, bgc, bb, 0.55)
        hquad(x + 6.0, y + 6.0, 12.0, 12.0, br, bgc, bb, 1.0)
        htext(hot, x + 72.0, y + 4.0, 0.28, 0.86, 0.88, 0.72, 1.0)
        htext(unit_name(utype), x + 6.0, y + 24.0, 0.34, 0.92, 0.94, 0.88, 1.0)
        htext("$" .. fmt_int(cost), x + 6.0, y + 39.0, 0.30,
              0.92, 0.82, 0.40, 1.0)
        i = i + 1
    end

    local px = 486.0
    local py = by + 8.0
    local pw = vw - 494.0
    hquad(px, py, pw, 116.0, 0.04, 0.06, 0.05, 0.9)
    hrect(px, py, pw, 116.0, 0.24, 0.34, 0.24, 0.9)
    htext("PRODUCTION", px + 10.0, py + 8.0, 0.34, 0.78, 0.86, 0.62, 1.0)
    if prod_p_type >= 0 then
        htext(unit_name(prod_p_type), px + 10.0, py + 28.0, 0.36,
              0.92, 0.94, 0.86, 1.0)
        local p = prod_p_timer / prod_p_total
        if p > 1.0 then p = 1.0 end
        hquad(px + 10.0, py + 50.0, pw - 20.0, 9.0, 0.10, 0.13, 0.10, 1.0)
        hquad(px + 10.0, py + 50.0, (pw - 20.0) * p, 9.0, 0.36, 0.82, 0.42, 1.0)
        hrect(px + 10.0, py + 50.0, pw - 20.0, 9.0, 0.30, 0.42, 0.32, 0.9)
        htext(fmt_int(p * 100.0) .. "%", px + 10.0, py + 64.0, 0.30,
              0.72, 0.82, 0.74, 1.0)
    else
        htext("SELECT A UNIT BUTTON TO TRAIN", px + 10.0, py + 28.0, 0.32,
              0.62, 0.70, 0.60, 1.0)
        htext("HARVESTERS MINE ORE AUTOMATICALLY", px + 10.0, py + 46.0, 0.30,
              0.62, 0.70, 0.60, 1.0)
        htext("RIGHT-CLICK CANCELS PLACEMENT", px + 10.0, py + 64.0, 0.30,
              0.62, 0.70, 0.60, 1.0)
    end
    if placing_type >= 0 then
        htext("PLACING " .. bld_name(placing_type) .. "  $" ..
              fmt_int(bld_cost(placing_type)), px + 10.0, py + 92.0, 0.32,
              0.96, 0.88, 0.48, 1.0)
    elseif power_ratio_p < 1.0 then
        htext("LOW POWER", px + 10.0, py + 92.0, 0.34, 0.96, 0.52, 0.36, 1.0)
    else
        htext("WASD PAN  WHEEL ZOOM  M MAP", px + 10.0, py + 92.0, 0.30,
              0.56, 0.64, 0.56, 1.0)
    end
end

local function draw_alert()
    if alert_timer > 0 and alert_code ~= 0 then
        local text = alert_text(alert_code)
        local w = text_width(text, 0.42) + 36.0
        local x = (vw - w) * 0.5
        hquad(x, 48.0, w, 28.0, 0.28, 0.09, 0.06, 0.94)
        hrect(x, 48.0, w, 28.0, 0.95, 0.48, 0.32, 0.95)
        local tw = text_width(text, 0.42)
        htext(text, (vw - tw) * 0.5, 55.0, 0.42, 1.0, 0.88, 0.78, 1.0)
    end
end

local function draw_gameover()
    if game_over == 0 then return end
    hquad(0.0, 200.0, vw, 120.0, 0.03, 0.04, 0.03, 0.92)
    hquad(0.0, 198.0, vw, 2.0, 0.90, 0.76, 0.36, 0.95)
    local title = "MISSION COMPLETE"
    local sub = "THE ENEMY COMMAND YARD HAS BEEN DESTROYED"
    if game_over == 2 then
        title = "BASE DESTROYED"
        sub = "THE COMMAND YARD HAS FALLEN - THE BATTLE IS OVER"
    end
    local tw = text_width(title, 0.80)
    htext(title, (vw - tw) * 0.5, 224.0, 0.80, 0.96, 0.86, 0.46, 1.0)
    local sw = text_width(sub, 0.34)
    htext(sub, (vw - sw) * 0.5, 276.0, 0.34, 0.78, 0.84, 0.72, 1.0)
end

local function draw_hud()
    draw_topbar()
    if minimap_on then draw_minimap() end
    draw_selection_panel()
    draw_command_panel()
    draw_alert()
    draw_gameover()
end

function update(dt)
    frame_no = frame_no + 1
    match_frames = match_frames + 1
    if not inited then
        inited = true
        setup_scene()
    end

    vw = camera_viewport_w()
    vh = camera_viewport_h()
    mouse_x = input_mouse_x()
    mouse_y = input_mouse_y()

    if map_id == 0 or tex_inf == 0 or tex_cy == 0 then
        draw_text("RTS ASSETS FAILED TO LOAD", 40.0, 60.0, 0.7, 1.0, 0.4, 0.4, 1.0)
        return 0
    end

    update_keys()
    update_camera()
    update_power()
    local occ = make_occupancy()
    update_buildings()
    update_units(occ)
    update_production()
    update_projectiles()
    update_ai()
    update_pointer()
    auto_camera()

    if alert_timer > 0 then alert_timer = alert_timer - 1 end
    if alert_timer == 0 then alert_code = 0 end
    if power_ratio_p < 1.0 and frame_no % 240 == 0 then notify(4) end

    draw_world()
    draw_hud()
    return 0
end
