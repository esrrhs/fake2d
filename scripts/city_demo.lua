-- fake2d city-builder demo (--city-demo), SimCity-style.
--
-- Zone, pave and power a generated 56x42 region. Plots develop next to a
-- road connected to the west entrance, buildings grow through three levels,
-- and each month the budget, taxes and RCI demand decide what happens next.
--
-- Controls: 1..7 tools (road / housing / shops / factory / park / power /
-- bulldoze), left click or drag to build, WASD or arrows pan, mouse wheel
-- zooms, SPACE pauses, +/- changes speed, M toggles the minimap, O overlays
-- the power grid. When nobody touches the controls a deterministic "mayor"
-- AI builds a working town, so headless runs play themselves.
--
-- Art is generated in C++ (assets/city): a 12-tile terrain sheet, a 32x64
-- building atlas and the base Tiled map.

local COLS = 56
local ROWS = 42
local TILE = 32

-- Persistent state rules (docs/SCRIPTING.md): file-level locals are the only
-- storage; mutable numbers need an expression initializer ("+ 0"), tables
-- built per frame may use numeric index writes.

-- gids, matching the generated tileset order.
local G_GRASS, G_FLOWER, G_WATER, G_SAND = 1, 2, 3, 4
local G_ROAD, G_RES, G_COM, G_IND = 5, 6, 7, 8
local G_PARK, G_RUBBLE, G_FOREST, G_POWER = 9, 10, 11, 12

-- tools.
local T_ROAD, T_RES, T_COM, T_IND = 0, 1, 2, 3
local T_PARK, T_POWER, T_BULL = 4, 5, 6
local TOOL_COUNT = 7

-- building types.
local B_RES, B_COM, B_IND, B_PARK, B_POWER = 0, 1, 2, 3, 4
local TAG_BLD = 1

-- EntityStore slots.
local S_COL, S_ROW, S_TYPE, S_LEVEL = 0, 1, 2, 3
local S_POP, S_JOBS, S_POWER, S_CNCT = 4, 5, 6, 7
local S_GROW, S_DECAY, S_ABANDON, S_AGE = 8, 9, 10, 11

local ROAD_COST, ZONE_COST, PARK_COST, POWER_COST, BULL_COST = 12, 6, 40, 900, 15
local POWER_R = 12
local START_MONEY = 12000.0

-- 240 sim frames at 1x is one month (4 s at 60 fps); speed multiplies this.
local MONTH_FRAMES = 240
local TOP_H = 46
local BAR_H = 70
local INFO_W = 230
local INFO_H = 178

local map_id = 0 + 0
local tex_bld = 0 + 0
local smoke_fx = 0 + 0
local inited = false

local money = START_MONEY + 0.0
local tax = 0.09 + 0.0
local tax_drag = false
local month_num = 1 + 0
local month_frames = 0 + 0
local speed = 1 + 0
local speed_before_pause = 1 + 0

local pop = 0.0 + 0.0
local jobs = 0.0 + 0.0
local jobs_com = 0.0 + 0.0
local jobs_ind = 0.0 + 0.0
local res_n = 0 + 0
local com_n = 0 + 0
local ind_n = 0 + 0
local plant_n = 0 + 0
local park_n = 0 + 0
local road_n = 0 + 0
local zone_n = 0 + 0
local happiness = 62.0 + 0.0
local dem_res = 0.4 + 0.0
local dem_com = 0.4 + 0.0
local dem_ind = 0.5 + 0.0
local income_last = 0.0 + 0.0
local expense_last = 0.0 + 0.0

local tool = T_ROAD + 0
local minimap_on = true
local overlay_power = false
local alert_code = 0 + 0
local alert_frames = 0 + 0

local idle_frames = 0 + 0
local attract = false
local ai_t = 0 + 0
local ai_actions = 0 + 0
local frame_no = 0 + 0

local ms100 = 0 + 0
local ms250 = 0 + 0
local ms500 = 0 + 0
local ms1000 = 0 + 0

local cam_left = 0.0 + 0.0
local cam_top = 0.0 + 0.0
local inv_zoom = 1.0 + 0.0
local vw = 960.0 + 0.0
local vh = 540.0 + 0.0
local mouse_x = 0.0 + 0.0
local mouse_y = 0.0 + 0.0
local hover_col = -1.0 + 0.0
local hover_row = -1.0 + 0.0

local paint_col = -1.0 + 0.0
local paint_row = -1.0 + 0.0
local painting = false

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

local function tool_cost(t)
    if t == T_ROAD then return ROAD_COST end
    if t == T_RES or t == T_COM or t == T_IND then return ZONE_COST end
    if t == T_PARK then return PARK_COST end
    if t == T_POWER then return POWER_COST end
    return BULL_COST
end

local function tool_name(t)
    if t == T_ROAD then return "ROAD" end
    if t == T_RES then return "HOUSING" end
    if t == T_COM then return "SHOPS" end
    if t == T_IND then return "FACTORY" end
    if t == T_PARK then return "PARK" end
    if t == T_POWER then return "POWER" end
    return "CLEAR"
end

local function tool_color(t)
    if t == T_ROAD then return 0.36, 0.38, 0.42 end
    if t == T_RES then return 0.36, 0.72, 0.40 end
    if t == T_COM then return 0.32, 0.62, 0.82 end
    if t == T_IND then return 0.78, 0.62, 0.32 end
    if t == T_PARK then return 0.44, 0.78, 0.38 end
    if t == T_POWER then return 0.95, 0.78, 0.24 end
    return 0.84, 0.36, 0.32
end

local function zone_gid(t)
    if t == T_RES then return G_RES end
    if t == T_COM then return G_COM end
    if t == T_IND then return G_IND end
    return G_PARK
end

local function zone_type_from_gid(g)
    if g == G_RES then return B_RES end
    if g == G_COM then return B_COM end
    if g == G_IND then return B_IND end
    if g == G_PARK then return B_PARK end
    if g == G_POWER then return B_POWER end
    return -1
end

local function btype_name(t)
    if t == B_RES then return "HOUSING" end
    if t == B_COM then return "SHOPS" end
    if t == B_IND then return "FACTORY" end
    if t == B_PARK then return "PARK" end
    return "POWER"
end

local function alert_text(code)
    if code == 1 then return "NOT ENOUGH FUNDS" end
    if code == 2 then return "CANNOT BUILD THERE" end
    if code == 3 then return "WATERFRONT - NO CONSTRUCTION" end
    if code == 4 then return "NEW BUILDINGS NEED POWER" end
    if code == 5 then return "ROADS ARE NOT CONNECTED TO THE ENTRANCE" end
    if code == 6 then return "BUDGET DEFICIT - CONSIDER RAISING TAXES" end
    if code == 7 then return "POWER PLANT ONLINE" end
    if code == 8 then return "ROADS CONNECT THE TOWN" end
    return ""
end

local function notify(code)
    alert_code = code
    alert_frames = 170
end

-- Screen-space HUD helpers: the engine draws everything in world space, so
-- every panel coordinate is projected through the camera at the current zoom.
local function hquad(x, y, w, h, r, g, b, a)
    draw_quad(cam_left + x * inv_zoom, cam_top + y * inv_zoom,
              w * inv_zoom, h * inv_zoom, r, g, b, a)
end

local function hrect(x, y, w, h, r, g, b, a)
    draw_rect_outline(cam_left + x * inv_zoom, cam_top + y * inv_zoom,
                      w * inv_zoom, h * inv_zoom, r, g, b, a)
end

local function htext(text, x, y, s, r, g, b, a)
    draw_text(text, cam_left + x * inv_zoom, cam_top + y * inv_zoom,
              s * inv_zoom, r, g, b, a)
end

local function htext_c(text, cx, y, s, r, g, b, a)
    local w = text_width(text, s)
    htext(text, cx - w * 0.5, y, s, r, g, b, a)
end

local function fmt_int(v)
    return string.format("%d", math.floor(v + 0.5))
end

local function fmt_money(v)
    local n = math.floor(math.abs(v) + 0.5)
    local sign = "$"
    if v < 0.0 then sign = "-$" end
    return sign .. fmt_int(n)
end

local function in_rect(mx, my, x, y, w, h)
    return mx >= x and mx <= x + w and my >= y and my <= y + h
end

-- ---------------------------------------------------------------------------
-- map helpers
-- ---------------------------------------------------------------------------

local function is_terrain(g)
    return g == G_GRASS or g == G_FLOWER or g == G_SAND or g == G_FOREST or g == G_RUBBLE
end

local function can_place(c, r, t)
    if c < 0 or r < 0 or c >= COLS or r >= ROWS then return false end
    local g = tile_at(c, r)
    if t == T_BULL then
        return g ~= G_WATER and g ~= 0
    end
    if g == G_WATER or g == G_ROAD or g == G_RES or g == G_COM or
       g == G_IND or g == G_PARK or g == G_POWER then
        return false
    end
    return is_terrain(g)
    end

local function find_building(c, r)
    local n = ent_count(TAG_BLD)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_BLD)
        if id ~= 0 then
            if ent_num(id, S_COL) == c and ent_num(id, S_ROW) == r then
                return id
            end
        end
        i = i + 1
    end
    return 0
end

local function tier_pop(ty, lv)
    if ty == B_RES then
        if lv >= 3 then return 120.0 end
        if lv == 2 then return 48.0 end
        return 16.0
    end
    return 0.0
end

local function tier_jobs(ty, lv)
    if ty == B_COM then
        if lv >= 3 then return 72.0 end
        if lv == 2 then return 30.0 end
        return 10.0
    end
    if ty == B_IND then
        if lv >= 3 then return 88.0 end
        if lv == 2 then return 38.0 end
        return 14.0
    end
    if ty == B_POWER then return 8.0 end
    return 0.0
end

local function spawn_building(c, r, ty, lv)
    local id = ent_create(TAG_BLD)
    if id == 0 then return 0 end
    ent_set_num(id, S_COL, c)
    ent_set_num(id, S_ROW, r)
    ent_set_num(id, S_TYPE, ty)
    ent_set_num(id, S_LEVEL, lv)
    ent_set_num(id, S_POP, 0.0)
    ent_set_num(id, S_JOBS, 0.0)
    ent_set_num(id, S_POWER, 0.0)
    ent_set_num(id, S_CNCT, 0.0)
    ent_set_num(id, S_GROW, 0.0)
    ent_set_num(id, S_DECAY, 0.0)
    ent_set_num(id, S_ABANDON, 0.0)
    ent_set_num(id, S_AGE, 0.0)
    return id
end

local function try_build(c, r, t, quiet)
    if not can_place(c, r, t) then
        if not quiet then
            local g = tile_at(c, r)
            if g == G_WATER then notify(3) else notify(2) end
            audio_play("city_error", 0.4)
        end
        return false
    end
    if t ~= T_BULL then
        if find_building(c, r) ~= 0 then
            if not quiet then notify(2); audio_play("city_error", 0.4) end
            return false
        end
    end
    local cost = tool_cost(t)
    if money < cost then
        if not quiet then notify(1); audio_play("city_error", 0.4) end
        return false
    end

    if t == T_ROAD then
        if tile_at(c, r) == G_ROAD then return false end
        money = money - cost
        map_set_tile(map_id, c, r, G_ROAD)
        road_n = road_n + 1
        if not quiet then audio_play("city_build", 0.35) end
        return true
    elseif t == T_BULL then
        local bid = find_building(c, r)
        if bid ~= 0 then ent_destroy(bid) end
        local g = tile_at(c, r)
        if g == G_GRASS or g == G_FLOWER then
            if bid == 0 then return true end
        end
        money = money - cost
        map_set_tile(map_id, c, r, G_GRASS)
        if not quiet then audio_play("city_bulldoze", 0.4) end
        return true
    elseif t == T_PARK then
        money = money - cost
        map_set_tile(map_id, c, r, G_PARK)
        spawn_building(c, r, B_PARK, 1)
        park_n = park_n + 1
        if not quiet then audio_play("city_zone", 0.4) end
        return true
    elseif t == T_POWER then
        money = money - cost
        map_set_tile(map_id, c, r, G_POWER)
        spawn_building(c, r, B_POWER, 1)
        plant_n = plant_n + 1
        if not quiet then
            audio_play("city_power", 0.45)
            notify(7)
        end
        return true
    end

    money = money - cost
    map_set_tile(map_id, c, r, zone_gid(t))
    zone_n = zone_n + 1
    if not quiet then audio_play("city_zone", 0.35) end
    return true
end

-- ---------------------------------------------------------------------------
-- monthly simulation
-- ---------------------------------------------------------------------------

local function build_road_reach()
    local reach = {}
    local queue = {}
    local head = 0
    local tail = 0
    road_n = 0
    local r = 0
    while r < ROWS do
        local c = 0
        while c < COLS do
            if tile_at(c, r) == G_ROAD then
                road_n = road_n + 1
            end
            c = c + 1
        end
        r = r + 1
    end
    local start = idx(0, 21)
    reach[start] = 1
    queue[tail] = start
    tail = tail + 1
    while head < tail do
        local cur = queue[head]
        head = head + 1
        local cc = cur % COLS
        local rr = math.floor(cur / COLS)
        if cc > 0 and reach[idx(cc - 1, rr)] == nil and tile_at(cc - 1, rr) == G_ROAD then
            local ni = idx(cc - 1, rr)
            reach[ni] = 1
            queue[tail] = ni
            tail = tail + 1
        end
        if cc < COLS - 1 and reach[idx(cc + 1, rr)] == nil and tile_at(cc + 1, rr) == G_ROAD then
            local ni = idx(cc + 1, rr)
            reach[ni] = 1
            queue[tail] = ni
            tail = tail + 1
        end
        if rr > 0 and reach[idx(cc, rr - 1)] == nil and tile_at(cc, rr - 1) == G_ROAD then
            local ni = idx(cc, rr - 1)
            reach[ni] = 1
            queue[tail] = ni
            tail = tail + 1
        end
        if rr < ROWS - 1 and reach[idx(cc, rr + 1)] == nil and tile_at(cc, rr + 1) == G_ROAD then
            local ni = idx(cc, rr + 1)
            reach[ni] = 1
            queue[tail] = ni
            tail = tail + 1
        end
    end
    return reach
end

local function build_power_grid()
    local grid = {}
    plant_n = 0
    local n = ent_count(TAG_BLD)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_BLD)
        if id ~= 0 and ent_num(id, S_TYPE) == B_POWER then
            plant_n = plant_n + 1
            local pc = ent_num(id, S_COL)
            local pr = ent_num(id, S_ROW)
            local dr = -POWER_R
            while dr <= POWER_R do
                local dc = -POWER_R
                while dc <= POWER_R do
                    if math.abs(dc) + math.abs(dr) <= POWER_R then
                        local tc = pc + dc
                        local tr = pr + dr
                        if tc >= 0 and tr >= 0 and tc < COLS and tr < ROWS then
                            grid[idx(tc, tr)] = 1
                        end
                    end
                    dc = dc + 1
                end
                dr = dr + 1
            end
        end
        i = i + 1
    end
    return grid
end

local function update_buildings(reach, power)
    pop = 0.0
    jobs_com = 0.0
    jobs_ind = 0.0
    res_n = 0
    com_n = 0
    ind_n = 0
    park_n = 0
    local i = ent_count(TAG_BLD) - 1
    while i >= 0 do
        local id = ent_at(i, TAG_BLD)
        if id ~= 0 then
            local c = ent_num(id, S_COL)
            local rr = ent_num(id, S_ROW)
            local ty = ent_num(id, S_TYPE)
            if ty == B_PARK then
                park_n = park_n + 1
            elseif ty ~= B_POWER then
                local powered = power[idx(c, rr)] ~= nil
                local connected = false
                if c > 0 and tile_at(c - 1, rr) == G_ROAD and reach[idx(c - 1, rr)] ~= nil then connected = true end
                if c < COLS - 1 and tile_at(c + 1, rr) == G_ROAD and reach[idx(c + 1, rr)] ~= nil then connected = true end
                if rr > 0 and tile_at(c, rr - 1) == G_ROAD and reach[idx(c, rr - 1)] ~= nil then connected = true end
                if rr < ROWS - 1 and tile_at(c, rr + 1) == G_ROAD and reach[idx(c, rr + 1)] ~= nil then connected = true end

                ent_set_num(id, S_POWER, powered and 1.0 or 0.0)
                ent_set_num(id, S_CNCT, connected and 1.0 or 0.0)

                local age = ent_num(id, S_AGE)
                local lv = ent_num(id, S_LEVEL)
                local abandon = ent_num(id, S_ABANDON)
                local grow = ent_num(id, S_GROW)
                local decay = ent_num(id, S_DECAY)
                if age < 1.0 then
                    age = 1.0
                    ent_set_num(id, S_AGE, age)
                end

                if abandon > 0.0 then
                    if powered and connected then
                        decay = decay - 1.0
                        if decay <= 0.0 then
                            decay = 0.0
                            abandon = 0.0
                        end
                    else
                        decay = decay + 1.0
                    end
                    if decay >= 3.0 then
                        map_set_tile(map_id, c, rr, G_RUBBLE)
                        ent_destroy(id)
                    end
                else
                    if powered and connected then
                        decay = 0.0
                        grow = grow + 1.0
                        if grow >= 3.0 and lv < 2.0 then lv = 2.0 end
                        if grow >= 9.0 and lv < 3.0 then lv = 3.0 end
                    else
                        decay = decay + 1.0
                        if decay >= 2.0 then
                            abandon = 1.0
                            lv = 1.0
                            grow = 0.0
                            decay = 1.0
                        end
                    end
                end

                ent_set_num(id, S_ABANDON, abandon)
                ent_set_num(id, S_DECAY, decay)
                ent_set_num(id, S_GROW, grow)
                ent_set_num(id, S_LEVEL, lv)
                local p = 0.0
                local j = 0.0
                if abandon <= 0.0 then
                    p = tier_pop(ty, lv)
                    j = tier_jobs(ty, lv)
                end
                ent_set_num(id, S_POP, p)
                ent_set_num(id, S_JOBS, j)
                pop = pop + p
                if ty == B_RES then
                    res_n = res_n + 1
                elseif ty == B_COM then
                    com_n = com_n + 1
                    jobs_com = jobs_com + j
                elseif ty == B_IND then
                    ind_n = ind_n + 1
                    jobs_ind = jobs_ind + j
                end
            end
        end
        i = i - 1
    end
    jobs = jobs_com + jobs_ind
end

local function update_economy()
    local income = tax * (pop * 4.5 + jobs * 2.5)
    local expense = road_n * 0.8 + plant_n * 40.0 + park_n * 8.0
    money = money + income - expense
    income_last = income
    expense_last = expense

    local pollution = ind_n * 2.0 / (1.0 + pop / 220.0)
    happiness = clampf(62.0 + math.min(park_n * 3.0, 20.0)
                       - (tax - 0.07) * 520.0 - pollution, 0.0, 100.0)
    local target_pop = jobs * 2.4 + 50.0
    dem_res = clampf((target_pop - pop) / 220.0 + 0.18 + (happiness - 52.0) / 260.0, 0.0, 1.0)
    local com_target = pop * 0.24 + 8.0
    dem_com = clampf((com_target - jobs_com) / 26.0 + 0.12, 0.0, 1.0)
    local ind_target = pop * 0.2 + 12.0
    dem_ind = clampf((ind_target - jobs_ind) / 30.0 + 0.12, 0.0, 1.0)
end

local function develop_zones(reach, power)
    local occupied = {}
    local n = ent_count(TAG_BLD)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_BLD)
        if id ~= 0 then
            occupied[idx(ent_num(id, S_COL), ent_num(id, S_ROW))] = 1
        end
        i = i + 1
    end

    zone_n = 0
    local r = 0
    while r < ROWS do
        local c = 0
        while c < COLS do
            local g = tile_at(c, r)
            local ty = zone_type_from_gid(g)
            if ty == B_RES or ty == B_COM or ty == B_IND then
                zone_n = zone_n + 1
                if occupied[idx(c, r)] == nil and power[idx(c, r)] ~= nil then
                    local connected = false
                    if c > 0 and tile_at(c - 1, r) == G_ROAD and reach[idx(c - 1, r)] ~= nil then connected = true end
                    if c < COLS - 1 and tile_at(c + 1, r) == G_ROAD and reach[idx(c + 1, r)] ~= nil then connected = true end
                    if r > 0 and tile_at(c, r - 1) == G_ROAD and reach[idx(c, r - 1)] ~= nil then connected = true end
                    if r < ROWS - 1 and tile_at(c, r + 1) == G_ROAD and reach[idx(c, r + 1)] ~= nil then connected = true end
                    if connected then
                        local demand = dem_res
                        if ty == B_COM then demand = dem_com end
                        if ty == B_IND then demand = dem_ind end
                        local chance = 0.15 + demand * 0.75
                        if rng_float() < chance then
                            spawn_building(c, r, ty, 1)
                        end
                    end
                end
            end
            c = c + 1
        end
        r = r + 1
    end
end

local function month_tick()
    month_num = month_num + 1
    local reach = build_road_reach()
    local power = build_power_grid()
    update_buildings(reach, power)
    update_economy()
    develop_zones(reach, power)

    if money < 0.0 then notify(6) end
    if pop >= 100.0 and ms100 == 0 then
        ms100 = 1
        log_info(string.format("city: population milestone 100 (month %d)", month_num))
    end
    if pop >= 250.0 and ms250 == 0 then
        ms250 = 1
        log_info(string.format("city: population milestone 250 (month %d)", month_num))
    end
    if pop >= 500.0 and ms500 == 0 then
        ms500 = 1
        log_info(string.format("city: population milestone 500 (month %d)", month_num))
    end
    if pop >= 1000.0 and ms1000 == 0 then
        ms1000 = 1
        log_info(string.format("city: population milestone 1000 (month %d)", month_num))
    end
    log_info(string.format("city: month %d pop %d jobs %d money %d",
                           month_num, math.floor(pop), math.floor(jobs),
                           math.floor(money)))
    if month_num % 6 == 0 then
        audio_play("city_month", 0.2)
    end
end

-- ---------------------------------------------------------------------------
-- attract mayor AI
-- ---------------------------------------------------------------------------

local function ai_step()
    ai_actions = ai_actions + 1

    -- Electric supply first: one central plant, then one per ~35 plots.
    local want_plants = 1 + math.floor(zone_n / 35.0)
    if plant_n < want_plants then
        if money >= POWER_COST then
            local k = 0
            while k < 80 do
                local c = 0
                local r = 0
                if plant_n == 0 then
                    c = math.floor(rng_float() * 9.0) + 12
                    r = math.floor(rng_float() * 9.0) + 16
                else
                    c = math.floor(rng_float() * 22.0) + 6
                    r = math.floor(rng_float() * 23.0) + 10
                end
                if can_place(c, r, T_POWER) and find_building(c, r) == 0 then
                    if try_build(c, r, T_POWER, true) then
                        return
                    end
                end
                k = k + 1
            end
        end
        return
    end

    -- Pick a random existing road cell in the developed centre as an anchor.
    local roads = {}
    local road_cells = 0
    local scan_r = 0
    while scan_r < ROWS do
        local scan_c = 0
        while scan_c < COLS do
            if tile_at(scan_c, scan_r) == G_ROAD and scan_c >= 6 and scan_c <= 30 and
               scan_r >= 11 and scan_r <= 32 then
                road_cells = road_cells + 1
                roads[road_cells] = idx(scan_c, scan_r)
            end
            scan_c = scan_c + 1
        end
        scan_r = scan_r + 1
    end
    if road_cells < 1 then return end
    local anchor = roads[math.floor(rng_float() * road_cells) + 1]
    local ac = anchor % COLS
    local ar = math.floor(anchor / COLS)
    local start_dir = math.floor(rng_float() * 4.0)
    local nc = -1
    local nr = -1
    local tried = 0
    while tried < 4 do
        local dir = (start_dir + tried) % 4
        local tc = ac
        local tr = ar
        if dir == 0 then tc = ac + 1 end
        if dir == 1 then tc = ac - 1 end
        if dir == 2 then tr = ar + 1 end
        if dir == 3 then tr = ar - 1 end
        if tc >= 4 and tc <= 28 and tr >= 10 and tr <= 33 and
           can_place(tc, tr, T_ROAD) and find_building(tc, tr) == 0 then
            nc = tc
            nr = tr
            tried = 4
        else
            tried = tried + 1
        end
    end
    if nc < 0 then return end

    -- One in five actions extends the street grid (more early on); the rest
    -- fill the gaps between roads with zoned plots.
    if (ai_actions % 5 == 0 and road_n < 100) or road_n < 35 then
        try_build(nc, nr, T_ROAD, true)
        return
    end

    local t = T_IND
    if dem_res >= dem_com and dem_res >= dem_ind then
        t = T_RES
    elseif dem_com >= dem_ind then
        t = T_COM
    end
    if happiness < 54.0 and park_n < 8 and ai_actions % 5 == 0 then
        t = T_PARK
    end
    try_build(nc, nr, t, true)
end

local over_hud = false

local function month_name(mo)
    if mo == 0 then return "JAN" end
    if mo == 1 then return "FEB" end
    if mo == 2 then return "MAR" end
    if mo == 3 then return "APR" end
    if mo == 4 then return "MAY" end
    if mo == 5 then return "JUN" end
    if mo == 6 then return "JUL" end
    if mo == 7 then return "AUG" end
    if mo == 8 then return "SEP" end
    if mo == 9 then return "OCT" end
    if mo == 10 then return "NOV" end
    return "DEC"
end

local function setup_scene()
    map_id = map_load("assets/city/city_map.json")
    tex_bld = sprite_load_nearest("assets/city/buildings.png")
    camera_set_bounds(0.0, 0.0, COLS * TILE, ROWS * TILE)
    camera_set_position(0.0, 21.0 * TILE - 270.0)
    camera_set_zoom(0.9)
    smoke_fx = part_create()
    part_set_lifetime(smoke_fx, 1.2, 2.4)
    part_set_speed(smoke_fx, 12, 44)
    part_set_direction(smoke_fx, -1.57, 0.6)
    part_set_size(smoke_fx, 2, 6)
    part_set_gravity(smoke_fx, 0, -6)
    part_set_drag(smoke_fx, 0.6)
    part_set_spin(smoke_fx, 1)
    part_set_color(smoke_fx, 0.58, 0.62, 0.64, 1.0, 0.0)
    audio_loop("city_theme", 0.12, 1.0)
end

local function update_camera()
    local zoom = camera_zoom()
    if input_key_down("a") or input_key_down("left") then camera_move(-9.0 / zoom, 0.0) end
    if input_key_down("d") or input_key_down("right") then camera_move(9.0 / zoom, 0.0) end
    if input_key_down("w") or input_key_down("up") then camera_move(0.0, -9.0 / zoom) end
    if input_key_down("s") or input_key_down("down") then camera_move(0.0, 9.0 / zoom) end
    local wheel = input_mouse_wheel()
    if wheel ~= 0.0 then
        zoom = clampf(zoom - wheel * 0.12, 0.6, 1.6)
        camera_set_zoom(zoom)
    end
    zoom = camera_zoom()
    cam_left = camera_screen_to_world_x(0.0, 0.0)
    cam_top = camera_screen_to_world_y(0.0, 0.0)
    inv_zoom = 1.0 / zoom
end

local function update_keys()
    local activity = false
    if input_mouse_pressed(0) or input_mouse_pressed(1) or input_mouse_pressed(2) then activity = true end
    if input_mouse_down(0) or input_mouse_down(1) then activity = true end
    if math.abs(input_mouse_dx()) + math.abs(input_mouse_dy()) > 1.0 then activity = true end
    if input_key_down("space") or input_key_down("enter") then activity = true end
    if math.abs(input_mouse_wheel()) > 0.0 then activity = true end
    if input_key_pressed("1") or input_key_pressed("2") or input_key_pressed("3") or
       input_key_pressed("4") or input_key_pressed("5") or input_key_pressed("6") or
       input_key_pressed("7") or input_key_pressed("m") or input_key_pressed("o") or
       input_key_pressed("equals") or input_key_pressed("minus") then
        activity = true
    end
    if activity then
        idle_frames = 0
        if attract then
            attract = false
            speed = 1
        end
    else
        idle_frames = idle_frames + 1
        if idle_frames > 90 then attract = true end
    end

    if input_key_pressed("1") then tool = T_ROAD end
    if input_key_pressed("2") then tool = T_RES end
    if input_key_pressed("3") then tool = T_COM end
    if input_key_pressed("4") then tool = T_IND end
    if input_key_pressed("5") then tool = T_PARK end
    if input_key_pressed("6") then tool = T_POWER end
    if input_key_pressed("7") then tool = T_BULL end
    if input_key_pressed("m") then minimap_on = not minimap_on end
    if input_key_pressed("o") then overlay_power = not overlay_power end
    if input_key_pressed("space") then
        if speed == 0 then
            speed = speed_before_pause
            if speed == 0 then speed = 1 end
        else
            speed_before_pause = speed
            speed = 0
        end
    end
    if input_key_pressed("equals") or input_key_pressed("plus") then
        if speed == 0 then speed = 2
        elseif speed < 4 then speed = speed * 2 end
    end
    if input_key_pressed("minus") then
        if speed > 1 then speed = math.floor(speed / 2) end
    end
end

local function update_sim_clock()
    if speed > 0 then
        month_frames = month_frames + speed
        if month_frames >= MONTH_FRAMES then
            month_frames = month_frames - MONTH_FRAMES
            month_tick()
        end
    end
    if attract then
        if speed ~= 4 then speed = 4 end
        ai_t = ai_t + 1
        if ai_t >= 10 then
            ai_t = 0
            ai_step()
        end
    end
end

local function update_pointer()
    local wx = camera_screen_to_world_x(mouse_x, mouse_y)
    local wy = camera_screen_to_world_y(mouse_x, mouse_y)
    hover_col = math.floor(wx / TILE)
    hover_row = math.floor(wy / TILE)

    over_hud = false
    if in_rect(mouse_x, mouse_y, 0, 0, vw, TOP_H) then over_hud = true end
    if in_rect(mouse_x, mouse_y, 0, vh - BAR_H, vw, BAR_H) then over_hud = true end
    if in_rect(mouse_x, mouse_y, vw - INFO_W - 10, TOP_H + 8, INFO_W + 10, INFO_H + 8) then over_hud = true end
    if minimap_on and in_rect(mouse_x, mouse_y, vw - 184, vh - BAR_H - 150, 176, 146) then over_hud = true end

    local pressed = input_mouse_pressed(0)
    local down = input_mouse_down(0)

    -- Speed buttons, top-right.
    local sbx = vw - 150
    local sby = 9
    local si = 0
    while si < 4 do
        if pressed and in_rect(mouse_x, mouse_y, sbx + si * 36, sby, 32, 28) then
            if si == 0 then
                if speed > 0 then speed_before_pause = speed end
                speed = 0
            elseif si == 1 then
                speed = 1
            elseif si == 2 then
                speed = 2
            else
                speed = 4
            end
        end
        si = si + 1
    end

    -- Tool palette.
    local bx = 8
    local by = vh - BAR_H + 8
    local ti = 0
    while ti < TOOL_COUNT do
        if pressed and in_rect(mouse_x, mouse_y, bx + ti * 80, by, 76, BAR_H - 16) then
            tool = ti
            audio_play("click", 0.2)
        end
        ti = ti + 1
    end

    -- Tax slider, info card.
    local ix = vw - INFO_W - 4
    local iy = TOP_H + 12
    local track_x = ix + 14
    local track_y = iy + 122
    local track_w = INFO_W - 28
    if pressed and in_rect(mouse_x, mouse_y, track_x, track_y - 6, track_w, 20) then
        tax_drag = true
    end
    if tax_drag then
        if down then
            tax = clampf((mouse_x - track_x) / track_w * 0.20, 0.0, 0.20)
        else
            tax_drag = false
        end
    end

    -- Minimap click jumps the camera.
    if minimap_on and pressed and in_rect(mouse_x, mouse_y, vw - 184, vh - BAR_H - 150, 176, 146) then
        local mx0 = vw - 180
        local my0 = vh - BAR_H - 138
        local fx = clampf((mouse_x - mx0) / 168.0, 0.0, 1.0)
        local fy = clampf((mouse_y - my0) / 126.0, 0.0, 1.0)
        camera_set_position(fx * COLS * TILE, fy * ROWS * TILE)
    end

    -- World placement: press, then drag-paint roads and zones.
    if pressed and not over_hud then
        painting = true
        paint_col = hover_col
        paint_row = hover_row
        if hover_col >= 0.0 and hover_row >= 0.0 and hover_col < COLS and hover_row < ROWS then
            try_build(hover_col, hover_row, tool, false)
            if tool == T_BULL or tool == T_PARK or tool == T_POWER then
                painting = false
            end
        else
            painting = false
        end
    end
    if painting then
        if down and not over_hud then
            if hover_col >= 0.0 and hover_row >= 0.0 and hover_col < COLS and hover_row < ROWS then
                if hover_col ~= paint_col or hover_row ~= paint_row then
                    paint_col = hover_col
                    paint_row = hover_row
                    try_build(hover_col, hover_row, tool, false)
                end
            end
        else
            painting = false
        end
    end

    if alert_frames > 0 then alert_frames = alert_frames - 1 end
    if alert_frames == 0 then alert_code = 0 end
end

local function draw_road_marks(c0, c1, r0, r1)
    local rr = r0
    while rr <= r1 do
        local cc = c0
        while cc <= c1 do
            if tile_at(cc, rr) == G_ROAD then
                local x = cc * TILE
                local y = rr * TILE
                if cc + 1 <= COLS - 1 and tile_at(cc + 1, rr) == G_ROAD then
                    draw_quad(x + 10, y + 15, 12, 2, 0.82, 0.84, 0.86, 0.55)
                end
                if rr + 1 <= ROWS - 1 and tile_at(cc, rr + 1) == G_ROAD then
                    draw_quad(x + 15, y + 10, 2, 12, 0.82, 0.84, 0.86, 0.55)
                end
            end
            cc = cc + 1
        end
        rr = rr + 1
    end
end

local function draw_power_overlay(c0, c1, r0, r1)
    local n = ent_count(TAG_BLD)
    local i = 0
    while i < n do
        local id = ent_at(i, TAG_BLD)
        if id ~= 0 and ent_num(id, S_TYPE) == B_POWER then
            local pc = ent_num(id, S_COL)
            local pr = ent_num(id, S_ROW)
            local dr = -POWER_R
            while dr <= POWER_R do
                local dc = -POWER_R
                while dc <= POWER_R do
                    if math.abs(dc) + math.abs(dr) <= POWER_R then
                        local tc = pc + dc
                        local tr = pr + dr
                        if tc >= c0 and tc <= c1 and tr >= r0 and tr <= r1 then
                            draw_quad(tc * TILE, tr * TILE, TILE, TILE,
                                      0.98, 0.82, 0.26, 0.16)
                        end
                end
                    dc = dc + 1
                end
                dr = dr + 1
            end
        end
        i = i + 1
    end
end

local function draw_buildings(c0, c1, r0, r1)
    local row_counts = {}
    local row_ids = {}
    local br = 0
    while br < ROWS do
        row_counts[br] = 0
        br = br + 1
    end
    local i = 0
    local n = ent_count(TAG_BLD)
    while i < n do
        local id = ent_at(i, TAG_BLD)
        if id ~= 0 then
            local bc = ent_num(id, S_COL)
            local brow = ent_num(id, S_ROW)
            if bc >= c0 - 1 and bc <= c1 + 1 and brow >= r0 - 1 and brow <= r1 + 1 then
                local k = row_counts[brow] + 1
                row_counts[brow] = k
                row_ids[brow * 80 + k] = id
            end
            if ent_num(id, S_TYPE) == B_IND and ent_num(id, S_ABANDON) <= 0.0 and
               ent_num(id, S_AGE) >= 1.0 and frame_no % 40 == 0 and i % 3 == 0 then
                part_emit(smoke_fx, 2, bc * TILE + 23.0, brow * TILE - 18.0)
            end
        end
        i = i + 1
    end

    br = r0
    while br <= r1 do
        local nrow = row_counts[br]
        local k = nrow
        while k >= 1 do
            local id = row_ids[br * 80 + k]
            if id ~= nil and id ~= 0 then
                local bc = ent_num(id, S_COL)
                local ty = ent_num(id, S_TYPE)
                local lv = ent_num(id, S_LEVEL)
                local x = bc * TILE
                local y = br * TILE
                local sx = 0.0
                local sy = ty * 64.0
                if ent_num(id, S_ABANDON) > 0.0 then
                    sx = 32.0
                    sy = 320.0
                elseif ent_num(id, S_AGE) < 1.0 then
                    sx = 0.0
                    sy = 320.0
                else
                    local li = math.floor(lv) - 1
                    if li < 0 then li = 0 end
                    if li > 2 then li = 2 end
                    sx = li * 32.0
                end
                sprite_draw_region(tex_bld, sx, sy, 32.0, 64.0,
                                   x, y - 32.0, 32.0, 64.0)
            end
            k = k - 1
        end
        br = br + 1
    end
end

local function draw_world()
    map_draw(map_id)

    local visible_w = vw * inv_zoom
    local visible_h = vh * inv_zoom
    local c0 = math.floor(cam_left / TILE)
    local r0 = math.floor(cam_top / TILE)
    local c1 = math.floor((cam_left + visible_w) / TILE) + 1
    local r1 = math.floor((cam_top + visible_h) / TILE) + 1
    if c0 < 0 then c0 = 0 end
    if r0 < 0 then r0 = 0 end
    if c1 > COLS - 1 then c1 = COLS - 1 end
    if r1 > ROWS - 1 then r1 = ROWS - 1 end

    draw_road_marks(c0, c1, r0, r1)
    if overlay_power or tool == T_POWER then
        draw_power_overlay(c0, c1, r0, r1)
    end
    draw_buildings(c0, c1, r0, r1)
    part_draw()

    if hover_col >= 0.0 and hover_row >= 0.0 and hover_col < COLS and hover_row < ROWS then
        local x = hover_col * TILE
        local y = hover_row * TILE
        local ok = can_place(hover_col, hover_row, tool)
        if tool == T_BULL then ok = tile_at(hover_col, hover_row) ~= G_WATER end
        if ok then
            draw_rect_outline(x, y, TILE, TILE, 0.85, 1.0, 0.55, 0.95)
        else
            draw_rect_outline(x, y, TILE, TILE, 1.0, 0.42, 0.38, 0.95)
        end
        draw_quad(x, y, TILE, TILE, 1.0, 1.0, 1.0, 0.10)
    end
end

local function draw_speed_button(x, y, w, h, value, label)
    local hovered = in_rect(mouse_x, mouse_y, x, y, w, h)
    local active = speed == value
    local r, g, b = 0.14, 0.17, 0.23
    if active then r, g, b = 0.22, 0.46, 0.56 end
    if hovered then r, g, b = r + 0.08, g + 0.08, b + 0.08 end
    hquad(x, y, w, h, r, g, b, 0.95)
    hrect(x, y, w, h, 0.42, 0.52, 0.58, 0.8)
    htext_c(label, x + w * 0.5, y + 6, 0.42, 0.92, 0.95, 0.96, 1.0)
end

local function draw_topbar()
    hquad(0.0, 0.0, vw, TOP_H, 0.055, 0.075, 0.110, 0.94)
    hquad(0.0, TOP_H - 2, vw, 2, 0.24, 0.46, 0.52, 0.9)

    htext("FAKE2D CITY", 12, 8, 0.52, 0.62, 0.92, 0.96, 1.0)
    htext("SIM", 12, 30, 0.32, 0.42, 0.60, 0.66, 0.9)

    htext("FUNDS", 178, 10, 0.30, 0.55, 0.66, 0.72, 0.9)
    htext(fmt_money(money), 178, 23, 0.50, 0.94, 0.86, 0.52, 1.0)

    local mo = (month_num - 1) % 12
    local yr = 2026 + math.floor((month_num - 1) / 12)
    htext("DATE", 314, 10, 0.30, 0.55, 0.66, 0.72, 0.9)
    htext(month_name(mo) .. " " .. fmt_int(yr), 314, 23, 0.48, 0.86, 0.92, 0.96, 1.0)

    htext("POP", 438, 10, 0.30, 0.55, 0.66, 0.72, 0.9)
    htext(fmt_int(pop), 438, 23, 0.48, 0.94, 0.94, 0.92, 1.0)
    htext("JOBS", 500, 10, 0.30, 0.55, 0.66, 0.72, 0.9)
    htext(fmt_int(jobs), 500, 23, 0.48, 0.90, 0.86, 0.60, 1.0)

    htext("R", 560, 10, 0.32, 0.44, 0.86, 0.50, 1.0)
    hquad(576, 13, 34 * dem_res, 7, 0.34, 0.78, 0.42, 0.95)
    htext("C", 560, 21, 0.32, 0.40, 0.66, 0.92, 1.0)
    hquad(576, 24, 34 * dem_com, 7, 0.34, 0.60, 0.90, 0.95)
    htext("I", 561, 32, 0.30, 0.90, 0.72, 0.36, 1.0)
    hquad(576, 35, 34 * dem_ind, 7, 0.90, 0.68, 0.30, 0.95)
    hrect(576, 13, 34, 7, 0.30, 0.36, 0.44, 0.8)
    hrect(576, 24, 34, 7, 0.30, 0.36, 0.44, 0.8)
    hrect(576, 35, 34, 7, 0.30, 0.36, 0.44, 0.8)

    htext("SPEED", vw - 202, 38, 0.28, 0.52, 0.62, 0.68, 0.8)
    local sbx = vw - 150
    local sby = 9
    draw_speed_button(sbx, sby, 32, 28, 0, "||")
    draw_speed_button(sbx + 36, sby, 32, 28, 1, "1x")
    draw_speed_button(sbx + 72, sby, 32, 28, 2, "2x")
    draw_speed_button(sbx + 108, sby, 32, 28, 4, "4x")
end

local function draw_toolbar()
    hquad(0.0, vh - BAR_H, vw, BAR_H, 0.055, 0.075, 0.110, 0.94)
    hquad(0.0, vh - BAR_H, vw, 2, 0.24, 0.46, 0.52, 0.9)
    htext("LMB BUILD   DRAG ROAD / ZONE   WASD PAN   WHEEL ZOOM   SPACE PAUSE   M MAP   O POWER",
          10, vh - BAR_H - 18, 0.36, 0.70, 0.80, 0.84, 0.85)

    local bx = 8
    local by = vh - BAR_H + 8
    local i = 0
    while i < TOOL_COUNT do
        local x = bx + i * 80
        local hovered = in_rect(mouse_x, mouse_y, x, by, 76, BAR_H - 16)
        local r, g, b = tool_color(i)
        local selected = tool == i
        local bg = 0.10
        if selected then bg = 0.20 end
        if hovered then bg = bg + 0.06 end
        hquad(x, by, 76, BAR_H - 16, bg, bg + 0.02, bg + 0.05, 0.95)
        hrect(x, by, 76, BAR_H - 16, r, g, b, selected and 1.0 or 0.42)
        hquad(x + 8, by + 8, 18, 18, r, g, b, 1.0)
        htext(tool_name(i), x + 8, by + 30, 0.40, 0.92, 0.95, 0.96, 1.0)
        htext("$" .. fmt_int(tool_cost(i)), x + 8, by + 44, 0.34, 0.68, 0.80, 0.70, 0.9)
        i = i + 1
    end
end

local function draw_info_card()
    local ix = vw - INFO_W - 4
    local iy = TOP_H + 12
    hquad(ix, iy, INFO_W, INFO_H, 0.055, 0.075, 0.110, 0.92)
    hrect(ix, iy, INFO_W, INFO_H, 0.26, 0.36, 0.42, 0.9)
    htext("BUDGET", ix + 12, iy + 10, 0.42, 0.60, 0.90, 0.94, 1.0)
    htext("INCOME", ix + 12, iy + 36, 0.32, 0.60, 0.70, 0.74, 0.9)
    htext(fmt_money(income_last) .. "/mo", ix + 118, iy + 34, 0.36, 0.60, 0.90, 0.58, 1.0)
    htext("UPKEEP", ix + 12, iy + 56, 0.32, 0.60, 0.70, 0.74, 0.9)
    htext("-" .. fmt_money(expense_last) .. "/mo", ix + 118, iy + 54, 0.36, 0.92, 0.62, 0.50, 1.0)
    htext("ROADS", ix + 12, iy + 78, 0.30, 0.55, 0.64, 0.68, 0.9)
    htext(fmt_int(road_n), ix + 170, iy + 76, 0.34, 0.78, 0.86, 0.88, 1.0)
    htext("HAPPY", ix + 12, iy + 96, 0.30, 0.55, 0.64, 0.68, 0.9)
    local hr = 0.50 + (100.0 - happiness) * 0.004
    local hg = 0.86 - (100.0 - happiness) * 0.004
    htext(fmt_int(happiness), ix + 170, iy + 94, 0.34, hr, hg, 0.50, 1.0)

    htext("TAX " .. fmt_int(tax * 100.0) .. "%", ix + 12, iy + 116, 0.34,
          0.78, 0.86, 0.90, 1.0)
    local track_x = ix + 14
    local track_y = iy + 122
    local track_w = INFO_W - 28
    hquad(track_x, track_y, track_w, 6, 0.14, 0.18, 0.24, 1.0)
    hquad(track_x, track_y, track_w * (tax / 0.20), 6, 0.32, 0.66, 0.74, 1.0)
    hquad(track_x + track_w * (tax / 0.20) - 3, track_y - 3, 7, 12, 0.92, 0.94, 0.96, 1.0)
    htext("0%", ix + 12, iy + 132, 0.28, 0.52, 0.60, 0.64, 0.8)
    htext("20%", ix + INFO_W - 44, iy + 132, 0.28, 0.52, 0.60, 0.64, 0.8)
    htext("ZONES " .. fmt_int(zone_n) .. "   POWER " .. fmt_int(plant_n),
          ix + 12, iy + 150, 0.30, 0.58, 0.68, 0.72, 0.9)
end

local function draw_alert()
    if alert_frames > 0 and alert_code ~= 0 then
        local text = alert_text(alert_code)
        local w = text_width(text, 0.45) + 36
        local bx = (vw - w) * 0.5
        hquad(bx, TOP_H + 10, w, 30, 0.32, 0.10, 0.08, 0.92)
        hrect(bx, TOP_H + 10, w, 30, 0.95, 0.42, 0.34, 0.95)
        htext_c(text, vw * 0.5, TOP_H + 18, 0.45, 1.0, 0.86, 0.80, 1.0)
    end
end

local function draw_minimap()
    local mmx = vw - 180
    local mmy = vh - BAR_H - 138
    hquad(mmx - 4, mmy - 20, 176, 146, 0.04, 0.06, 0.09, 0.92)
    hrect(mmx - 4, mmy - 20, 176, 146, 0.28, 0.40, 0.46, 0.9)
    htext("CITY MAP", mmx + 4, mmy - 16, 0.34, 0.62, 0.82, 0.88, 1.0)
    local mw = 3
    local r = 0
    while r < ROWS do
        local c = 0
        while c < COLS do
            local g = tile_at(c, r)
            local cr, cg, cb = 0.22, 0.42, 0.20
            if g == G_WATER then cr, cg, cb = 0.16, 0.36, 0.56 end
            if g == G_SAND then cr, cg, cb = 0.60, 0.55, 0.38 end
            if g == G_ROAD then cr, cg, cb = 0.28, 0.30, 0.34 end
            if g == G_RES then cr, cg, cb = 0.42, 0.70, 0.44 end
            if g == G_COM then cr, cg, cb = 0.36, 0.60, 0.78 end
            if g == G_IND then cr, cg, cb = 0.70, 0.58, 0.30 end
            if g == G_PARK then cr, cg, cb = 0.32, 0.62, 0.30 end
            if g == G_FOREST then cr, cg, cb = 0.16, 0.34, 0.16 end
            if g == G_POWER then cr, cg, cb = 0.88, 0.76, 0.26 end
            if g == G_RUBBLE then cr, cg, cb = 0.42, 0.38, 0.30 end
            hquad(mmx + c * mw, mmy + r * mw, mw, mw, cr, cg, cb, 1.0)
            c = c + 1
        end
        r = r + 1
    end
    local vx = mmx + cam_left / (COLS * TILE) * 168.0
    local vy = mmy + cam_top / (ROWS * TILE) * 126.0
    local vw2 = (vw * inv_zoom) / (COLS * TILE) * 168.0
    local vh2 = (vh * inv_zoom) / (ROWS * TILE) * 126.0
    hrect(vx, vy, vw2, vh2, 0.95, 0.94, 0.82, 0.9)
end

local function draw_tooltip()
    if over_hud then return end
    if hover_col < 0.0 or hover_col >= COLS or hover_row < 0.0 or hover_row >= ROWS then return end
    local bid = find_building(hover_col, hover_row)
    if bid == 0 then return end
    local ty = ent_num(bid, S_TYPE)
    local lv = ent_num(bid, S_LEVEL)
    local p = ent_num(bid, S_POP)
    local j = ent_num(bid, S_JOBS)
    local powered = ent_num(bid, S_POWER) > 0.0
    local connected = ent_num(bid, S_CNCT) > 0.0
    local abandoned = ent_num(bid, S_ABANDON) > 0.0
    local tx = mouse_x + 16
    local tyy = mouse_y + 16
    if tx + 170 > vw then tx = mouse_x - 186 end
    if tyy + 74 > vh then tyy = mouse_y - 90 end
    hquad(tx, tyy, 170, 74, 0.05, 0.07, 0.10, 0.94)
    hrect(tx, tyy, 170, 74, 0.32, 0.46, 0.52, 0.9)
    htext(btype_name(ty) .. "  LV" .. fmt_int(lv), tx + 8, tyy + 7, 0.38,
          0.90, 0.94, 0.94, 1.0)
    htext("POP " .. fmt_int(p) .. "   JOBS " .. fmt_int(j), tx + 8, tyy + 27, 0.32,
          0.70, 0.82, 0.84, 1.0)
    local state = "OK"
    if abandoned then state = "ABANDONED" end
    if not powered then state = "NO POWER" end
    if not connected then state = "NO ROAD" end
    htext(state, tx + 8, tyy + 46, 0.32,
          abandoned and 0.94 or 0.70, 0.62, 0.50, 1.0)
end

local function draw_hud()
    draw_topbar()
    draw_toolbar()
    draw_info_card()
    draw_alert()
    if minimap_on then draw_minimap() end
    draw_tooltip()
end

function update(dt)
    frame_no = frame_no + 1
    if not inited then
        inited = true
        setup_scene()
    end

    vw = camera_viewport_w()
    vh = camera_viewport_h()
    mouse_x = input_mouse_x()
    mouse_y = input_mouse_y()

    if map_id == 0 or tex_bld == 0 then
        draw_text("CITY ASSETS FAILED TO LOAD", 40.0, 60.0, 0.7, 1.0, 0.4, 0.4, 1.0)
        return 0
    end

    update_camera()
    update_keys()
    update_sim_clock()
    update_pointer()
    draw_world()
    draw_hud()
    return 0
end
