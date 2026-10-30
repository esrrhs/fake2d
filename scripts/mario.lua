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
-- Controls: Left/Right or A/D move, Shift run, Space/W/Up jump, P toggles the
--   attract AI.
-- The attract AI (runs right on its own and hops walls/pits/goombas) is OFF
-- by default so a human keeps full control of the controller. Press P to
-- switch it back on for an unattended attract run. Headless runs that want
-- the AI drive it with MARIO_TEST_AI=1.
-- Debug hooks (test only): MARIO_TEST_DEATH=<frame>, MARIO_TEST_BIG=1,
--   MARIO_TEST_WIN=<frame>, MARIO_TEST_RIGHT=1, MARIO_TEST_JUMP=1,
--   MARIO_TEST_DIE3=1, MARIO_TEST_TP=<px>, MARIO_TEST_MUSH=1,
--   MARIO_TEST_PHYS=1, MARIO_TEST_BLOCKS=1, MARIO_TEST_AI=1.

-- gids (must match the host-generated assets/mario.json)
local G_GROUND, G_DIRT, G_BRICK, G_QUESTION, G_USED = 1, 2, 3, 4, 5
local G_PIPE_TL, G_PIPE_TR, G_PIPE_BL, G_PIPE_BR, G_COIN = 6, 7, 8, 9, 10
local G_POLEBALL, G_POLE, G_FLAG, G_STONE = 11, 12, 13, 14

-- entity tags
local TAG_GOOMBA, TAG_MUSH = 1, 2
local TAG_BUMP, TAG_POPUP, TAG_RESTORE = 10, 11, 20
-- Persistent spawn records read once from the map's "spawns" object layer;
-- they survive level resets and drive goomba/player (re)spawning.
local TAG_SP_G, TAG_SP_P = 21, 22
-- goomba/mushroom number slots
local S_X, S_Y, S_VX, S_VY, S_STATE, S_T, S_DIR, S_ANIM = 0, 1, 2, 3, 4, 5, 6, 7
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

-- Engine-owned frame clips (AnimationSystem) for the player forms and the
-- per-goomba waddle. Clips live in C++, so they survive the per-frame Lua
-- arena reset without an EntityStore slot for phase.

-- Phase 8: per-draw custom GLSL shader slot. The level-clear flag gets a
-- travelling highlight; everything else stays on the default program.
-- Phase 8: SaveStore slot for the persistent high score / best time.
local SAVE_SLOT = "mario"

-- Walk-cycle phase accumulator (seconds of actual ground movement). The
-- 4-frame strip is idle / stride A / stride B / airborne, sampled as the
-- classic 3-pose loop step-idle-step while grounded.
-- Attract AI toggle. 0 = human has the controller (default), 1 = the AI
-- drives. The AI used to be unconditional whenever no horizontal key was
-- held, which meant it grabbed the controller the moment the player let go.


-- head-bit result reported by move_y

-- test hooks (parsed once)

-- DIE3 scripted death counter / verification flag
-- PHYS selftest: phase clock / one-shot logs / jump apex tracking
local phys_t = 0.0 + 0.0
local phys_log1, phys_log2, phys_apex_log = 0 + 0, 0 + 0, 0 + 0
-- BLOCKS selftest: phase clock / one-shot logs / max bump offset

-- FakeLua binds file-level locals as const, so everything that changes
-- across frames lives in a host-side NativeObject instead (see
-- docs/SCRIPTING.md rule 3). Fields read and write like a plain table.
local S = new_global_obj("mario", "state")

local function reset_state()
    S.map_id = 0 + 0
    S.tex_sheet = 0 + 0
    S.tex_goomba = 0 + 0
    S.tex_mush = 0 + 0
    S.tex_ps = 0 + 0
    S.tex_pb = 0 + 0
    S.tex_sky = 0 + 0  -- AI-illustrated sky
    S.anim_ps = 0 + 0
    S.anim_pb = 0 + 0
    S.fx_debris = 0 + 0
    S.inited = false
    S.fx_flag_shader = 0 + 0
    S.best_score = 0 + 0
    S.best_time = 0 + 0
    S.new_record = false
    S.px = 0.0 + 0.0
    S.py = 0.0 + 0.0
    S.vx = 0.0 + 0.0
    S.vy = 0.0 + 0.0
    S.box_w = 20.0 + 0.0
    S.box_h = 28.0 + 0.0
    S.big = 0 + 0
    S.on_ground = false
    S.facing = 1 + 0
    S.walk_t = 0.0 + 0.0
    S.coyote_t = 0.0 + 0.0
    S.buffer_t = 0.0 + 0.0
    S.cut_used = false
    S.jump_hold = 0 + 0
    S.ai_cd_t = 0.0 + 0.0
    S.ai_enabled = 0 + 0
    S.human_seen = 0 + 0
    S.invinc = 0.0 + 0.0
    S.mode = M_PLAY + 0
    S.mtime = 0.0 + 0.0
    S.win_bonus = 0 + 0
    S.death_done = false
    S.score = 0 + 0
    S.coins = 0 + 0
    S.lives = 3 + 0
    S.time_left = 300 + 0
    S.time_acc = 0.0 + 0.0
    S.frame_n = 0 + 0
    S.cam_x = 0.0 + 0.0
    S.cam_y = 0.0 + 0.0
    S.cam_vw = 960.0 + 0.0
    S.cam_vh = 540.0 + 0.0
    S.hit_ok = false
    S.hit_col = 0 + 0
    S.hit_row = 0 + 0
    S.test_death = -1 + 0
    S.test_win = -1 + 0
    S.test_big = 0 + 0
    S.test_right = 0 + 0
    S.test_jump = 0 + 0
    S.test_die3 = 0 + 0
    S.test_tp = -1.0 + 0.0
    S.test_mush = 0 + 0
    S.test_phys = 0 + 0
    S.test_blocks = 0 + 0
    S.die3_left = 0 + 0
    S.die3_checked = 0 + 0
    S.phys_phase = 0 + 0
    S.jump_top = 9999.0 + 0.0
    S.blk_t = 0.0 + 0.0
    S.blk_phase = 0 + 0
    S.blk_flag = 0 + 0
    S.max_bump = 0.0 + 0.0
    S.bonk_kill_n = 0 + 0
end

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

-- ---------------------------------------------------------------------------
-- score / coins
-- ---------------------------------------------------------------------------

-- Phase 8 SaveStore: the best score and the best remaining time survive a
-- process restart. storage_save/load take a slot name, so each run writes one
-- JSON file under the engine save directory.
local function load_records()
    if not storage_load(SAVE_SLOT) then return end
    S.best_score = storage_get_num("best_score", 0)
    S.best_time = storage_get_num("best_time", 0)
end

local function save_records()
    storage_set_num("best_score", S.best_score)
    storage_set_num("best_time", S.best_time)
    storage_set_str("version", "1.3")
    storage_save(SAVE_SLOT)
end

local function add_score(points, wx, wy)
    S.score = S.score + points
    local id = ent_create(TAG_POPUP)
    ent_set_num(id, S_X, wx)
    ent_set_num(id, S_Y, wy)
    ent_set_num(id, S_VX, points)
    ent_set_num(id, S_T, 0.75)
end

local function add_coin(wx, wy)
    S.coins = S.coins + 1
    if S.coins >= 100 then
        S.coins = S.coins - 100
        S.lives = S.lives + 1
        audio_play("win", 0.35)
    else
        audio_play("coin", 0.5)
    end
    add_score(200, wx, wy)
end

-- ---------------------------------------------------------------------------
-- level reset / entity spawning
-- ---------------------------------------------------------------------------

local function destroy_goomba(id)
    -- release the engine animation slot paired with the entity
    local aid = ent_num(id, S_ANIM)
    if aid > 0 then anim_destroy(aid) end
    ent_destroy(id)
end

-- Spawn a goomba at world pixels (x, y); coordinates come from the "spawns"
-- object layer's point objects (or test hooks), not tile cells.
local function spawn_goomba_at(x, y)
    local id = ent_create(TAG_GOOMBA)
    ent_set_num(id, S_X, x)
    ent_set_num(id, S_Y, y)
    ent_set_num(id, S_VX, -GOOMBA_SPEED)
    ent_set_num(id, S_VY, 0.0)
    ent_set_num(id, S_STATE, GOOMBA_WALK)
    ent_set_num(id, S_T, 0.0)
    ent_set_num(id, S_DIR, -1.0)
    -- 2-frame waddle clip owned by the engine (8 fps, looping); slot 7
    -- holds the anim id instead of a Lua-side phase accumulator.
    local aid = anim_create()
    anim_frame(aid, S.tex_goomba, 0.0, 0.0, 28.0, 28.0)
    anim_frame(aid, S.tex_goomba, 28.0, 0.0, 28.0, 28.0)
    anim_fps(aid, 8.0)
    anim_loop(aid, true)
    anim_play(aid)
    ent_set_num(id, S_ANIM, aid)
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
    S.box_w, S.box_h = 20.0, 28.0
    if S.test_big == 1 then
        S.big = 1
        S.box_w, S.box_h = 24.0, 44.0
    else
        S.big = 0
    end
    -- Spawn point authored as the "player_start" object in the map's
    -- spawns layer; fall back to the classic 2.5-tiles-in stance.
    local spx, spy = 2.5 * TILE, GROUND_TOP
    local sp = ent_at(0, TAG_SP_P)
    if sp ~= 0 then
        spx = ent_num(sp, S_X)
        spy = ent_num(sp, S_Y)
    end
    S.px = spx
    if S.test_tp >= 0.0 then S.px = S.test_tp end
    S.py = spy - S.box_h
    S.vx, S.vy = 0.0, 0.0
    S.on_ground = false
    S.facing = 1
    S.coyote_t, S.buffer_t = 0.0, 0.0
    S.cut_used = false
    S.jump_hold = 0
    S.invinc = 0.0
    S.mode = M_PLAY
    S.mtime = 0.0
    S.win_bonus = 0
    S.death_done = false
    S.cam_x = 0.0
    camera_snap(0.0, 0.0) -- scroll back to the level start on every reset
    S.time_left = 300
    S.time_acc = 0.0
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
            map_set_tile(S.map_id, c, r, g)
        end
    end
    -- goombas own engine animation slots; release them before the entities
    local gn0 = ent_count(TAG_GOOMBA)
    for i = 0, gn0 - 1 do
        local gid = ent_at(i, TAG_GOOMBA)
        if gid ~= 0 then
            local aid = ent_num(gid, S_ANIM)
            if aid > 0 then anim_destroy(aid) end
        end
    end
    clear_tag(TAG_GOOMBA)
    clear_tag(TAG_MUSH)
    clear_tag(TAG_BUMP)
    clear_tag(TAG_POPUP)
    -- Sweep the persistent goomba spawn records (sourced once from the
    -- map's "spawns" object layer) into live entities.
    local gn = ent_count(TAG_SP_G)
    for i = 0, gn - 1 do
        local sp = ent_at(i, TAG_SP_G)
        if sp ~= 0 then
            spawn_goomba_at(ent_num(sp, S_X), ent_num(sp, S_Y))
        end
    end
    reset_player()
    -- test hook: a mushroom rises from the ground near the spawn point
    if S.test_mush == 1 then spawn_mushroom(6, 13) end
end

local function full_reset()
    S.score = 0
    S.coins = 0
    S.lives = 3
    reset_level()
end

-- Read the map's "spawns" object layer once and park every point in a
-- persistent EntityStore slot; level resets respawn from these instead of
-- scanning the tile grid for marker gids. Lives in its own function to keep
-- update()'s register count within the interpreter's limit.
local function read_spawns()
    local spawn_layer = map_find_layer(S.map_id, "spawns")
    local goomba_spawns = 0
    if spawn_layer >= 0 then
        local n = map_obj_count(S.map_id, spawn_layer)
        for i = 0, n - 1 do
            local nm = map_obj_name(S.map_id, spawn_layer, i)
            local ox = map_obj_x(S.map_id, spawn_layer, i)
            local oy = map_obj_y(S.map_id, spawn_layer, i)
            if nm == "goomba" then
                local rec = ent_create(TAG_SP_G)
                ent_set_num(rec, S_X, ox)
                ent_set_num(rec, S_Y, oy)
                goomba_spawns = goomba_spawns + 1
            elseif nm == "player_start" then
                local rec = ent_create(TAG_SP_P)
                ent_set_num(rec, S_X, ox)
                ent_set_num(rec, S_Y, oy)
            end
        end
    end
    log_number(900200 + goomba_spawns)
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
                S.bonk_kill_n = S.bonk_kill_n + 1
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
    local g = map_tile(S.map_id, c, r)
    if g == G_QUESTION then
        if bump_active(c, r) then return end
        map_set_tile(S.map_id, c, r, G_USED)
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
        if S.big == 1 then
            map_set_tile(S.map_id, c, r, 0)
            audio_play("brick", 0.5)
            log_number(300003)
            local cx = c * TILE + TILE * 0.5
            local cy = r * TILE + TILE * 0.5
            part_set_color(S.fx_debris, 0.72, 0.31, 0.20, 1.0, 0.0)
            part_emit(S.fx_debris, 8, cx, cy)
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
    local nx = S.px + dx
    if not box_hits_solid(nx, S.py, S.box_w, S.box_h) then
        S.px = nx
        return
    end
    -- snap to the wall and kill horizontal speed
    if dx > 0.0 then
        local cc = math.floor((nx + S.box_w) / TILE)
        S.px = cc * TILE - S.box_w
    elseif dx < 0.0 then
        local cc = math.floor(nx / TILE)
        S.px = (cc + 1) * TILE
    end
    S.vx = 0.0
end

local function move_y_player(dy)
    local ny = S.py + dy
    if not box_hits_solid(S.px, ny, S.box_w, S.box_h) then
        S.py = ny
        return
    end
    if dy > 0.0 then
        local rr = math.floor((ny + S.box_h) / TILE)
        S.py = rr * TILE - S.box_h
        S.vy = 0.0
        S.on_ground = true
    else
        -- head bump: find the solid cell across the head span, prefer center
        local rr = math.floor(ny / TILE)
        local center_c = math.floor((S.px + S.box_w * 0.5) / TILE)
        local c0 = math.floor(S.px / TILE)
        local c1 = math.floor((S.px + S.box_w - 0.01) / TILE)
        local chosen = -1
        if solid_at(center_c, rr) then
            chosen = center_c
        else
            for c = c0, c1 do
                if solid_at(c, rr) then chosen = c break end
            end
        end
        S.py = (rr + 1) * TILE
        S.vy = 0.0
        if chosen >= 0 then
            S.hit_ok = true
            S.hit_col = chosen
            S.hit_row = rr
        end
    end
end

local function hurt_player()
    if S.invinc > 0.0 or S.mode ~= M_PLAY then return end
    if S.big == 1 then
        local feet = S.py + S.box_h
        S.big = 0
        S.box_w, S.box_h = 20.0, 28.0
        S.py = feet - S.box_h -- keep feet planted (any surface)
        S.invinc = 1.5
        audio_play("lose", 0.35)
        log_number(303000 + math.floor(S.box_h))
        if S.py + S.box_h == feet then log_number(303200) end
    else
        S.mode = M_DYING
        S.mtime = 0.0
        S.death_done = false
        S.vx = 0.0
        S.vy = -420.0
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
                if t >= 0.5 then destroy_goomba(id) end
            elseif st == GOOMBA_DEAD then
                local y = ent_num(id, S_Y)
                local evy = ent_num(id, S_VY) + GRAV * dt
                ent_set_num(id, S_VY, evy)
                ent_set_num(id, S_Y, y + evy * dt)
                if y > GROUND_TOP + 96.0 then destroy_goomba(id) end
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
                -- the 2-frame waddle is advanced by the engine animator
                local y = ent_num(id, S_Y)
                local gxx = ent_num(id, S_X)
                if y > GROUND_TOP + 120.0 or gxx < S.cam_x - 80.0
                    or gxx > MAP_W + 80.0 then
                    destroy_goomba(id)
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
                if y > GROUND_TOP + 96.0 or x < S.cam_x - 100.0 or x > MAP_W + 100.0 then
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
    if S.mode ~= M_PLAY or S.invinc > 0.0 then return end
    local n = ent_count(TAG_GOOMBA)
    for i = 0, n - 1 do
        local id = ent_at(i, TAG_GOOMBA)
        if id ~= 0 and ent_num(id, S_STATE) == GOOMBA_WALK then
            local gx = ent_num(id, S_X) + 1.0
            local gy = ent_num(id, S_Y) + 1.0
            if overlaps(S.px, S.py, S.box_w, S.box_h, gx, gy, 26.0, 26.0) then
                local stomp = S.vy > 1.0 and feet_before <= gy + 16.0
                if stomp then
                    ent_set_num(id, S_STATE, GOOMBA_SQUASH)
                    ent_set_num(id, S_T, 0.0)
                    add_score(100, gx, gy)
                    audio_play("paddle", 0.45)
                    if S.jump_hold > 0 then S.vy = -430.0 else S.vy = -260.0 end
                    S.py = gy - S.box_h
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
    if S.mode ~= M_PLAY then return end
    local n = ent_count(TAG_MUSH)
    for i = 0, n - 1 do
        local id = ent_at(i, TAG_MUSH)
        if id ~= 0 and ent_num(id, S_STATE) == MUSH_WALK then
            local mx = ent_num(id, S_X)
            local my = ent_num(id, S_Y)
            if overlaps(S.px, S.py, S.box_w, S.box_h, mx, my, 22.0, 22.0) then
                add_score(1000, mx, my)
                audio_play("launch", 0.45)
                if S.big == 0 then
                    local feet = S.py + S.box_h
                    S.big = 1
                    S.box_w, S.box_h = 24.0, 44.0
                    S.py = feet - S.box_h
                    log_number(300002)
                    log_number(302000 + math.floor(S.box_h))
                    if S.py + S.box_h == feet then log_number(303100) end
                else
                    log_number(300014)
                end
                ent_destroy(id)
            end
        end
    end
end

local function collect_coins()
    local cx = math.floor((S.px + S.box_w * 0.5) / TILE)
    local cy = math.floor((S.py + S.box_h * 0.5) / TILE)
    for r = cy - 2, cy + 2 do
        for c = cx - 2, cx + 2 do
            if map_tile(S.map_id, c, r) == G_COIN then
                map_set_tile(S.map_id, c, r, 0)
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
    local cc = math.floor((S.px + S.box_w * 0.5) / TILE)
    local r0 = math.floor(S.py / TILE)
    local r1 = math.floor((S.py + S.box_h) / TILE)
    for r = r0, r1 do
        local g = map_tile(S.map_id, cc, r)
        if g == G_POLE or g == G_POLEBALL then return true end
    end
    if cc + 1 == POLE_COL or cc == POLE_COL then
        for r = r0, r1 do
            if map_tile(S.map_id, POLE_COL, r) == G_POLE then return true end
        end
    end
    return false
end

local function start_win()
    S.mode = M_SLIDE
    S.mtime = 0.0
    S.facing = 1
    S.vx = 0.0
    S.px = POLE_COL * TILE - S.box_w - 3.0
    audio_play("win", 0.5)
    -- Phase 8 SaveStore: bank the run's score and the time left on the clock.
    -- time_left is still counting down here, so it is the bonus that decides
    -- whether this run beat the record.
    S.new_record = false
    local final_score = S.score + S.win_bonus
    if final_score > S.best_score then
        S.best_score = final_score
        S.new_record = true
    end
    if S.time_left > S.best_time then S.best_time = S.time_left end
    save_records()
    log_number(300005)
    log_number(300020)
    log_number(300030 + S.best_score)
end

local function finish_death()
    S.lives = S.lives - 1
    if S.test_die3 == 1 then S.die3_left = S.die3_left - 1 end
    log_number(300004)
    log_number(301000 + S.lives)
    if S.lives <= 0 then
        S.mode = M_GAMEOVER
        S.mtime = 0.0
    else
        reset_level()
    end
end

-- ---------------------------------------------------------------------------
-- input / AI
-- ---------------------------------------------------------------------------

-- Row the player's feet occupy while standing (ground surface is row+1).
local function ai_stand_row()
    return math.floor((S.py + S.box_h - 0.01) / TILE)
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
        if solid_at(cc, sr) then
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
    local pad = input_pad_connected(0)
    if input_key_down("left") or input_key_down("a") then move = -1.0 end
    if input_key_down("right") or input_key_down("d") then move = 1.0 end
    -- Phase 9: gamepad left stick / dpad steer (the axis already has its
    -- dead zone removed; 0.5 is the digital-engagement threshold).
    if pad then
        local lx = input_pad_axis(0, "left_x")
        if lx < -0.5 or input_pad_down(0, "dpad_left") then move = -1.0 end
        if lx > 0.5 or input_pad_down(0, "dpad_right") then move = 1.0 end
    end

    local key_jump = input_key_pressed("space") or input_key_pressed("w")
        or input_key_pressed("up")
    local real_held = input_key_down("space") or input_key_down("w")
        or input_key_down("up")
    -- A jumps (edge + hold, mirroring the keyboard semantics).
    if pad then
        if input_pad_pressed(0, "a") then key_jump = true end
        if input_pad_down(0, "a") then real_held = true end
    end
    if move ~= 0.0 or key_jump or real_held then S.human_seen = 1 end

    -- P toggles the attract AI so a human keeps the controller by default.
    if input_key_pressed("p") then
        S.ai_enabled = 1 - S.ai_enabled
        S.ai_cd_t = 0.0
        log_number(860000 + S.ai_enabled)
    end

    if S.test_jump == 1 then key_jump = true end
    local key_held = real_held
    if S.test_jump == 1 then key_held = true end
    -- The engine's key table spells the modifiers left_shift/right_shift;
    -- "shift" resolves to nothing, so it silently never ran.
    local running = input_key_down("left_shift")
        or input_key_down("right_shift")
    -- X (or either shoulder trigger) is the run button on a gamepad.
    if pad and (input_pad_down(0, "x") or input_pad_axis(0, "right_trigger") > 0.5) then
        running = true
    end
    if S.test_right == 1 then
        move = 1.0
        running = true
    end

    -- Attract AI, only while it is switched on and the human is not steering.
    -- It used to engage on `move == 0` alone, so letting go of the keys handed
    -- the controller to an AI that runs right and jumps on its own. P toggles
    -- it; the scripted TP hook disables it so deterministic fixtures can hold
    -- their position.
    --
    -- The planner only commits to jumps while grounded: it classifies the
    -- nearest threat (wall height / pit width / goomba gap) and jumps inside
    -- a precomputed lead window for a full-height RUN takeoff (apex ~130px,
    -- ~196px horizontal arc). Airborne frames never queue jumps, so there is
    -- no buffered re-hop when landing. All timing is in seconds so the
    -- behaviour is identical at 60fps and under the headless fast clock.
    if S.ai_enabled == 1 and move == 0.0 and S.test_tp < 0.0 then
        ai_active = true
        move = 1.0
        running = true
        local cx = S.px + S.box_w * 0.5
        local toes = S.px + S.box_w
        if S.ai_cd_t > 0.0 then S.ai_cd_t = S.ai_cd_t - dt end

        if S.on_ground and S.ai_cd_t <= 0.0 then
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
                S.ai_cd_t = 0.25
                log_number(310000 + why)
            end
        end
    end

    -- human latch: a tap tracks 12 frames then follows the real button, so
    -- per-frame flicker cannot end a hop early; releases within the latch
    -- still allow the variable-height cut.
    if key_jump and not ai_active then S.jump_hold = 12 end
    if S.jump_hold > 0 then
        if key_held then held = true end
        S.jump_hold = S.jump_hold - 1
    else
        held = key_held
    end
    -- AI jumps: hold through the whole ascent for full-height arcs; release
    -- at apex so a descent-side contact never restores the cut. The launch
    -- frame must count as held: intent is built while vy is still the old
    -- value (0), but the jump is launched later in the same step, so without
    -- key_jump the variable-height cut would immediately clip every AI jump
    -- to 42% launch speed (~23px, unable to clear pipes).
    if ai_active then held = S.vy < 0.0 or key_jump end
    return move, running, pressed or key_jump, held
end

-- ---------------------------------------------------------------------------
-- per-frame update
-- ---------------------------------------------------------------------------

local function step_player(dt, intent_x, running, jump_pressed, jumbox_held)
    local target = running and RUN_SPEED or WALK_SPEED
    if intent_x > 0.0 then
        S.facing = 1
        local rate = S.on_ground and G_ACC or A_ACC
        if S.vx < target then S.vx = math.min(target, S.vx + rate * dt) end
    elseif intent_x < 0.0 then
        S.facing = -1
        local rate = S.on_ground and G_ACC or A_ACC
        if S.vx > -target then S.vx = math.max(-target, S.vx - rate * dt) end
    elseif S.on_ground then
        if S.vx > 0 then S.vx = math.max(0.0, S.vx - FRICTION * dt) end
        if S.vx < 0 then S.vx = math.min(0.0, S.vx + FRICTION * dt) end
    end

    if jump_pressed then S.buffer_t = JUMP_BUFFER end
    if S.on_ground then S.coyote_t = COYOTE end

    S.vx = S.vx + 0.0 -- no platform forces
    S.vy = math.min(MAX_FALL, S.vy + GRAV * dt)

    local ddx = S.vx * dt
    local ddy = S.vy * dt
    local n = math.ceil(math.max(math.abs(ddx), math.abs(ddy)) / 8.0)
    if n < 1 then n = 1 end
    local sx = ddx / n
    local sy = ddy / n
    S.hit_ok = false
    local feet_before = S.py + S.box_h
    local landed = false
    for _ = 1, n do
        move_x_player(sx)
        S.on_ground = false
        move_y_player(sy)
        if S.vy == 0.0 and sy > 0.0 then landed = true end
        if S.hit_ok then
            handle_block(S.hit_col, S.hit_row)
            S.hit_ok = false
        end
    end

    -- coyote + buffered jump (negative y = up)
    local jump_launched = false
    if S.buffer_t > 0.0 and S.coyote_t > 0.0 and not S.cut_used then
        S.vy = 0.0 - JUMP_V
        S.on_ground = false
        S.coyote_t = 0.0
        S.buffer_t = 0.0
        S.cut_used = true
        jump_launched = true
        audio_play("jump", 0.3)
        log_number(312000 + math.floor(S.px))
    end
    -- variable jump height: releasing the button while ascending cuts rise.
    -- The launch frame itself is exempt: a buffered/coyote jump fires after
    -- intent was built (held may reflect the pre-jump state, e.g. AI while
    -- falling), so cutting on that same frame would shrink every buffered
    -- jump to 42% launch speed.
    if not jump_launched and not jumbox_held and S.vy < 0.0 and S.cut_used then
        S.vy = S.vy * JUMP_CUT
        S.cut_used = false
    end

    -- ground probe; landing re-arms the jump cut
    if S.vy >= 0.0 and box_hits_solid(S.px, S.py + 2.0, S.box_w, S.box_h) then
        S.on_ground = true
        S.cut_used = false
    end

    -- SMB edge rule: the player cannot walk left past the camera's left edge
    local left_bound = S.cam_x
    if S.px < left_bound then S.px = left_bound end

    resolve_player_enemies(feet_before)
    resolve_player_mushrooms()
    collect_coins()

    if S.py > GROUND_TOP + 96.0 and S.mode == M_PLAY then
        S.mode = M_DYING
        S.mtime = 0.0
        S.death_done = false
        S.vy = -200.0
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

-- Pick a frame from the 4-frame strip:
-- 0 idle, 1/2 strides, 3 airborne. Ground movement advances walk_t at a
-- cadence proportional to speed, cycling step-A -> idle -> step-B -> idle
-- (the SMB three-pose walk); standing still holds frame 0.
local function player_frame(dt)
    if not S.on_ground then
        return 3
    end
    local speed = math.abs(S.vx)
    if speed < 12.0 then
        return 0
    end
    S.walk_t = S.walk_t + dt * (6.0 + 5.0 * speed / RUN_SPEED)
    local p = imod(math.floor(S.walk_t), 4.0)
    if p == 0.0 or p == 2.0 then
        return 1
    end
    return 2
end

local function draw_world(dt)
    if S.tex_sky ~= 0 then
        sprite_draw(S.tex_sky, S.cam_x, S.cam_y, S.cam_vw, S.cam_vh)
    else
        draw_quad(S.cam_x, S.cam_y, S.cam_vw, S.cam_vh, 0.36, 0.58, 0.95, 1.0)
    end
    map_draw(S.map_id)

    -- Phase 8 per-draw custom shader: a shimmer band travels down the goal
    -- pole. draw_use_shader switches the batcher onto slot 1 for the calls
    -- that follow and 0 restores the default program, so only these quads pay
    -- for the extra pass. The pole is re-drawn here (it is part of the tilemap
    -- above) with the shader applied, giving a visible highlight on approach.
    if S.fx_flag_shader > 0 and S.mode ~= M_CLEAR then
        shader_set_float(S.fx_flag_shader, "u_time", time_frame())
        draw_use_shader(S.fx_flag_shader)
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
            local g = map_tile(S.map_id, c, r)
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
                sprite_draw_region(S.tex_sheet, sx0, sy0, TILE, TILE,
                                   dx0, dy0, TILE, TILE)
            end
        end
    end

    -- mushrooms
    local mn = ent_count(TAG_MUSH)
    for i = 0, mn - 1 do
        local id = ent_at(i, TAG_MUSH)
        if id ~= 0 then
            sprite_draw(S.tex_mush, ent_num(id, S_X), ent_num(id, S_Y), 24, 24)
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
                -- squashed: frame 0 squished vertically into the lower half
                sprite_draw_flip(S.tex_goomba, 0, 0, 28, 28,
                                 gx - 1.0, gy + 16.0, 28, 12, 0.0, 0.0)
            else
                -- engine advances the 2-frame waddle; mirror against the
                -- patrol direction
                anim_draw_flip(ent_num(id, S_ANIM), gx, gy, 28.0, 28.0,
                               ent_num(id, S_VX) < 0.0, false)
            end
        end
    end

    -- player (blinks while invincible); face the movement direction.
    -- The engine clip holds idle/stride/stride/airborne frames; gameplay
    -- state (ground/air/speed via player_frame) pins the current frame,
    -- and anim_draw_flip mirrors in UV space for the facing direction.
    local blink = S.invinc <= 0.0 or imod(math.floor(S.invinc * 12.0), 2.0) == 0.0
    if blink and S.mode ~= M_GAMEOVER then
        local panim = S.anim_ps
        if S.big == 1 then panim = S.anim_pb end
        local fr = player_frame(dt)
        anim_set_frame(panim, fr)
        anim_draw_flip(panim, S.px, S.py, S.box_w, S.box_h, S.facing < 0, false)
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
    local sx = S.cam_x
    local sy = S.cam_y
    draw_text_str("SCORE " .. istr(S.score), sx + 16.0, sy + 12.0, 0.6,
                  1.0, 1.0, 1.0)
    draw_text_str("COINS x" .. istr(S.coins), sx + 250.0, sy + 12.0, 0.6,
                  1.0, 1.0, 1.0)
    draw_text_str("WORLD 1-1", sx + 470.0, sy + 12.0, 0.6, 1.0, 1.0, 1.0)
    draw_text_str("TIME " .. istr(S.time_left), sx + 670.0, sy + 12.0, 0.6,
                  1.0, 1.0, 1.0)
    draw_text_str("LIVES x" .. istr(S.lives), sx + 830.0, sy + 12.0, 0.6,
                  1.0, 1.0, 1.0)
    -- Phase 8 SaveStore: the record banked on the previous run.
    if S.best_score > 0 then
        draw_text_str("BEST " .. istr(S.best_score), sx + 16.0, sy + 34.0, 0.45,
                      1.0, 0.92, 0.55)
    end
    -- Only shown while the attract AI actually has the controller, so the
    -- default view stays clean.
    if S.ai_enabled == 1 then
        draw_text_str("DEMO AI ON  (P)", sx + 700.0, sy + 34.0, 0.45,
                      0.6, 1.0, 0.6)
    end
end

local function draw_banner(title, sub)
    local bx = S.cam_x + 180.0
    local by = S.cam_y + 180.0
    draw_quad(bx, by, 600.0, 150.0, 0.0, 0.0, 0.0, 0.62)
    local tx = S.cam_x + 330.0
    local ty = S.cam_y + 215.0
    draw_text_str(title, tx, ty, 1.4, 1.0, 1.0, 0.6)
    local sx2 = S.cam_x + 270.0
    local sy2 = S.cam_y + 275.0
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
    if S.phys_phase == 0 then
        -- walk plateau
        mv = 1.0
        if S.vx >= 149.0 then
            log_number(820000 + math.floor(S.vx))
            S.phys_phase = 1
        end
    elseif S.phys_phase == 1 then
        -- jump: press on entry, hold until back on the ground
        if S.jump_top > 9000.0 then jp = true end
        held = true
    elseif S.phys_phase == 2 then
        -- run plateau, immediately after landing (no idle friction gap)
        mv = 1.0
        run = true
        if S.vx >= 234.0 then
            log_number(822000 + math.floor(S.vx))
            S.phys_phase = 3
        end
    end
    step_player(sdt, mv, run, jp, held)
    if S.vy < 0.0 and S.py < S.jump_top then S.jump_top = S.py end
    if S.phys_phase == 1 and S.jump_top < 9000.0 and S.on_ground then
        local hgt = GROUND_TOP - (S.jump_top + S.box_h)
        log_number(821000 + math.floor(hgt))
        S.jump_top = 9999.0
        S.phys_phase = 2
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
    S.blk_t = S.blk_t + sdt
    if S.blk_phase == 0 then
        S.blk_phase = 1
        S.blk_t = 0.0
        S.px = 440.0
        S.py = GROUND_TOP - S.box_h
        S.vx, S.vy = 0.0, 0.0
        S.on_ground = true
        S.cam_x = 0.0
    end
    local mv = 0.0
    local run = false
    local jp = false
    local held = false
    if S.blk_phase == 1 then
        -- first hop: lone ?(coin) at (14,9)
        if S.blk_t < 0.1 then jp = true end
        held = S.blk_t < 0.55
        if S.on_ground and S.blk_t > 0.55 then
            if map_tile(S.map_id, 14, 9) == G_USED then log_number(830001) end
            S.blk_phase = 2 S.blk_t = 0.0
        end
    elseif S.blk_phase == 2 then
        -- vertical hop: used block must not retrigger
        if S.blk_t < 0.1 then jp = true end
        held = S.blk_t < 0.55
        if S.on_ground and S.blk_t > 0.55 then
            if S.coins == 1 then log_number(830002) end
            S.blk_phase = 3 S.blk_t = 0.0
        end
    elseif S.blk_phase == 3 then
        -- Cross the first pit (cols 16-17) via the used ? block at (14,9).
        -- Physics-precision platforming is covered by the PHYS selftest;
        -- riding a single 32px perch is a frame-rate dependent pixel chase
        -- (the safe takeoff window shrinks to <1px at the headless clock),
        -- so this fixture places the player on the block lip and launches a
        -- fixed full RUN arc (~196px, lands near 648 past the 576 far lip).
        -- blk_flag: 0 = place on perch, 3 = hop launch, 4 = airborne.
        if S.blk_flag == 0 then
            S.px = 452.0
            S.py = 288.0 - S.box_h
            S.vx = RUN_SPEED
            S.vy = 0.0
            S.on_ground = true
            S.blk_flag = 3
            S.blk_t = 0.0
        end
        mv = 1.0 run = true
        if S.blk_flag == 3 then jp = true end
        held = true
    elseif S.blk_phase == 4 then
        -- brick at (20,9), small bump: deterministic vertical hop from a
        -- snapped spot (small 20px box centered in col 20). Frame-rate
        -- independent; PHYS covers the movement model itself.
        if S.blk_flag == 0 then
            S.px = 650.0
            S.py = GROUND_TOP - S.box_h
            S.vx, S.vy = 0.0, 0.0
            S.on_ground = true
            S.blk_flag = 1 S.blk_t = 0.0
        end
        if S.blk_flag == 1 then jp = true end
        held = true
        if S.blk_flag == 2 and S.on_ground and S.blk_t > 1.0 then
            S.blk_phase = 5 S.blk_t = 0.0 S.blk_flag = 0
        end
    elseif S.blk_phase == 5 then
        -- question at (21,9): mushroom block, vertical hop (col 21 center)
        if S.blk_flag == 0 then
            S.px = 682.0
            S.py = GROUND_TOP - S.box_h
            S.vx, S.vy = 0.0, 0.0
            S.on_ground = true
            S.blk_flag = 1 S.blk_t = 0.0
        end
        if S.blk_flag == 1 then jp = true end
        held = true
        if S.blk_flag == 2 and S.on_ground and S.blk_t > 1.0 then
            if ent_count(TAG_MUSH) == 1 then log_number(830003) end
            S.blk_phase = 6 S.blk_t = 0.0 S.blk_flag = 0
        end
    elseif S.blk_phase == 6 then
        -- Deterministic pickup: place the walking mushroom once on the ground
        -- just ahead of the player, then walk into it (repositioning it
        -- every frame would keep it perpetually out of reach). Validates the
        -- grow transition + 1000 score.
        if S.big == 1 then
            log_number(830004)
            S.blk_phase = 7 S.blk_t = 0.0 S.blk_flag = 0
        elseif S.blk_t > 4.0 then
            S.blk_phase = 7 S.blk_t = 0.0 S.blk_flag = 0
        else
            if S.blk_flag == 0 then
                S.blk_flag = 1
                local mn = ent_count(TAG_MUSH)
                for i = 0, mn - 1 do
                    local id = ent_at(i, TAG_MUSH)
                    if id ~= 0 then
                        ent_set_num(id, S_STATE, MUSH_WALK)
                        ent_set_num(id, S_X, S.px + 28.0)
                        ent_set_num(id, S_Y, GROUND_TOP - 24.0)
                        ent_set_num(id, S_VX, 0.0)
                        ent_set_num(id, S_VY, 0.0)
                    end
                end
            end
            mv = 1.0
        end
    elseif S.blk_phase == 7 then
        -- place a goomba atop (23,9) and hold it still for 0.5s: a patrolling
        -- one walks off to col 22 before the bonk hop reaches its band.
        if S.blk_flag == 0 then
            S.blk_flag = 1
            local gid = spawn_goomba_at(23 * TILE + 2.0, 8 * TILE + 4.0)
            ent_set_num(gid, S_VX, 0.0)
            ent_set_num(gid, S_DIR, 0.0)
        end
        if S.blk_t > 0.5 then
            S.blk_phase = 8 S.blk_t = 0.0 S.blk_flag = 0
        end
    elseif S.blk_phase == 8 then
        -- question (23,9): coin + goomba killed via bonk band; big 24px box
        -- centered in col 23, straight vertical hop.
        if S.blk_flag == 0 then
            S.px = 740.0
            S.py = GROUND_TOP - S.box_h
            S.vx, S.vy = 0.0, 0.0
            S.on_ground = true
            S.blk_flag = 1 S.blk_t = 0.0
        end
        if S.blk_flag == 1 then jp = true end
        held = true
        if S.blk_flag == 2 and S.on_ground and S.blk_t > 1.0 then
            if S.bonk_kill_n >= 1 then log_number(830007) end
            S.blk_phase = 9 S.blk_t = 0.0 S.blk_flag = 0
        end
    elseif S.blk_phase == 9 then
        -- brick (22,9): big form breaks it (col 22 center, vertical hop)
        if S.blk_flag == 0 then
            S.px = 708.0
            S.py = GROUND_TOP - S.box_h
            S.vx, S.vy = 0.0, 0.0
            S.on_ground = true
            S.blk_flag = 1 S.blk_t = 0.0
        end
        if S.blk_flag == 1 then jp = true end
        held = true
        if S.blk_flag == 2 and S.on_ground and S.blk_t > 1.0 then
            if map_tile(S.map_id, 22, 9) == 0 then log_number(830005) end
            S.blk_phase = 10 S.blk_t = 0.0 S.blk_flag = 0
        end
    elseif S.blk_phase == 10 then
        log_number(830006 + math.floor(S.max_bump * 10.0))
        S.blk_phase = 11
    end
    step_player(sdt, mv, run, jp, held)
    -- phase 3 perch-hop transition (state is only valid post-step)
    if S.blk_phase == 3 then
        if S.blk_flag == 3 and S.vy < 0.0 then S.blk_flag = 4 end
        if S.blk_flag == 4 and S.on_ground and S.py + S.box_h > 400.0 then
            S.blk_phase = 4 S.blk_t = 0.0 S.blk_flag = 0
        end
    end
    -- vertical-hop phases 4/5/8/9: arm(1) -> airborne(2) on launch
    if (S.blk_phase == 4 or S.blk_phase == 5 or S.blk_phase == 8 or S.blk_phase == 9)
        and S.blk_flag == 1 and S.vy < 0.0 then
        S.blk_flag = 2
    end
    update_goombas(sdt)
    update_mushrooms(sdt)
    -- clean per-frame timeline for diagnosis
    log_number(360000 + S.blk_phase)
    log_number(361000 + math.floor(S.px))
    log_number(362000 + math.floor(S.py))
    log_number(363000 + math.floor(S.vy))
    log_number(364000 + S.blk_flag)
    local bn = ent_count(TAG_BUMP)
    for i = 0, bn - 1 do
        local id = ent_at(i, TAG_BUMP)
        if id ~= 0 then
            local c = ent_num(id, 0)
            local r = ent_num(id, 1)
            local ox = bump_offset(c, r)
            if 0.0 - ox > S.max_bump then S.max_bump = 0.0 - ox end
        end
    end
end

-- ---------------------------------------------------------------------------
-- entry
-- ---------------------------------------------------------------------------

function update(dt)
    if S.seeded == nil then
        S.seeded = true
        reset_state()
    end

    S.frame_n = S.frame_n + 1
    if imod(S.frame_n, 100.0) == 0.0 then log_number(400000 + S.frame_n) end

    -- clamp the gameplay step so physics/AI are stable even when the host
    -- frame clock stalls (headless) or spikes (slow machines)
    local sdt = dt
    if sdt > 0.034 then sdt = 0.034 end

    if not S.inited then
        S.inited = true
        local td = os.getenv("MARIO_TEST_DEATH")
        local tw = os.getenv("MARIO_TEST_WIN")
        if td ~= nil then S.test_death = tonumber(td) + 0 end
        if tw ~= nil then S.test_win = tonumber(tw) + 0 end
        if os.getenv("MARIO_TEST_BIG") ~= nil then S.test_big = 1 end
        if os.getenv("MARIO_TEST_AI") ~= nil then S.ai_enabled = 1 end
        if os.getenv("MARIO_TEST_RIGHT") ~= nil then S.test_right = 1 end
        if os.getenv("MARIO_TEST_JUMP") ~= nil then S.test_jump = 1 end
        local ttp = os.getenv("MARIO_TEST_TP")
        if ttp ~= nil then S.test_tp = tonumber(ttp) + 0.0 end
        if os.getenv("MARIO_TEST_DIE3") ~= nil then
            S.test_die3 = 1
            S.die3_left = 3
        end
        if os.getenv("MARIO_TEST_MUSH") ~= nil then S.test_mush = 1 end
        if os.getenv("MARIO_TEST_PHYS") ~= nil then S.test_phys = 1 end
        if os.getenv("MARIO_TEST_BLOCKS") ~= nil then S.test_blocks = 1 end

        S.map_id = map_load("assets/mario.json")
        S.tex_sheet = sprite_load("assets/m_tiles.png")
        S.tex_sky = sprite_load("assets/art/mario_sky.png")
        S.tex_goomba = sprite_load("assets/m_goomba.png")
        S.tex_mush = sprite_load("assets/m_mushroom.png")
        S.tex_ps = sprite_load("assets/m_player_s.png")
        S.tex_pb = sprite_load("assets/m_player_b.png")
        -- Player clips: 4 frames each (idle / stride A / stride B / airborne).
        -- Gameplay state selects the frame every draw via anim_set_frame
        -- (paused clip), so the engine owns the clip while the script owns
        -- the state machine.
        S.anim_ps = anim_create()
        anim_frame(S.anim_ps, S.tex_ps, 0.0, 0.0, 20.0, 28.0)
        anim_frame(S.anim_ps, S.tex_ps, 20.0, 0.0, 20.0, 28.0)
        anim_frame(S.anim_ps, S.tex_ps, 40.0, 0.0, 20.0, 28.0)
        anim_frame(S.anim_ps, S.tex_ps, 60.0, 0.0, 20.0, 28.0)
        anim_fps(S.anim_ps, 10.0)
        anim_loop(S.anim_ps, true)
        anim_pause(S.anim_ps)
        S.anim_pb = anim_create()
        anim_frame(S.anim_pb, S.tex_pb, 0.0, 0.0, 24.0, 44.0)
        anim_frame(S.anim_pb, S.tex_pb, 24.0, 0.0, 24.0, 44.0)
        anim_frame(S.anim_pb, S.tex_pb, 48.0, 0.0, 24.0, 44.0)
        anim_frame(S.anim_pb, S.tex_pb, 72.0, 0.0, 24.0, 44.0)
        anim_fps(S.anim_pb, 10.0)
        anim_loop(S.anim_pb, true)
        anim_pause(S.anim_pb)
        -- Phase 8: custom GLSL slot for the flag shimmer, plus the saved
        -- high score from the previous run.
        S.fx_flag_shader = shader_load("assets/flag_shimmer.vert",
                                     "assets/flag_shimmer.frag")
        shader_set_float(S.fx_flag_shader, "u_speed", 0.35)
        shader_set_float(S.fx_flag_shader, "u_width", 0.14)
        shader_set_vec4(S.fx_flag_shader, "u_tint", 1.0, 0.96, 0.78, 1.0)
        load_records()
        S.fx_debris = part_create()
        part_set_lifetime(S.fx_debris, 0.35, 0.7)
        part_set_speed(S.fx_debris, 60.0, 200.0)
        part_set_direction(S.fx_debris, -2.4, 0.6)
        part_set_gravity(S.fx_debris, 0.0, 900.0)
        part_set_size(S.fx_debris, 4, 0)
        part_set_spin(S.fx_debris, 10)

        -- record destructible tiles once
        for r = 0, 14 do
            for c = 0, 111 do
                local g = map_tile(S.map_id, c, r)
                if g == G_BRICK or g == G_QUESTION or g == G_COIN then
                    local rec = ent_create(TAG_RESTORE)
                    ent_set_num(rec, 0, c)
                    ent_set_num(rec, 1, r)
                    ent_set_num(rec, 2, g)
                end
            end
        end
        -- Spawn points come from the "spawns" object layer (persistent
        -- EntityStore slots; see read_spawns).
        read_spawns()
        full_reset()
        -- DIE3 begins with a nonzero score; the GAME OVER reopen must zero it
        if S.test_die3 == 1 then S.score = 999 end
        log_number(900000 + map_cols(S.map_id))
    end

    S.mtime = S.mtime + sdt
    -- Esc or the gamepad Back/Select button leaves the game from any mode.
    if input_key_pressed("escape") or input_pad_pressed(0, "back") then
        window_quit()
    end
    if S.invinc > 0.0 then S.invinc = math.max(0.0, S.invinc - sdt) end
    if S.buffer_t > 0.0 then S.buffer_t = math.max(0.0, S.buffer_t - sdt) end
    if S.coyote_t > 0.0 then S.coyote_t = math.max(0.0, S.coyote_t - sdt) end

    if S.mode == M_PLAY then
        -- timer
        S.time_acc = S.time_acc + sdt
        while S.time_acc >= 1.0 do
            S.time_acc = S.time_acc - 1.0
            S.time_left = S.time_left - 1
        end
        if S.time_left <= 0 then
            S.mode = M_DYING
            S.mtime = 0.0
            S.death_done = false
            S.vy = -200.0
            audio_play("lose", 0.5)
            log_number(300012)
        elseif S.test_phys == 1 then
            run_phys_mode(sdt)
        elseif S.test_blocks == 1 then
            run_blocks_mode(sdt)
        else
            local intent_x, running, jp, jh = build_intent(sdt)
            step_player(sdt, intent_x, running, jp, jh)
            update_goombas(sdt)
            update_mushrooms(sdt)
            if touches_pole() then start_win() end
        end
    elseif S.mode == M_DYING then
        S.vy = S.vy + GRAV * sdt
        S.py = S.py + S.vy * sdt
        S.mtime = S.mtime + sdt
        if S.py > GROUND_TOP + 200.0 and not S.death_done then
            S.death_done = true
            finish_death()
        end
    elseif S.mode == M_SLIDE then
        if S.py + S.box_h < GROUND_TOP then S.py = S.py + SLIDE_SPEED * sdt end
        if S.py + S.box_h >= GROUND_TOP then
            S.py = GROUND_TOP - S.box_h
            S.mode = M_WALK
            log_number(300021)
        end
    elseif S.mode == M_WALK then
        S.px = S.px + WALK_OUT_SPEED * sdt
        if S.px >= CASTLE_X then
            S.mode = M_CLEAR
            S.mtime = 0.0
            local bonus = S.time_left * 10
            S.win_bonus = bonus
            S.score = S.score + bonus
            audio_play("win", 0.5)
            log_number(300022)
            log_number(300023 + bonus)
        end
    elseif S.mode == M_GAMEOVER then
        if input_key_pressed("space") or input_key_pressed("w")
            or input_key_pressed("up") or input_pad_pressed(0, "a")
            or S.test_jump == 1 then
            full_reset()
        elseif S.human_seen == 0 and S.mtime > 4.5 then
            -- unattended attract: loop forever
            full_reset()
            log_number(300030)
        end
    elseif S.mode == M_CLEAR then
        if input_key_pressed("space") or input_key_pressed("w")
            or input_key_pressed("up") or input_pad_pressed(0, "a") then
            reset_level()
        elseif S.human_seen == 0 and S.mtime > 4.5 then
            reset_level()
            log_number(300031)
        end
    end

    -- test hooks (forced transitions)
    if S.test_death >= 0 and S.frame_n == S.test_death and S.mode == M_PLAY then
        S.mode = M_DYING
        S.mtime = 0.0
        S.death_done = false
        S.vy = -200.0
        S.py = GROUND_TOP + 40.0
    end
    if S.test_win >= 0 and S.frame_n == S.test_win and S.mode == M_PLAY then
        start_win()
    end

    -- DIE3: force one death per fresh level until three lives are spent,
    -- then the GAME OVER reopen (MARIO_TEST_JUMP) must restore everything.
    if S.test_die3 == 1 and S.die3_left > 0 and S.mode == M_PLAY then
        S.mode = M_DYING
        S.mtime = 0.0
        S.death_done = false
        S.vy = -200.0
        S.py = GROUND_TOP + 40.0
    end
    if S.test_die3 == 1 and S.die3_left == 0 and S.mode == M_PLAY
        and S.die3_checked == 0 and S.frame_n > 30 then
        if S.lives == 3 and S.score == 0 and S.time_left == 300
            and ent_count(TAG_GOOMBA) == 7
            and map_tile(S.map_id, 14, 9) == G_QUESTION
            and map_tile(S.map_id, 21, 9) == G_QUESTION then
            S.die3_checked = 1
            log_number(840000)
        end
    end

    update_bumps(sdt)
    update_popups(sdt)

    -- camera follows via the engine: SMB scroll lock (never moves left),
    -- clamped to the level, instant (no smoothing). x bounds keep the view
    -- inside [0, MAP_W]; the y bounds are wide open because 1-1 scrolls
    -- horizontally only.
    S.cam_vw = camera_viewport_w()
    S.cam_vh = camera_viewport_h()
    camera_forward_only(true)
    camera_smoothing(0.0)
    camera_set_bounds(0.0, -100000.0, MAP_W, 200000.0)
    camera_follow(S.px + S.box_w * 0.5 - S.cam_vw * 0.05, S.cam_vh * 0.5)
    S.cam_x = camera_x()
    S.cam_y = camera_y()

    draw_world(sdt)
    draw_hud()

    if S.frame_n < 200 and S.mode == M_PLAY then
        local hx = S.cam_x + 150.0
        local hy = S.cam_y + 500.0
        draw_text_str("ARROWS/A/D MOVE   SHIFT RUN   SPACE JUMP   ESC QUIT",
                      hx, hy, 0.6, 1.0, 1.0, 1.0)
        draw_text_str("GAMEPAD: STICK/D-PAD MOVE   X RUN   A JUMP   BACK QUIT",
                      hx, hy + 22.0, 0.5, 0.85, 0.9, 1.0)
    end

    if S.mode == M_GAMEOVER then
        draw_banner("GAME OVER", "PRESS SPACE TO RETRY")
    elseif S.mode == M_CLEAR then
        draw_banner("LEVEL CLEAR!", "SCORE " .. istr(S.score)
            .. "   TIME BONUS " .. istr(S.win_bonus)
            .. "   PRESS SPACE")
    end

    -- debug markers for headless verification
    if S.frame_n >= 599 and S.frame_n <= 601 then log_number(410000 + S.frame_n) end
    if S.frame_n == 90 or S.frame_n == 200 or S.frame_n == 300 or S.frame_n == 400
        or S.frame_n == 600 or S.frame_n == 900 or S.frame_n == 1200 or S.frame_n == 1500 then
        log_number(200000 + S.frame_n)
        log_number(201000 + S.mode)
        log_number(202000 + S.lives)
        log_number(203000 + S.score)
        log_number(204000 + math.floor(S.px))
        log_number(205000 + ent_count(TAG_GOOMBA))
        log_number(206000 + math.floor(S.vx))
    end

    return 0
end
