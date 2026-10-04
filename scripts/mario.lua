-- fake2d Super Mario-style 1-1 gameplay (--mario-demo).
-- Pure Lua game logic over engine primitives; enemies/items/effects live in
-- the C++ entity store (ent_*) so they survive FakeLua's per-frame reset.
--
-- Phase 8 systems on show here:
--   * EntityStore  — every goomba, mushroom, block bump, score popup and
--                     tile-restore record is an ent_* slot, which is what
--                     makes them survive the per-frame arena reset.
--   * Tilemap slopes — the hill at cols 19..24 is built from Tiled `slope`
--                     tiles. The player walks it via map_slope_dir and
--                     map_ground_y (see ground_probe / snap_to_slope), which
--                     is the only mario terrain the script reads back as a
--                     surface rather than as a solid cell.
--   * SaveStore     — the high score and best remaining time are banked to
--                     saves/mario.json on clear and re-read on the next run
--                     (storage_*), shown as "BEST" in the HUD.
--   * Custom shader — the goal pole runs through a per-draw GLSL slot
--                     (shader_load / shader_set_* / draw_use_shader); the band
--                     travels down it as the frame counter advances.
--
-- Controls: Left/Right or A/D move, Shift run, Space/W/Up jump.
-- Headless: deterministic attract AI plays automatically (runs, clears
-- gaps/pipes and long-hops goombas); clear/game-over screens auto-reopen
-- after a few seconds so an unattended attract run loops forever.
-- Debug hooks (test only): MARIO_TEST_DEATH=<frame>, MARIO_TEST_BIG=1,
--   MARIO_TEST_WIN=<frame>, MARIO_TEST_RIGHT=1, MARIO_TEST_JUMP=1,
--   MARIO_TEST_DIE3=1, MARIO_TEST_TP=<px>, MARIO_TEST_MUSH=1,
--   MARIO_TEST_PHYS=1, MARIO_TEST_BLOCKS=1.

-- gids (must match the host-generated assets/mario.json)
local G_GROUND, G_DIRT, G_BRICK, G_QUESTION, G_USED = 1, 2, 3, 4, 5
local G_PIPE_TL, G_PIPE_TR, G_PIPE_BL, G_PIPE_BR, G_COIN = 6, 7, 8, 9, 10
local G_POLEBALL, G_POLE, G_FLAG, G_STONE, G_GOOMBA_MARK = 11, 12, 13, 14, 15

-- entity tags
local TAG_GOOMBA, TAG_MUSH = 1, 2
local TAG_BUMP, TAG_POPUP, TAG_RESTORE = 10, 11, 20
-- goomba/mushroom number slots
local S_X, S_Y, S_VX, S_VY, S_STATE, S_T, S_DIR = 0, 1, 2, 3, 4, 5, 6
local GOOMBA_WALK, GOOMBA_SQUASH, GOOMBA_DEAD = 1, 2, 3
local MUSH_EMERGE, MUSH_WALK = 0, 1

local TILE = 32
local GROUND_TOP = 13 * TILE
local MAP_W = 112 * TILE
local POLE_COL = 102
local CASTLE_X = 105 * TILE

-- modes
local M_PLAY, M_DYING, M_GAMEOVER = 1, 2, 3
local M_SLIDE, M_WALK, M_CLEAR = 4, 5, 6

-- tuning
local WALK_SPEED, RUN_SPEED = 150.0, 235.0
local G_ACC, A_ACC = 2100.0, 1250.0
local FRICTION = 1900.0
local GRAV, MAX_FALL = 1500.0, 640.0
-- upward launch speed (negative y = up); 625 -> ~130px (4 tiles) apex
local JUMP_V = 625.0
local JUMP_CUT = 0.42
local COYOTE, JUMP_BUFFER = 0.15, 0.14
local GOOMBA_SPEED = 58.0
local MUSH_SPEED = 72.0
local SLIDE_SPEED = 260.0
local WALK_OUT_SPEED = 95.0

local map_id = 0 + 0
local tex_sheet, tex_goomba, tex_mush = 0 + 0, 0 + 0, 0 + 0
local tex_ps, tex_pb = 0 + 0, 0 + 0
local fx_debris = 0 + 0
local inited = false

-- Phase 8: per-draw custom GLSL shader slot. The level-clear flag gets a
-- travelling highlight; everything else stays on the default program.
local fx_flag_shader = 0 + 0
-- Phase 8: SaveStore slot for the persistent high score / best time.
local SAVE_SLOT = "mario"
local best_score = 0 + 0
local best_time = 0 + 0
local new_record = false

local px, py = 0.0 + 0.0, 0.0 + 0.0
local vx, vy = 0.0 + 0.0, 0.0 + 0.0
local box_w, box_h = 20.0 + 0.0, 28.0 + 0.0
local big = 0 + 0
local on_ground = false
local facing = 1 + 0
local coyote_t, buffer_t = 0.0 + 0.0, 0.0 + 0.0
local cut_used = false
local jump_hold = 0 + 0
local ai_cd_t = 0.0 + 0.0
local human_seen = 0 + 0
local invinc = 0.0 + 0.0
local mode = M_PLAY + 0
local mtime = 0.0 + 0.0
local win_bonus = 0 + 0
local death_done = false

local score, coins, lives, time_left = 0 + 0, 0 + 0, 3 + 0, 300 + 0
local time_acc = 0.0 + 0.0
local frame_n = 0 + 0
local cam_x = 0.0 + 0.0
local cam_y = 0.0 + 0.0
local cam_vw = 960.0 + 0.0
local cam_vh = 540.0 + 0.0

-- head-bit result reported by move_y
local hit_ok = false
local hit_col, hit_row = 0 + 0, 0 + 0

-- test hooks (parsed once)
local test_death = -1 + 0
local test_win = -1 + 0
local test_big = 0 + 0
local test_right = 0 + 0
local test_jump = 0 + 0
local test_die3 = 0 + 0
local test_tp = -1.0 + 0.0
local test_mush = 0 + 0
local test_phys = 0 + 0
local test_blocks = 0 + 0

-- DIE3 scripted death counter / verification flag
local die3_left = 0 + 0
local die3_checked = 0 + 0
-- PHYS selftest: phase clock / one-shot logs / jump apex tracking
local phys_t = 0.0 + 0.0
local phys_phase = 0 + 0
local phys_log1, phys_log2, phys_apex_log = 0 + 0, 0 + 0, 0 + 0
local jump_top = 9999.0 + 0.0
-- BLOCKS selftest: phase clock / one-shot logs / max bump offset
local blk_t = 0.0 + 0.0
local blk_phase = 0 + 0
local blk_flag = 0 + 0
local max_bump = 0.0 + 0.0
local bonk_kill_n = 0 + 0

local function clampf(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

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

-- Phase 8 tilemap slopes. The hill at cols 33..40 carries the Tiled `slope`
-- property on cells that step up one row each, which is what makes
-- GroundYAt's interpolation chain into one continuous incline.
--
-- A slope cell is deliberately NOT solid, so it never appears in map_solid
-- and never blocks a move on its own. These helpers exist so the rest of the
-- controller can tell "there is a ramp here" apart from "there is a wall".
local function is_slope(c, r)
    if map_slope_dir(map_id, c, r) ~= 0 then return true end
    if map_slope_dir(map_id, c, r + 1) ~= 0 then return true end
    if map_slope_dir(map_id, c, r + 2) ~= 0 then return true end
    return false
end

-- Walkable surface under a world x, or -1 for none. GroundYAt only scans the
-- rows within one tile of `reach_y` and returns the lowest candidate in that
-- window, so a single query can miss a ramp cell that sits a row above (or
-- below) the feet. Probing one tile up and one tile down and taking the
-- nearer result covers both the climb and the descent.
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

-- Follow the ramp. Called every frame while grounded: it reads the
-- interpolated surface and places the feet on it, so walking right carries the
-- player up the near side and down the far side with no per-tile math. vy is
-- zeroed so leftover fall speed does not fight the snap.
local function snap_to_slope()
    local feet = py + box_h
    -- Only the ramp's own column counts as ground here. Requiring the slope to
    -- be in the row the feet occupy (or the row just above, where the surface
    -- sits when climbing) keeps a jump that passes over the hill from being
    -- dragged back down onto it.
    local col = math.floor((px + box_w * 0.5) / TILE)
    local r_now = math.floor(feet / TILE)
    if map_slope_dir(map_id, col, r_now) == 0
        and map_slope_dir(map_id, col, r_now - 1) == 0 then
        if not on_ground then return end
    end
    local g = ground_probe(px + box_w * 0.5, feet)
    if g < 0.0 then return end
    -- Bounded follow: a surface far above or below the feet is a wall or a
    -- pit, not the ramp being walked.
    if g <= feet - 24.0 or g >= feet + 34.0 then return end
    py = g - box_h
    if vy > 0.0 then vy = 0.0 end
    on_ground = true
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

-- ---------------------------------------------------------------------------
-- score / coins
-- ---------------------------------------------------------------------------

-- Phase 8 SaveStore: the best score and the best remaining time survive a
-- process restart. storage_save/load take a slot name, so each run writes one
-- JSON file under the engine save directory.
local function load_records()
    if not storage_load(SAVE_SLOT) then return end
    best_score = storage_get_num("best_score", 0)
    best_time = storage_get_num("best_time", 0)
end

local function save_records()
    storage_set_num("best_score", best_score)
    storage_set_num("best_time", best_time)
    storage_set_str("version", "1.3")
    storage_save(SAVE_SLOT)
end

local function add_score(points, wx, wy)
    score = score + points
    local id = ent_create(TAG_POPUP)
    ent_set_num(id, S_X, wx)
    ent_set_num(id, S_Y, wy)
    ent_set_num(id, S_VX, points)
    ent_set_num(id, S_T, 0.75)
end

local function add_coin(wx, wy)
    coins = coins + 1
    if coins >= 100 then
        coins = coins - 100
        lives = lives + 1
        audio_play("win", 0.35)
    else
        audio_play("coin", 0.5)
    end
    add_score(200, wx, wy)
end

-- ---------------------------------------------------------------------------
-- level reset / entity spawning
-- ---------------------------------------------------------------------------

local function spawn_goomba(c, r)
    local id = ent_create(TAG_GOOMBA)
    ent_set_num(id, S_X, c * TILE + 2.0)
    ent_set_num(id, S_Y, r * TILE + 4.0)
    ent_set_num(id, S_VX, -GOOMBA_SPEED)
    ent_set_num(id, S_VY, 0.0)
    ent_set_num(id, S_STATE, GOOMBA_WALK)
    ent_set_num(id, S_T, 0.0)
    ent_set_num(id, S_DIR, -1.0)
    return id
end

local function spawn_mushroom(c, r)
    local id = ent_create(TAG_MUSH)
    -- slot 7 remembers the final y (standing on top of the source block)
    local base_y = r * TILE - 24.0
    ent_set_num(id, S_X, c * TILE + 4.0)
    ent_set_num(id, S_Y, base_y + 32.0)
    ent_set_num(id, S_VX, MUSH_SPEED)
    ent_set_num(id, S_VY, 0.0)
    ent_set_num(id, S_STATE, MUSH_EMERGE)
    ent_set_num(id, S_T, 0.0)
    ent_set_num(id, S_DIR, 1.0)
    ent_set_num(id, 7, base_y)
end

local function clear_tag(tag)
    local n = ent_count(tag)
    local i = 0
    while i < n do
        local id = ent_at(i, tag)
        if id == 0 then break end
        ent_destroy(id)
        n = ent_count(tag)
    end
end

local function reset_player()
    box_w, box_h = 20.0, 28.0
    if test_big == 1 then
        big = 1
        box_w, box_h = 24.0, 44.0
    else
        big = 0
    end
    px = 2.5 * TILE
    if test_tp >= 0.0 then px = test_tp end
    py = GROUND_TOP - box_h
    vx, vy = 0.0, 0.0
    on_ground = false
    facing = 1
    coyote_t, buffer_t = 0.0, 0.0
    cut_used = false
    jump_hold = 0
    invinc = 0.0
    mode = M_PLAY
    mtime = 0.0
    win_bonus = 0
    death_done = false
    cam_x = 0.0
    time_left = 300
    time_acc = 0.0
end

local function reset_level()
    -- restore destructible tiles (brick/?/coin/markers) from records
    local n = ent_count(TAG_RESTORE)
    for i = 0, n - 1 do
        local id = ent_at(i, TAG_RESTORE)
        if id ~= 0 then
            local c = ent_num(id, 0)
            local r = ent_num(id, 1)
            local g = ent_num(id, 2)
            map_set_tile(map_id, c, r, g)
        end
    end
    clear_tag(TAG_GOOMBA)
    clear_tag(TAG_MUSH)
    clear_tag(TAG_BUMP)
    clear_tag(TAG_POPUP)
    -- sweep goomba markers into entities, then clear the marker cells
    for r = 0, 14 do
        for c = 0, 111 do
            if map_tile(map_id, c, r) == G_GOOMBA_MARK then
                spawn_goomba(c, r)
                map_set_tile(map_id, c, r, 0)
            end
        end
    end
    reset_player()
    -- test hook: a mushroom rises from the ground near the spawn point
    if test_mush == 1 then spawn_mushroom(6, 13) end
end

local function full_reset()
    score = 0
    coins = 0
    lives = 3
    reset_level()
end

-- ---------------------------------------------------------------------------
-- blocks
-- ---------------------------------------------------------------------------

local function bonk_enemies_over(c, r)
    local cx = c * TILE + TILE * 0.5
    local band_lo = r * TILE - 12.0
    local band_hi = r * TILE + 16.0
    local n = ent_count(TAG_GOOMBA)
    for i = 0, n - 1 do
        local id = ent_at(i, TAG_GOOMBA)
        if id ~= 0 and ent_num(id, S_STATE) == GOOMBA_WALK then
            local gx = ent_num(id, S_X) + 14.0
            local feet = ent_num(id, S_Y) + 28.0
            local dx = gx - cx
            if dx > -30.0 and dx < 30.0 and feet >= band_lo and feet <= band_hi then
                ent_set_num(id, S_STATE, GOOMBA_DEAD)
                ent_set_num(id, S_VY, -320.0)
                add_score(200, gx, ent_num(id, S_Y))
                audio_play("paddle", 0.4)
                bonk_kill_n = bonk_kill_n + 1
                log_number(300007)
            end
        end
    end
end

local function start_bump(c, r)
    local id = ent_create(TAG_BUMP)
    ent_set_num(id, 0, c)
    ent_set_num(id, 1, r)
    ent_set_num(id, 2, 0.25)
end

local function bump_active(c, r)
    local n = ent_count(TAG_BUMP)
    for i = 0, n - 1 do
        local id = ent_at(i, TAG_BUMP)
        if id ~= 0 and ent_num(id, 0) == c and ent_num(id, 1) == r then
            return true
        end
    end
    return false
end

local function handle_block(c, r)
    local g = map_tile(map_id, c, r)
    if g == G_QUESTION then
        if bump_active(c, r) then return end
        map_set_tile(map_id, c, r, G_USED)
        start_bump(c, r)
        -- the mid-cluster block at (21,9) holds a mushroom; others hold coins
        if c == 21 and r == 9 then
            spawn_mushroom(c, r)
            audio_play("launch", 0.4)
            log_number(300006)
        else
            local wx = c * TILE + 8.0
            local wy = r * TILE - 6.0
            add_coin(wx, wy)
            log_number(300008)
        end
        bonk_enemies_over(c, r)
    elseif g == G_BRICK then
        if bump_active(c, r) then return end
        if big == 1 then
            map_set_tile(map_id, c, r, 0)
            audio_play("brick", 0.5)
            log_number(300003)
            local cx = c * TILE + TILE * 0.5
            local cy = r * TILE + TILE * 0.5
            part_set_color(fx_debris, 0.72, 0.31, 0.20, 1.0, 0.0)
            part_emit(fx_debris, 8, cx, cy)
        else
            start_bump(c, r)
            audio_play("brick", 0.5)
            log_number(300013)
        end
        bonk_enemies_over(c, r)
    end
end

-- ---------------------------------------------------------------------------
-- player physics
-- ---------------------------------------------------------------------------

local function move_x_player(dx)
    local nx = px + dx
    if not box_hits_solid(nx, py, box_w, box_h) then
        px = nx
        return
    end
    -- snap to the wall and kill horizontal speed
    if dx > 0.0 then
        local cc = math.floor((nx + box_w) / TILE)
        px = cc * TILE - box_w
    elseif dx < 0.0 then
        local cc = math.floor(nx / TILE)
        px = (cc + 1) * TILE
    end
    vx = 0.0
end

local function move_y_player(dy)
    local ny = py + dy
    if not box_hits_solid(px, ny, box_w, box_h) then
        py = ny
        return
    end
    if dy > 0.0 then
        local rr = math.floor((ny + box_h) / TILE)
        py = rr * TILE - box_h
        vy = 0.0
        on_ground = true
    else
        -- head bump: find the solid cell across the head span, prefer center
        local rr = math.floor(ny / TILE)
        local center_c = math.floor((px + box_w * 0.5) / TILE)
        local c0 = math.floor(px / TILE)
        local c1 = math.floor((px + box_w - 0.01) / TILE)
        local chosen = -1
        if solid_at(center_c, rr) then
            chosen = center_c
        else
            for c = c0, c1 do
                if solid_at(c, rr) then chosen = c break end
            end
        end
        py = (rr + 1) * TILE
        vy = 0.0
        if chosen >= 0 then
            hit_ok = true
            hit_col = chosen
            hit_row = rr
        end
    end
end

local function hurt_player()
    if invinc > 0.0 or mode ~= M_PLAY then return end
    if big == 1 then
        local feet = py + box_h
        big = 0
        box_w, box_h = 20.0, 28.0
        py = feet - box_h -- keep feet planted (any surface)
        invinc = 1.5
        audio_play("lose", 0.35)
        log_number(303000 + math.floor(box_h))
        if py + box_h == feet then log_number(303200) end
    else
        mode = M_DYING
        mtime = 0.0
        death_done = false
        vx = 0.0
        vy = -420.0
        audio_play("lose", 0.5)
        log_number(300010)
    end
end

-- ---------------------------------------------------------------------------
-- enemies / items
-- ---------------------------------------------------------------------------

local function body_move(eid, w, h, speed_factor)
    local x = ent_num(eid, S_X)
    local y = ent_num(eid, S_Y)
    local evx = ent_num(eid, S_VX)
    local nx = x + evx * speed_factor
    if box_hits_solid(nx, y, w, h) then
        local dir = ent_num(eid, S_DIR)
        dir = 0.0 - dir
        ent_set_num(eid, S_DIR, dir)
        ent_set_num(eid, S_VX, 0.0 - ent_num(eid, S_VX))
        if ent_tag(eid) == TAG_GOOMBA then
            log_number(320001)
            log_number(320100 + math.floor(x))
        end
    else
        ent_set_num(eid, S_X, nx)
        x = nx
    end
    local evy = ent_num(eid, S_VY) + GRAV * speed_factor
    if evy > MAX_FALL then evy = MAX_FALL end
    ent_set_num(eid, S_VY, evy)
    local ny = y + evy * speed_factor
    if box_hits_solid(x, ny, w, h) then
        local rr = math.floor((ny + h) / TILE)
        ent_set_num(eid, S_Y, rr * TILE - h)
        ent_set_num(eid, S_VY, 0.0)
        return true
    end
    ent_set_num(eid, S_Y, ny)
    return false
end

local function update_goombas(dt)
    local n = ent_count(TAG_GOOMBA)
    for i = 0, n - 1 do
        local id = ent_at(i, TAG_GOOMBA)
        if id ~= 0 then
            local st = ent_num(id, S_STATE)
            if st == GOOMBA_SQUASH then
                local t = ent_num(id, S_T) + dt
                ent_set_num(id, S_T, t)
                if t >= 0.5 then ent_destroy(id) end
            elseif st == GOOMBA_DEAD then
                local y = ent_num(id, S_Y)
                local evy = ent_num(id, S_VY) + GRAV * dt
                ent_set_num(id, S_VY, evy)
                ent_set_num(id, S_Y, y + evy * dt)
                if y > GROUND_TOP + 96.0 then ent_destroy(id) end
            else
                -- substep to match the player's integration granularity
                local evx = ent_num(id, S_VX)
                local ddx = evx * dt
                local ddy = ent_num(id, S_VY) * dt
                local steps = math.ceil(math.max(math.abs(ddx), math.abs(ddy)) / 8.0)
                if steps < 1 then steps = 1 end
                for _ = 1, steps do
                    body_move(id, 26.0, 26.0, dt / steps)
                end
                local y = ent_num(id, S_Y)
                local gxx = ent_num(id, S_X)
                if y > GROUND_TOP + 120.0 or gxx < cam_x - 80.0
                    or gxx > MAP_W + 80.0 then
                    ent_destroy(id)
                end
            end
        end
    end
end

local function update_mushrooms(dt)
    local n = ent_count(TAG_MUSH)
    for i = 0, n - 1 do
        local id = ent_at(i, TAG_MUSH)
        if id ~= 0 then
            local st = ent_num(id, S_STATE)
            if st == MUSH_EMERGE then
                local t = ent_num(id, S_T) + dt / 0.6
                if t >= 1.0 then t = 1.0 end
                ent_set_num(id, S_T, t)
                local base_y = ent_num(id, 7)
                ent_set_num(id, S_Y, base_y + (1.0 - t) * 32.0)
                if t >= 1.0 then ent_set_num(id, S_STATE, MUSH_WALK) end
            else
                body_move(id, 22.0, 22.0, dt)
                local y = ent_num(id, S_Y)
                local x = ent_num(id, S_X)
                if y > GROUND_TOP + 96.0 or x < cam_x - 100.0 or x > MAP_W + 100.0 then
                    ent_destroy(id)
                end
            end
        end
    end
end

local function overlaps(ax, ay, aw, ah, bx, by, bw, bh)
    return ax < bx + bw and ax + aw > bx and ay < by + bh and ay + ah > by
end

local function resolve_player_enemies(feet_before)
    if mode ~= M_PLAY or invinc > 0.0 then return end
    local n = ent_count(TAG_GOOMBA)
    for i = 0, n - 1 do
        local id = ent_at(i, TAG_GOOMBA)
        if id ~= 0 and ent_num(id, S_STATE) == GOOMBA_WALK then
            local gx = ent_num(id, S_X) + 1.0
            local gy = ent_num(id, S_Y) + 1.0
            if overlaps(px, py, box_w, box_h, gx, gy, 26.0, 26.0) then
                local stomp = vy > 1.0 and feet_before <= gy + 16.0
                if stomp then
                    ent_set_num(id, S_STATE, GOOMBA_SQUASH)
                    ent_set_num(id, S_T, 0.0)
                    add_score(100, gx, gy)
                    audio_play("paddle", 0.45)
                    if jump_hold > 0 then vy = -430.0 else vy = -260.0 end
                    py = gy - box_h
                    log_number(300001)
                else
                    hurt_player()
                    return
                end
            end
        end
    end
end

local function resolve_player_mushrooms()
    if mode ~= M_PLAY then return end
    local n = ent_count(TAG_MUSH)
    for i = 0, n - 1 do
        local id = ent_at(i, TAG_MUSH)
        if id ~= 0 and ent_num(id, S_STATE) == MUSH_WALK then
            local mx = ent_num(id, S_X)
            local my = ent_num(id, S_Y)
            if overlaps(px, py, box_w, box_h, mx, my, 22.0, 22.0) then
                add_score(1000, mx, my)
                audio_play("launch", 0.45)
                if big == 0 then
                    local feet = py + box_h
                    big = 1
                    box_w, box_h = 24.0, 44.0
                    py = feet - box_h
                    log_number(300002)
                    log_number(302000 + math.floor(box_h))
                    if py + box_h == feet then log_number(303100) end
                else
                    log_number(300014)
                end
                ent_destroy(id)
            end
        end
    end
end

local function collect_coins()
    local cx = math.floor((px + box_w * 0.5) / TILE)
    local cy = math.floor((py + box_h * 0.5) / TILE)
    for r = cy - 2, cy + 2 do
        for c = cx - 2, cx + 2 do
            if map_tile(map_id, c, r) == G_COIN then
                map_set_tile(map_id, c, r, 0)
                add_coin(c * TILE + 8.0, r * TILE + 8.0)
                log_number(300009)
            end
        end
    end
end

-- ---------------------------------------------------------------------------
-- win / death progression
-- ---------------------------------------------------------------------------

local function touches_pole()
    local cc = math.floor((px + box_w * 0.5) / TILE)
    local r0 = math.floor(py / TILE)
    local r1 = math.floor((py + box_h) / TILE)
    for r = r0, r1 do
        local g = map_tile(map_id, cc, r)
        if g == G_POLE or g == G_POLEBALL then return true end
    end
    if cc + 1 == POLE_COL or cc == POLE_COL then
        for r = r0, r1 do
            if map_tile(map_id, POLE_COL, r) == G_POLE then return true end
        end
    end
    return false
end

local function start_win()
    mode = M_SLIDE
    mtime = 0.0
    facing = 1
    vx = 0.0
    px = POLE_COL * TILE - box_w - 3.0
    audio_play("win", 0.5)
    -- Phase 8 SaveStore: bank the run's score and the time left on the clock.
    -- time_left is still counting down here, so it is the bonus that decides
    -- whether this run beat the record.
    new_record = false
    local final_score = score + win_bonus
    if final_score > best_score then
        best_score = final_score
        new_record = true
    end
    if time_left > best_time then best_time = time_left end
    save_records()
    log_number(300005)
    log_number(300020)
    log_number(300030 + best_score)
end

local function finish_death()
    lives = lives - 1
    if test_die3 == 1 then die3_left = die3_left - 1 end
    log_number(300004)
    log_number(301000 + lives)
    if lives <= 0 then
        mode = M_GAMEOVER
        mtime = 0.0
    else
        reset_level()
    end
end

-- ---------------------------------------------------------------------------
-- input / AI
-- ---------------------------------------------------------------------------

-- Row the player's feet occupy while standing (ground surface is row+1).
local function ai_stand_row()
    return math.floor((py + box_h - 0.01) / TILE)
end

-- A column is a deadly pit when it has no solid ground in the 4 rows below
-- the standing row. A 1-tile drop (pipe top -> ground, 96px) stays solid in
-- row +3/+4, so ledges are not mistaken for gaps.
local function ai_is_pit(cc, sr)
    for r = sr + 1, sr + 4 do
        if solid_at(cc, r) then return false end
    end
    return true
end

-- First wall intersecting the player's own body row ahead of the toes:
-- returns distance (px) and height in tiles (only body-row solids count, so
-- overhead ?/brick rows are correctly ignored while running underneath).
-- `is_slope` (not `solid_at`) is what the walkable-surface helpers use, so a
-- ramp never registers as a wall and the attract AI keeps running into the
-- hill instead of jumping it. Treating a slope as a one-cell ledge makes the
-- planner hop onto the near side and walk up.
local function ai_wall_ahead(toes, sr)
    local base = math.floor(toes / TILE) + 1
    for dc = 0, 5 do
        local cc = base + dc
        if solid_at(cc, sr) or is_slope(cc, sr) then
            local h = 1
            for up = 1, 5 do
                if solid_at(cc, sr - up) then h = h + 1 else break end
            end
            return cc * TILE - toes, h
        end
    end
    return 9999.0, 0
end

-- First pit within the next 6 columns (look-ahead, not just the column at
-- the toes): distance to its left edge and width in tiles. A negative
-- distance means the toes already hang over the gap (coyote emergency).
local function ai_pit_ahead(toes, sr)
    local base = math.floor(toes / TILE)
    for dc = 0, 6 do
        local cc = base + dc
        if ai_is_pit(cc, sr) then
            local w = 0
            local scan = cc
            while w < 6 and ai_is_pit(scan, sr) do
                scan = scan + 1
                w = w + 1
            end
            return cc * TILE - toes, w
        end
    end
    return 9999.0, 0
end

-- Nearest walking goomba strictly ahead of the player center.
local function ai_goomba_gap(cx)
    local best = 9999.0
    local k = ent_count(TAG_GOOMBA)
    for i = 0, k - 1 do
        local id = ent_at(i, TAG_GOOMBA)
        if id ~= 0 and ent_num(id, S_STATE) == GOOMBA_WALK then
            local d = ent_num(id, S_X) + 1.0 - cx
            if d > 0.0 and d < best then best = d end
        end
    end
    return best
end

local function build_intent(dt)
    local move = 0.0
    local pressed = false
    local held = false
    local ai_active = false
    if input_key_down("left") or input_key_down("a") then move = -1.0 end
    if input_key_down("right") or input_key_down("d") then move = 1.0 end

    local key_jump = input_key_pressed("space") or input_key_pressed("w")
        or input_key_pressed("up")
    local real_held = input_key_down("space") or input_key_down("w")
        or input_key_down("up")
    if move ~= 0.0 or key_jump or real_held then human_seen = 1 end

    if test_jump == 1 then key_jump = true end
    local key_held = real_held
    if test_jump == 1 then key_held = true end
    local running = input_key_down("shift")
    if test_right == 1 then
        move = 1.0
        running = true
    end

    -- attract AI when there is no human horizontal input. The scripted TP
    -- hook disables it so deterministic fixtures can hold their position.
    --
    -- The planner only commits to jumps while grounded: it classifies the
    -- nearest threat (wall height / pit width / goomba gap) and jumps inside
    -- a precomputed lead window for a full-height RUN takeoff (apex ~130px,
    -- ~196px horizontal arc). Airborne frames never queue jumps, so there is
    -- no buffered re-hop when landing. All timing is in seconds so the
    -- behaviour is identical at 60fps and under the headless fast clock.
    if move == 0.0 and test_tp < 0.0 then
        ai_active = true
        move = 1.0
        running = true
        local cx = px + box_w * 0.5
        local toes = px + box_w
        if ai_cd_t > 0.0 then ai_cd_t = ai_cd_t - dt end

        if on_ground and ai_cd_t <= 0.0 then
            local sr = ai_stand_row()
            local jump = false
            local why = 0 + 0

            -- Terrain first: wall tiers and pits own their takeoff windows,
            -- otherwise a goomba hop in front of a pipe/pit steals the jump.
            local wall_d, wall_h = ai_wall_ahead(toes, sr)
            if wall_h >= 4 then
                -- 128px: only a near-apex arc clears it; land on the lip
                if wall_d >= 90.0 and wall_d <= 116.0 then jump = true; why = 4 end
            elseif wall_h == 3 then
                if wall_d >= 74.0 and wall_d <= 108.0 then jump = true; why = 3 end
            elseif wall_h == 2 then
                if wall_d >= 54.0 and wall_d <= 96.0 then jump = true; why = 2 end
            elseif wall_h == 1 then
                if wall_d >= 22.0 and wall_d <= 64.0 then jump = true; why = 1 end
            end

            local pit_d, pit_w = ai_pit_ahead(toes, sr)
            if not jump and pit_w >= 3 then
                if pit_d >= 74.0 and pit_d <= 112.0 then jump = true; why = 13 end
            elseif not jump and pit_w >= 1 then
                if pit_d >= 48.0 and pit_d <= 104.0 then jump = true; why = 12 end
            end
            -- coyote emergency: toes already at/over a pit lip
            if not jump and pit_w >= 1 and pit_d < 54.0 then
                jump = true; why = 14
            end

            -- Goombas only when the ~210px landing arc is free of terrain;
            -- a panic hop at arm's length is always allowed (side contact
            -- otherwise costs a life in small form).
            local g_d = ai_goomba_gap(cx)
            if not jump and g_d <= 92.0 then
                jump = true; why = 22
            elseif not jump and g_d >= 120.0 and g_d <= 208.0
                and wall_d > 215.0 and pit_d > 215.0 then
                jump = true; why = 21
            end

            if jump then
                key_jump = true
                ai_cd_t = 0.25
                log_number(310000 + why)
            end
        end
    end

    -- human latch: a tap tracks 12 frames then follows the real button, so
    -- per-frame flicker cannot end a hop early; releases within the latch
    -- still allow the variable-height cut.
    if key_jump and not ai_active then jump_hold = 12 end
    if jump_hold > 0 then
        if key_held then held = true end
        jump_hold = jump_hold - 1
    else
        held = key_held
    end
    -- AI jumps: hold through the whole ascent for full-height arcs; release
    -- at apex so a descent-side contact never restores the cut. The launch
    -- frame must count as held: intent is built while vy is still the old
    -- value (0), but the jump is launched later in the same step, so without
    -- key_jump the variable-height cut would immediately clip every AI jump
    -- to 42% launch speed (~23px, unable to clear pipes).
    if ai_active then held = vy < 0.0 or key_jump end
    return move, running, pressed or key_jump, held
end

-- ---------------------------------------------------------------------------
-- per-frame update
-- ---------------------------------------------------------------------------

local function step_player(dt, intent_x, running, jump_pressed, jumbox_held)
    local target = running and RUN_SPEED or WALK_SPEED
    if intent_x > 0.0 then
        facing = 1
        local rate = on_ground and G_ACC or A_ACC
        if vx < target then vx = math.min(target, vx + rate * dt) end
    elseif intent_x < 0.0 then
        facing = -1
        local rate = on_ground and G_ACC or A_ACC
        if vx > -target then vx = math.max(-target, vx - rate * dt) end
    elseif on_ground then
        if vx > 0 then vx = math.max(0.0, vx - FRICTION * dt) end
        if vx < 0 then vx = math.min(0.0, vx + FRICTION * dt) end
    end

    if jump_pressed then buffer_t = JUMP_BUFFER end
    if on_ground then coyote_t = COYOTE end

    vx = vx + 0.0 -- no platform forces
    vy = math.min(MAX_FALL, vy + GRAV * dt)

    local ddx = vx * dt
    local ddy = vy * dt
    local n = math.ceil(math.max(math.abs(ddx), math.abs(ddy)) / 8.0)
    if n < 1 then n = 1 end
    local sx = ddx / n
    local sy = ddy / n
    hit_ok = false
    local feet_before = py + box_h
    local landed = false
    for _ = 1, n do
        move_x_player(sx)
        on_ground = false
        move_y_player(sy)
        if vy == 0.0 and sy > 0.0 then landed = true end
        if hit_ok then
            handle_block(hit_col, hit_row)
            hit_ok = false
        end
    end

    -- coyote + buffered jump (negative y = up)
    local jump_launched = false
    if buffer_t > 0.0 and coyote_t > 0.0 and not cut_used then
        vy = 0.0 - JUMP_V
        on_ground = false
        coyote_t = 0.0
        buffer_t = 0.0
        cut_used = true
        jump_launched = true
        audio_play("jump", 0.3)
        log_number(312000 + math.floor(px))
    end
    -- variable jump height: releasing the button while ascending cuts rise.
    -- The launch frame itself is exempt: a buffered/coyote jump fires after
    -- intent was built (held may reflect the pre-jump state, e.g. AI while
    -- falling), so cutting on that same frame would shrink every buffered
    -- jump to 42% launch speed.
    if not jump_launched and not jumbox_held and vy < 0.0 and cut_used then
        vy = vy * JUMP_CUT
        cut_used = false
    end

    -- ground probe; landing re-arms the jump cut
    if vy >= 0.0 and box_hits_solid(px, py + 2.0, box_w, box_h) then
        on_ground = true
        cut_used = false
    end

    -- Phase 8: follow the slope surface. On the ramp the ground probe above
    -- finds nothing (a slope cell is not solid), so this is what keeps the
    -- player attached to the incline instead of sliding off it.
    snap_to_slope()

    -- SMB edge rule: the player cannot walk left past the camera's left edge
    local left_bound = cam_x
    if px < left_bound then px = left_bound end

    resolve_player_enemies(feet_before)
    resolve_player_mushrooms()
    collect_coins()

    if py > GROUND_TOP + 96.0 and mode == M_PLAY then
        mode = M_DYING
        mtime = 0.0
        death_done = false
        vy = -200.0
        log_number(300011)
    end
end

local function update_bumps(dt)
    local n = ent_count(TAG_BUMP)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_BUMP)
        if id == 0 then break end
        local t = ent_num(id, S_T) - dt
        if t <= 0.0 then
            ent_destroy(id)
            n = ent_count(TAG_BUMP)
        else
            ent_set_num(id, S_T, t)
            i = i + 1
        end
    end
end

local function update_popups(dt)
    local n = ent_count(TAG_POPUP)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_POPUP)
        if id == 0 then break end
        local t = ent_num(id, S_T) - dt
        if t <= 0.0 then
            ent_destroy(id)
            n = ent_count(TAG_POPUP)
        else
            ent_set_num(id, S_T, t)
            local yy = ent_num(id, S_Y) - 26.0 * dt
            ent_set_num(id, S_Y, yy)
            i = i + 1
        end
    end
end

-- ---------------------------------------------------------------------------
-- rendering
-- ---------------------------------------------------------------------------

local function bump_offset(c, r)
    local n = ent_count(TAG_BUMP)
    for i = 0, n - 1 do
        local id = ent_at(i, TAG_BUMP)
        if id ~= 0 and ent_num(id, 0) == c and ent_num(id, 1) == r then
            local t = ent_num(id, S_T)
            local phase = 1.0 - t / 0.25
            return 0.0 - math.sin(phase * 3.14159) * 10.0
        end
    end
    return 0.0
end

local function draw_world()
    draw_quad(cam_x, cam_y, cam_vw, cam_vh, 0.36, 0.58, 0.95, 1.0)
    map_draw(map_id)

    -- Phase 8 per-draw custom shader: a shimmer band travels down the goal
    -- pole. draw_use_shader switches the batcher onto slot 1 for the calls
    -- that follow and 0 restores the default program, so only these quads pay
    -- for the extra pass. The pole is re-drawn here (it is part of the tilemap
    -- above) with the shader applied, giving a visible highlight on approach.
    if fx_flag_shader > 0 and mode ~= M_CLEAR then
        shader_set_float(fx_flag_shader, "u_time", time_frame())
        draw_use_shader(fx_flag_shader)
        local pole_x = POLE_COL * TILE
        -- the pole shaft spans rows 4..12, with the ball on row 3
        draw_quad(pole_x + 11.0, 4 * TILE, 6.0, 9 * TILE, 1.0, 1.0, 1.0, 0.55)
        draw_quad(pole_x + 6.0, 3 * TILE, 16.0, 16.0, 1.0, 1.0, 1.0, 0.75)
        draw_use_shader(0)
    end

    -- block bump re-draw (the original tile has already changed for ?, so
    -- draw the current tile sprite at the bumped offset)
    local bn = ent_count(TAG_BUMP)
    for i = 0, bn - 1 do
        local id = ent_at(i, TAG_BUMP)
        if id ~= 0 then
            local c = ent_num(id, 0)
            local r = ent_num(id, 1)
            local g = map_tile(map_id, c, r)
            if g > 0 then
                local ox = bump_offset(c, r)
                local col0 = imod(g - 1.0, 8.0)
                local row0 = math.floor((g - 1) / 8)
                -- hoist all arithmetic out of the native call's argument
                -- slots (FakeLua JIT slow-path tail-arg pitfall)
                local sx0 = col0 * TILE
                local sy0 = row0 * TILE
                local dx0 = c * TILE
                local dy0 = r * TILE + ox
                sprite_draw_region(tex_sheet, sx0, sy0, TILE, TILE,
                                   dx0, dy0, TILE, TILE)
            end
        end
    end

    -- mushrooms
    local mn = ent_count(TAG_MUSH)
    for i = 0, mn - 1 do
        local id = ent_at(i, TAG_MUSH)
        if id ~= 0 then
            sprite_draw(tex_mush, ent_num(id, S_X), ent_num(id, S_Y), 24, 24)
        end
    end

    -- goombas
    local gn = ent_count(TAG_GOOMBA)
    for i = 0, gn - 1 do
        local id = ent_at(i, TAG_GOOMBA)
        if id ~= 0 then
            local st = ent_num(id, S_STATE)
            local gx = ent_num(id, S_X)
            local gy = ent_num(id, S_Y)
            if st == GOOMBA_SQUASH then
                sprite_draw(tex_goomba, gx - 1.0, gy + 16.0, 28, 12)
            else
                sprite_draw(tex_goomba, gx, gy, 28, 28)
            end
        end
    end

    -- player (blinks while invincible)
    local blink = invinc <= 0.0 or imod(math.floor(invinc * 12.0), 2.0) == 0.0
    if blink and mode ~= M_GAMEOVER then
        local tex = tex_ps
        if big == 1 then tex = tex_pb end
        sprite_draw(tex, px, py, box_w, box_h)
    end

    -- score popups
    local pn = ent_count(TAG_POPUP)
    for i = 0, pn - 1 do
        local id = ent_at(i, TAG_POPUP)
        if id ~= 0 then
            local txt = "+" .. istr(ent_num(id, S_VX))
            draw_text(txt, ent_num(id, S_X), ent_num(id, S_Y), 0.5,
                      1.0, 1.0, 0.7, 1.0)
        end
    end
end

local function draw_text_str(s, x, y, scale, r, g, b)
    draw_text(s, x, y, scale, r, g, b, 1.0)
end

local function draw_hud()
    local sx = cam_x
    local sy = cam_y
    draw_text_str("SCORE " .. istr(score), sx + 16.0, sy + 12.0, 0.6,
                  1.0, 1.0, 1.0)
    draw_text_str("COINS x" .. istr(coins), sx + 250.0, sy + 12.0, 0.6,
                  1.0, 1.0, 1.0)
    draw_text_str("WORLD 1-1", sx + 470.0, sy + 12.0, 0.6, 1.0, 1.0, 1.0)
    draw_text_str("TIME " .. istr(time_left), sx + 670.0, sy + 12.0, 0.6,
                  1.0, 1.0, 1.0)
    draw_text_str("LIVES x" .. istr(lives), sx + 830.0, sy + 12.0, 0.6,
                  1.0, 1.0, 1.0)
    -- Phase 8 SaveStore: the record banked on the previous run.
    if best_score > 0 then
        draw_text_str("BEST " .. istr(best_score), sx + 16.0, sy + 34.0, 0.45,
                      1.0, 0.92, 0.55)
    end
end

local function draw_banner(title, sub)
    local bx = cam_x + 180.0
    local by = cam_y + 180.0
    draw_quad(bx, by, 600.0, 150.0, 0.0, 0.0, 0.0, 0.62)
    local tx = cam_x + 330.0
    local ty = cam_y + 215.0
    draw_text_str(title, tx, ty, 1.4, 1.0, 1.0, 0.6)
    local sx2 = cam_x + 270.0
    local sy2 = cam_y + 275.0
    draw_text_str(sub, sx2, sy2, 0.7, 1.0, 1.0, 1.0)
end

-- ---------------------------------------------------------------------------
-- scripted headless selftests (PHYS / BLOCKS)
-- ---------------------------------------------------------------------------

-- PHYS: measure walk plateau, full-hold jump apex and run plateau using the
-- real player controller, then log 820xxx / 821xxx / 822xxx markers. The
-- whole script stays left of the first pit (x 512): walk to ~200, hop lands
-- ~340, run plateau is reached by ~400.
local function run_phys_mode(sdt)
    local mv = 0.0
    local run = false
    local jp = false
    local held = false
    if phys_phase == 0 then
        -- walk plateau
        mv = 1.0
        if vx >= 149.0 then
            log_number(820000 + math.floor(vx))
            phys_phase = 1
        end
    elseif phys_phase == 1 then
        -- jump: press on entry, hold until back on the ground
        if jump_top > 9000.0 then jp = true end
        held = true
    elseif phys_phase == 2 then
        -- run plateau, immediately after landing (no idle friction gap)
        mv = 1.0
        run = true
        if vx >= 234.0 then
            log_number(822000 + math.floor(vx))
            phys_phase = 3
        end
    end
    step_player(sdt, mv, run, jp, held)
    if vy < 0.0 and py < jump_top then jump_top = py end
    if phys_phase == 1 and jump_top < 9000.0 and on_ground then
        local hgt = GROUND_TOP - (jump_top + box_h)
        log_number(821000 + math.floor(hgt))
        jump_top = 9999.0
        phys_phase = 2
    end
    update_goombas(sdt)
    update_mushrooms(sdt)
end

-- BLOCKS: deterministic block/item state machine. Hops go straight up so
-- the head hits exactly one chosen cell, except phase 3 which run-jumps the
-- first pit (cols 16-17). blk_flag marks "in position"/"airborne". Start
-- px=440: the small box's head span [440,460) covers col14 while landing
-- stays left of the pit.
local function run_blocks_mode(sdt)
    blk_t = blk_t + sdt
    if blk_phase == 0 then
        blk_phase = 1
        blk_t = 0.0
        px = 440.0
        py = GROUND_TOP - box_h
        vx, vy = 0.0, 0.0
        on_ground = true
        cam_x = 0.0
    end
    local mv = 0.0
    local run = false
    local jp = false
    local held = false
    if blk_phase == 1 then
        -- first hop: lone ?(coin) at (14,9)
        if blk_t < 0.1 then jp = true end
        held = blk_t < 0.55
        if on_ground and blk_t > 0.55 then
            if map_tile(map_id, 14, 9) == G_USED then log_number(830001) end
            blk_phase = 2 blk_t = 0.0
        end
    elseif blk_phase == 2 then
        -- vertical hop: used block must not retrigger
        if blk_t < 0.1 then jp = true end
        held = blk_t < 0.55
        if on_ground and blk_t > 0.55 then
            if coins == 1 then log_number(830002) end
            blk_phase = 3 blk_t = 0.0
        end
    elseif blk_phase == 3 then
        -- Cross the first pit (cols 16-17) via the used ? block at (14,9).
        -- Physics-precision platforming is covered by the PHYS selftest;
        -- riding a single 32px perch is a frame-rate dependent pixel chase
        -- (the safe takeoff window shrinks to <1px at the headless clock),
        -- so this fixture places the player on the block lip and launches a
        -- fixed full RUN arc (~196px, lands near 648 past the 576 far lip).
        -- blk_flag: 0 = place on perch, 3 = hop launch, 4 = airborne.
        if blk_flag == 0 then
            px = 452.0
            py = 288.0 - box_h
            vx = RUN_SPEED
            vy = 0.0
            on_ground = true
            blk_flag = 3
            blk_t = 0.0
        end
        mv = 1.0 run = true
        if blk_flag == 3 then jp = true end
        held = true
    elseif blk_phase == 4 then
        -- brick at (20,9), small bump: deterministic vertical hop from a
        -- snapped spot (small 20px box centered in col 20). Frame-rate
        -- independent; PHYS covers the movement model itself.
        if blk_flag == 0 then
            px = 650.0
            py = GROUND_TOP - box_h
            vx, vy = 0.0, 0.0
            on_ground = true
            blk_flag = 1 blk_t = 0.0
        end
        if blk_flag == 1 then jp = true end
        held = true
        if blk_flag == 2 and on_ground and blk_t > 1.0 then
            blk_phase = 5 blk_t = 0.0 blk_flag = 0
        end
    elseif blk_phase == 5 then
        -- question at (21,9): mushroom block, vertical hop (col 21 center)
        if blk_flag == 0 then
            px = 682.0
            py = GROUND_TOP - box_h
            vx, vy = 0.0, 0.0
            on_ground = true
            blk_flag = 1 blk_t = 0.0
        end
        if blk_flag == 1 then jp = true end
        held = true
        if blk_flag == 2 and on_ground and blk_t > 1.0 then
            if ent_count(TAG_MUSH) == 1 then log_number(830003) end
            blk_phase = 6 blk_t = 0.0 blk_flag = 0
        end
    elseif blk_phase == 6 then
        -- Deterministic pickup: place the walking mushroom once on the ground
        -- just ahead of the player, then walk into it (repositioning it
        -- every frame would keep it perpetually out of reach). Validates the
        -- grow transition + 1000 score.
        if big == 1 then
            log_number(830004)
            blk_phase = 7 blk_t = 0.0 blk_flag = 0
        elseif blk_t > 4.0 then
            blk_phase = 7 blk_t = 0.0 blk_flag = 0
        else
            if blk_flag == 0 then
                blk_flag = 1
                local mn = ent_count(TAG_MUSH)
                for i = 0, mn - 1 do
                    local id = ent_at(i, TAG_MUSH)
                    if id ~= 0 then
                        ent_set_num(id, S_STATE, MUSH_WALK)
                        ent_set_num(id, S_X, px + 28.0)
                        ent_set_num(id, S_Y, GROUND_TOP - 24.0)
                        ent_set_num(id, S_VX, 0.0)
                        ent_set_num(id, S_VY, 0.0)
                    end
                end
            end
            mv = 1.0
        end
    elseif blk_phase == 7 then
        -- place a goomba atop (23,9) and hold it still for 0.5s: a patrolling
        -- one walks off to col 22 before the bonk hop reaches its band.
        if blk_flag == 0 then
            blk_flag = 1
            local gid = spawn_goomba(23, 8)
            ent_set_num(gid, S_VX, 0.0)
            ent_set_num(gid, S_DIR, 0.0)
        end
        if blk_t > 0.5 then
            blk_phase = 8 blk_t = 0.0 blk_flag = 0
        end
    elseif blk_phase == 8 then
        -- question (23,9): coin + goomba killed via bonk band; big 24px box
        -- centered in col 23, straight vertical hop.
        if blk_flag == 0 then
            px = 740.0
            py = GROUND_TOP - box_h
            vx, vy = 0.0, 0.0
            on_ground = true
            blk_flag = 1 blk_t = 0.0
        end
        if blk_flag == 1 then jp = true end
        held = true
        if blk_flag == 2 and on_ground and blk_t > 1.0 then
            if bonk_kill_n >= 1 then log_number(830007) end
            blk_phase = 9 blk_t = 0.0 blk_flag = 0
        end
    elseif blk_phase == 9 then
        -- brick (22,9): big form breaks it (col 22 center, vertical hop)
        if blk_flag == 0 then
            px = 708.0
            py = GROUND_TOP - box_h
            vx, vy = 0.0, 0.0
            on_ground = true
            blk_flag = 1 blk_t = 0.0
        end
        if blk_flag == 1 then jp = true end
        held = true
        if blk_flag == 2 and on_ground and blk_t > 1.0 then
            if map_tile(map_id, 22, 9) == 0 then log_number(830005) end
            blk_phase = 10 blk_t = 0.0 blk_flag = 0
        end
    elseif blk_phase == 10 then
        log_number(830006 + math.floor(max_bump * 10.0))
        blk_phase = 11
    end
    step_player(sdt, mv, run, jp, held)
    -- phase 3 perch-hop transition (state is only valid post-step)
    if blk_phase == 3 then
        if blk_flag == 3 and vy < 0.0 then blk_flag = 4 end
        if blk_flag == 4 and on_ground and py + box_h > 400.0 then
            blk_phase = 4 blk_t = 0.0 blk_flag = 0
        end
    end
    -- vertical-hop phases 4/5/8/9: arm(1) -> airborne(2) on launch
    if (blk_phase == 4 or blk_phase == 5 or blk_phase == 8 or blk_phase == 9)
        and blk_flag == 1 and vy < 0.0 then
        blk_flag = 2
    end
    update_goombas(sdt)
    update_mushrooms(sdt)
    -- clean per-frame timeline for diagnosis
    log_number(360000 + blk_phase)
    log_number(361000 + math.floor(px))
    log_number(362000 + math.floor(py))
    log_number(363000 + math.floor(vy))
    log_number(364000 + blk_flag)
    local bn = ent_count(TAG_BUMP)
    for i = 0, bn - 1 do
        local id = ent_at(i, TAG_BUMP)
        if id ~= 0 then
            local c = ent_num(id, 0)
            local r = ent_num(id, 1)
            local ox = bump_offset(c, r)
            if 0.0 - ox > max_bump then max_bump = 0.0 - ox end
        end
    end
end

-- ---------------------------------------------------------------------------
-- entry
-- ---------------------------------------------------------------------------

function update(dt)
    frame_n = frame_n + 1
    if imod(frame_n, 100.0) == 0.0 then log_number(400000 + frame_n) end

    -- clamp the gameplay step so physics/AI are stable even when the host
    -- frame clock stalls (headless) or spikes (slow machines)
    local sdt = dt
    if sdt > 0.034 then sdt = 0.034 end

    if not inited then
        inited = true
        local td = os.getenv("MARIO_TEST_DEATH")
        local tw = os.getenv("MARIO_TEST_WIN")
        if td ~= nil then test_death = tonumber(td) + 0 end
        if tw ~= nil then test_win = tonumber(tw) + 0 end
        if os.getenv("MARIO_TEST_BIG") ~= nil then test_big = 1 end
        if os.getenv("MARIO_TEST_RIGHT") ~= nil then test_right = 1 end
        if os.getenv("MARIO_TEST_JUMP") ~= nil then test_jump = 1 end
        local ttp = os.getenv("MARIO_TEST_TP")
        if ttp ~= nil then test_tp = tonumber(ttp) + 0.0 end
        if os.getenv("MARIO_TEST_DIE3") ~= nil then
            test_die3 = 1
            die3_left = 3
        end
        if os.getenv("MARIO_TEST_MUSH") ~= nil then test_mush = 1 end
        if os.getenv("MARIO_TEST_PHYS") ~= nil then test_phys = 1 end
        if os.getenv("MARIO_TEST_BLOCKS") ~= nil then test_blocks = 1 end

        map_id = map_load("assets/mario.json")
        tex_sheet = sprite_load("assets/m_tiles.png")
        tex_goomba = sprite_load("assets/m_goomba.png")
        tex_mush = sprite_load("assets/m_mushroom.png")
        tex_ps = sprite_load("assets/m_player_s.png")
        tex_pb = sprite_load("assets/m_player_b.png")
        -- Phase 8: custom GLSL slot for the flag shimmer, plus the saved
        -- high score from the previous run.
        fx_flag_shader = shader_load("assets/flag_shimmer.vert",
                                     "assets/flag_shimmer.frag")
        shader_set_float(fx_flag_shader, "u_speed", 0.35)
        shader_set_float(fx_flag_shader, "u_width", 0.14)
        shader_set_vec4(fx_flag_shader, "u_tint", 1.0, 0.96, 0.78, 1.0)
        load_records()
        fx_debris = part_create()
        part_set_lifetime(fx_debris, 0.35, 0.7)
        part_set_speed(fx_debris, 60.0, 200.0)
        part_set_direction(fx_debris, -2.4, 0.6)
        part_set_gravity(fx_debris, 0.0, 900.0)
        part_set_size(fx_debris, 4, 0)
        part_set_spin(fx_debris, 10)

        -- record destructible tiles once
        for r = 0, 14 do
            for c = 0, 111 do
                local g = map_tile(map_id, c, r)
                if g == G_BRICK or g == G_QUESTION or g == G_COIN
                    or g == G_GOOMBA_MARK then
                    local rec = ent_create(TAG_RESTORE)
                    ent_set_num(rec, 0, c)
                    ent_set_num(rec, 1, r)
                    ent_set_num(rec, 2, g)
                end
            end
        end
        full_reset()
        -- DIE3 begins with a nonzero score; the GAME OVER reopen must zero it
        if test_die3 == 1 then score = 999 end
        log_number(900000 + map_cols(map_id))
    end

    mtime = mtime + sdt
    if invinc > 0.0 then invinc = math.max(0.0, invinc - sdt) end
    if buffer_t > 0.0 then buffer_t = math.max(0.0, buffer_t - sdt) end
    if coyote_t > 0.0 then coyote_t = math.max(0.0, coyote_t - sdt) end

    if mode == M_PLAY then
        -- timer
        time_acc = time_acc + sdt
        while time_acc >= 1.0 do
            time_acc = time_acc - 1.0
            time_left = time_left - 1
        end
        if time_left <= 0 then
            mode = M_DYING
            mtime = 0.0
            death_done = false
            vy = -200.0
            audio_play("lose", 0.5)
            log_number(300012)
        elseif test_phys == 1 then
            run_phys_mode(sdt)
        elseif test_blocks == 1 then
            run_blocks_mode(sdt)
        else
            local intent_x, running, jp, jh = build_intent(sdt)
            step_player(sdt, intent_x, running, jp, jh)
            update_goombas(sdt)
            update_mushrooms(sdt)
            if touches_pole() then start_win() end
        end
    elseif mode == M_DYING then
        vy = vy + GRAV * sdt
        py = py + vy * sdt
        mtime = mtime + sdt
        if py > GROUND_TOP + 200.0 and not death_done then
            death_done = true
            finish_death()
        end
    elseif mode == M_SLIDE then
        if py + box_h < GROUND_TOP then py = py + SLIDE_SPEED * sdt end
        if py + box_h >= GROUND_TOP then
            py = GROUND_TOP - box_h
            mode = M_WALK
            log_number(300021)
        end
    elseif mode == M_WALK then
        px = px + WALK_OUT_SPEED * sdt
        if px >= CASTLE_X then
            mode = M_CLEAR
            mtime = 0.0
            local bonus = time_left * 10
            win_bonus = bonus
            score = score + bonus
            audio_play("win", 0.5)
            log_number(300022)
            log_number(300023 + bonus)
        end
    elseif mode == M_GAMEOVER then
        if input_key_pressed("space") or input_key_pressed("w")
            or input_key_pressed("up") or test_jump == 1 then
            full_reset()
        elseif human_seen == 0 and mtime > 4.5 then
            -- unattended attract: loop forever
            full_reset()
            log_number(300030)
        end
    elseif mode == M_CLEAR then
        if input_key_pressed("space") or input_key_pressed("w")
            or input_key_pressed("up") then
            reset_level()
        elseif human_seen == 0 and mtime > 4.5 then
            reset_level()
            log_number(300031)
        end
    end

    -- test hooks (forced transitions)
    if test_death >= 0 and frame_n == test_death and mode == M_PLAY then
        mode = M_DYING
        mtime = 0.0
        death_done = false
        vy = -200.0
        py = GROUND_TOP + 40.0
    end
    if test_win >= 0 and frame_n == test_win and mode == M_PLAY then
        start_win()
    end

    -- DIE3: force one death per fresh level until three lives are spent,
    -- then the GAME OVER reopen (MARIO_TEST_JUMP) must restore everything.
    if test_die3 == 1 and die3_left > 0 and mode == M_PLAY then
        mode = M_DYING
        mtime = 0.0
        death_done = false
        vy = -200.0
        py = GROUND_TOP + 40.0
    end
    if test_die3 == 1 and die3_left == 0 and mode == M_PLAY
        and die3_checked == 0 and frame_n > 30 then
        if lives == 3 and score == 0 and time_left == 300
            and ent_count(TAG_GOOMBA) == 7
            and map_tile(map_id, 14, 9) == G_QUESTION
            and map_tile(map_id, 21, 9) == G_QUESTION then
            die3_checked = 1
            log_number(840000)
        end
    end

    update_bumps(sdt)
    update_popups(sdt)

    -- camera follows, clamped to the level
    cam_vw = camera_viewport_w()
    cam_vh = camera_viewport_h()
    local want = px + box_w * 0.5 - cam_vw * 0.55
    if want > cam_x then cam_x = want end
    local max_cam = MAP_W - cam_vw
    local cam_hi = math.max(0.0, max_cam)
    cam_x = clampf(cam_x, 0.0, cam_hi)
    camera_set_position(cam_x, 0.0)

    draw_world()
    draw_hud()

    if frame_n < 200 and mode == M_PLAY then
        local hx = cam_x + 150.0
        local hy = cam_y + 500.0
        draw_text_str("ARROWS/A/D MOVE   SHIFT RUN   SPACE JUMP",
                      hx, hy, 0.6, 1.0, 1.0, 1.0)
    end

    if mode == M_GAMEOVER then
        draw_banner("GAME OVER", "PRESS SPACE TO RETRY")
    elseif mode == M_CLEAR then
        draw_banner("LEVEL CLEAR!", "SCORE " .. istr(score)
            .. "   TIME BONUS " .. istr(win_bonus)
            .. "   PRESS SPACE")
    end

    -- debug markers for headless verification
    if frame_n >= 599 and frame_n <= 601 then log_number(410000 + frame_n) end
    if frame_n == 90 or frame_n == 200 or frame_n == 300 or frame_n == 400
        or frame_n == 600 or frame_n == 900 or frame_n == 1200 or frame_n == 1500 then
        log_number(200000 + frame_n)
        log_number(201000 + mode)
        log_number(202000 + lives)
        log_number(203000 + score)
        log_number(204000 + math.floor(px))
        log_number(205000 + ent_count(TAG_GOOMBA))
        log_number(206000 + math.floor(vx))
    end

    return 0
end
