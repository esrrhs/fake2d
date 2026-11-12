-- fake2d pure-Lua maze chase demo (--path-demo).
--
-- Shows off both grid routers: the three chasers share ONE flow field
-- (integrated cost-to-go, rebuilt as the player moves) and steer by local
-- descent, while the player's attract autopilot still runs A* and draws its
-- polyline. P toggles the overlays: chaser cost field + player path.
--
-- Persistent state: file-level numeric locals (per-frame tables are reset at
-- the frame boundary), plus C++-owned EntityStore slots for the chasers
-- (tag 7): slot 0 x, 1 y, 2 repath timer, 3 waypoint index, 4 waypoint count.

local TILE = 24.0
local TAG_CHASER = 7



local player_speed = 150.0
local chaser_speed = 108.0



-- FakeLua binds file-level locals as const, so everything that changes
-- across frames lives in a host-side NativeObject instead (see
-- docs/SCRIPTING.md rule 3). Fields read and write like a plain table.
local S = new_global_obj("path_demo", "state")

local function reset_state()
    S.map_id = 0 + 0
    S.nav_id = 0 + 0       -- separate Tilemap instance for the player's path
    S.inited = false
    S.spawns_read = false
    S.px = 0.0 + 0.0         -- player (world pixels, centre)
    S.py = 0.0 + 0.0
    S.sx = 0.0 + 0.0         -- spawn point
    S.sy = 0.0 + 0.0
    S.gx = 0.0 + 0.0         -- goal marker
    S.gy = 0.0 + 0.0
    S.player_wp = 1.0 + 0.0  -- attract-mode waypoint cursor
    S.player_wp_n = 0.0 + 0.0
    S.repath_t = 0.0 + 0.0   -- player attract re-path timer
    S.idle_t = 0.0 + 0.0     -- seconds without input
    S.caught = 0 + 0
    S.clears = 0 + 0
    S.run_t = 0.0 + 0.0
    S.show_paths = true
    S.frame_n = 0 + 0
    S.flow_t = 0.0 + 0.0    -- shared chaser field rebuild timer
    S.flow_c = -1.0          -- goal cell the field was built for
    S.flow_r = -1.0
    S.flow_ok = false
end

local function cell_of(v)
    return math.floor(v / TILE)
end

-- Axis-separated collision against the solid grid: the player is a 14px box.
local function blocked(x, y)
    local h = 7.0
    if map_solid(S.map_id, cell_of(x - h), cell_of(y - h)) then return true end
    if map_solid(S.map_id, cell_of(x + h), cell_of(y - h)) then return true end
    if map_solid(S.map_id, cell_of(x - h), cell_of(y + h)) then return true end
    if map_solid(S.map_id, cell_of(x + h), cell_of(y + h)) then return true end
    return false
end

-- Read the object layer once: player_start / goal / chaser points.
local function read_spawns()
    local layers = map_layer_count(S.map_id)
    local li = 0
    while li < layers do
        if map_layer_type(S.map_id, li) == 2 then
            local n = map_obj_count(S.map_id, li)
            local i = 0
            while i < n do
                local name = map_obj_name(S.map_id, li, i)
                local ox = map_obj_x(S.map_id, li, i)
                local oy = map_obj_y(S.map_id, li, i)
                if name == "player_start" then
                    S.sx = ox
                    S.sy = oy
                elseif name == "goal" then
                    S.gx = ox
                    S.gy = oy
                elseif name == "chaser" then
                    local id = ent_create(TAG_CHASER)
                    ent_set_num(id, 0, ox)
                    ent_set_num(id, 1, oy)
                    ent_set_num(id, 2, 0.0)
                    ent_set_num(id, 3, 1.0)
                    ent_set_num(id, 4, 0.0)
                end
                i = i + 1
            end
        end
        li = li + 1
    end
end

local function reset_run()
    S.px = S.sx
    S.py = S.sy
    S.run_t = 0.0
    S.repath_t = 0.0
    local n = ent_count(TAG_CHASER)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_CHASER)
        -- Send every chaser back to its own start cell (kept in slot 5/6 on
        -- first sight of the spawn point).
        ent_set_num(id, 0, ent_num(id, 5))
        ent_set_num(id, 1, ent_num(id, 6))
        ent_set_num(id, 2, 0.0)
        ent_set_num(id, 3, 1.0)
        ent_set_num(id, 4, 0.0)
        i = i + 1
    end
    S.flow_t = 0.0
end

-- Steer one chaser along the shared flow field. The direction points at the
-- lowest-cost neighbouring cell centre, so the same axis-separated move as
-- the A* followers keeps the 14px box from clipping wall corners.
local function follow_flow(id, cx, cy, dt)
    if S.flow_ok and map_flow_dir(S.map_id, cx, cy) then
        local nx = cx + map_flow_dir_x(S.map_id) * chaser_speed * dt
        if not blocked(nx, cy) then cx = nx end
        local ny = cy + map_flow_dir_y(S.map_id) * chaser_speed * dt
        if not blocked(cx, ny) then cy = ny end
        ent_set_num(id, 0, cx)
        ent_set_num(id, 1, cy)
    end
end

local function step_chasers(dt)
    -- One field serves every chaser; rebuild when the player changes cell or
    -- every quarter second (the maze is small enough that this is cheap).
    local pc = cell_of(S.px)
    local pr = cell_of(S.py)
    S.flow_t = S.flow_t - dt
    if S.flow_t <= 0.0 or pc ~= S.flow_c or pr ~= S.flow_r then
        S.flow_t = 0.25
        S.flow_c = pc
        S.flow_r = pr
        S.flow_ok = map_flow_build(S.map_id, S.px, S.py, true)
    end
    local n = ent_count(TAG_CHASER)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_CHASER)
        local cx = ent_num(id, 0)
        local cy = ent_num(id, 1)
        follow_flow(id, cx, cy, dt)
        cx = ent_num(id, 0)
        cy = ent_num(id, 1)
        -- Catch: within half a tile of the player.
        local dx = cx - S.px
        local dy = cy - S.py
        if dx * dx + dy * dy < 144.0 then
            S.caught = S.caught + 1
            reset_run()
            return 0
        end
        i = i + 1
    end
    return 0
end

local function step_player(dt)
    local mx = 0.0
    local my = 0.0
    if input_key_down("left") or input_key_down("a") then mx = mx - 1.0 end
    if input_key_down("right") or input_key_down("d") then mx = mx + 1.0 end
    if input_key_down("up") or input_key_down("w") then my = my - 1.0 end
    if input_key_down("down") or input_key_down("s") then my = my + 1.0 end
    -- gamepad: left stick / d-pad
    local padx = input_pad_axis(0, "leftx")
    local pady = input_pad_axis(0, "lefty")
    if padx > 0.2 or padx < -0.2 then mx = mx + padx end
    if pady > 0.2 or pady < -0.2 then my = my + pady end

    if mx ~= 0.0 or my ~= 0.0 then
        S.idle_t = 0.0
        local len = math.sqrt(mx * mx + my * my)
        local nx = S.px + mx / len * player_speed * dt
        if not blocked(nx, S.py) then S.px = nx end
        local ny = S.py + my / len * player_speed * dt
        if not blocked(S.px, ny) then S.py = ny end
        map_clear_path(S.nav_id)
        return 0
    end

    -- Attract / autopilot: A* to the goal, re-pathing twice a second.
    S.idle_t = S.idle_t + dt
    if S.idle_t < 1.0 then
        return 0
    end
    S.repath_t = S.repath_t - dt
    if S.repath_t <= 0.0 then
        S.repath_t = 0.5
        if map_find_path(S.nav_id, S.px, S.py, S.gx, S.gy, true) then
            S.player_wp = 1.0
            S.player_wp_n = map_path_count(S.nav_id)
        else
            S.player_wp_n = 0.0
        end
    end
    return 0
end

local function step_autopilot(dt)
    if S.idle_t < 1.0 or S.player_wp_n < 1.0 then return 0 end
    if S.player_wp >= S.player_wp_n then return 0 end
    local wx = map_path_x(S.nav_id, S.player_wp)
    local wy = map_path_y(S.nav_id, S.player_wp)
    local dx = wx - S.px
    local dy = wy - S.py
    local dist = math.sqrt(dx * dx + dy * dy)
    if dist < 4.0 then
        S.player_wp = S.player_wp + 1.0
        return 0
    end
    local step = player_speed * 0.85 * dt
    if step > dist then step = dist end
    -- Same axis-separated move as the chasers: A* waypoints are cell centres,
    -- so a combined diagonal step clips wall corners on a 14px box. Sliding
    -- along the free axis lets the player make progress instead of re-pathing
    -- into the same corner forever.
    local moved = false
    local nx = S.px + dx / dist * step
    if not blocked(nx, S.py) then
        S.px = nx
        moved = true
    end
    local ny = S.py + dy / dist * step
    if not blocked(S.px, ny) then
        S.py = ny
        moved = true
    end
    if not moved then
        -- Fully wedged: force a fresh search on the next update.
        S.repath_t = 0.0
    end
    return 0
end

local function draw_actor(x, y, r, g, b, size)
    draw_quad(x - size * 0.5, y - size * 0.5, size, size, r, g, b, 1.0)
end

local function draw_path_overlay()
    if not S.show_paths then return 0 end
    -- Shared chaser field: paint each reachable cell by its cost-to-go so the
    -- gradient the chasers descend is visible (faint far, brighter near goal).
    local cols = map_cols(S.map_id)
    local rows = map_rows(S.map_id)
    local c = 0
    while c < cols do
        local r = 0
        while r < rows do
            local cost = map_flow_cost(S.map_id, c * TILE + TILE * 0.5, r * TILE + TILE * 0.5)
            if cost > 0.0 then
                local a = cost * 0.004
                if a > 0.20 then a = 0.20 end
                draw_quad(c * TILE, r * TILE, TILE, TILE, 0.25, 0.45, 0.95, a)
            end
            r = r + 1
        end
        c = c + 1
    end
    if S.player_wp_n >= 2.0 then
        local k = 0.0
        while k + 1.0 < S.player_wp_n do
            draw_line(map_path_x(S.nav_id, k), map_path_y(S.nav_id, k),
                      map_path_x(S.nav_id, k + 1.0), map_path_y(S.nav_id, k + 1.0),
                      0.35, 0.95, 0.55, 0.65)
            k = k + 1.0
        end
    end
    return 0
end

local function draw_hud()
    local vw = camera_viewport_w()
    local vh = camera_viewport_h()
    local ox = camera_x()
    local oy = camera_y()
    draw_quad(ox, oy, vw, 46.0, 0.05, 0.07, 0.12, 0.82)
    draw_text("MAZE CHASE - FLOW FIELD + A*", ox + 14.0, oy + 8.0, 0.55,
              0.90, 0.93, 1.0, 1.0)
    draw_text("ARROWS/WASD MOVE   P PATHS   ESC QUIT", ox + 14.0, oy + 28.0,
              0.42, 0.62, 0.68, 0.80, 1.0)
    local right = ox + vw - 250.0
    draw_text("CAUGHT " .. S.caught .. "   CLEAR " .. S.clears, right, oy + 8.0,
              0.5, 1.0, 0.85, 0.4, 1.0)
    draw_text("WAYPOINTS " .. (S.player_wp_n + 0.0), right, oy + 28.0, 0.45,
              0.55, 0.95, 0.75, 1.0)
    return 0
end

function update(dt)
    if S.seeded == nil then
        S.seeded = true
        reset_state()
    end

    S.frame_n = S.frame_n + 1

    if not S.inited then
        S.inited = true
        S.map_id = map_load("assets/path.json")
        if S.map_id == 0 then
            draw_text("PATH MAP LOAD FAILED", 20.0, 20.0, 0.8, 1.0, 0.4, 0.4, 1.0)
            return 0
        end
        -- A Tilemap owns exactly one A* result buffer, so give the player and
        -- every chaser their own instance; otherwise each search overwrites
        -- the waypoints the other actors are currently following.
        S.nav_id = map_load("assets/path.json")
        if S.nav_id == 0 then S.nav_id = S.map_id end
        read_spawns()
        S.spawns_read = true
        -- Chasers share the flow field on S.map_id, so they need no per-actor
        -- Tilemap instance; only remember each spawn cell for resets.
        local n = ent_count(TAG_CHASER)
        local i = 0
        while i < n do
            local id = ent_at(i, TAG_CHASER)
            ent_set_num(id, 5, ent_num(id, 0))
            ent_set_num(id, 6, ent_num(id, 1))
            i = i + 1
        end
        S.px = S.sx
        S.py = S.sy
        camera_smoothing(9.0)
        camera_deadzone(0.0, 0.0)
        camera_set_bounds(0.0, 0.0, map_cols(S.map_id) * TILE, map_rows(S.map_id) * TILE)
        camera_snap(S.px, S.py)
    end

    if input_key_pressed("escape") or input_pad_pressed(0, "back") then
        window_quit()
        return 0
    end
    if input_key_pressed("p") then
        S.show_paths = not S.show_paths
    end

    S.run_t = S.run_t + dt
    step_player(dt)
    step_autopilot(dt)
    step_chasers(dt)

    -- Reached the goal tile?
    if math.abs(S.px - S.gx) < TILE * 0.6 and math.abs(S.py - S.gy) < TILE * 0.6 then
        S.clears = S.clears + 1
        reset_run()
    end

    camera_follow(S.px, S.py)
    map_draw(S.map_id)

    draw_actor(S.gx, S.gy, 0.18, 0.75, 0.38, 20.0)
    local n = ent_count(TAG_CHASER)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_CHASER)
        draw_actor(ent_num(id, 0), ent_num(id, 1), 0.92, 0.30, 0.32, 16.0)
        i = i + 1
    end
    draw_actor(S.px, S.py, 0.98, 0.86, 0.32, 18.0)

    draw_path_overlay()
    draw_hud()
    return 0
end
