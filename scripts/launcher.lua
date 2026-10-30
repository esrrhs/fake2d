-- fake2d demo launcher -- pick a demo, press ENTER, and the host restarts into it.
--
-- The catalogue is not this file's business: it lives in include/fake2d/demos.h
-- and is the same table the command line parses, read back here through
-- demo_count() / demo_flag() / demo_script() / demo_category(). That is what
-- keeps the menu honest -- it can only ever offer a demo the host knows how to
-- start, and a demo added to the catalogue shows up here automatically.
-- Presentation (title, blurb, cover art) is this file's business, keyed by flag.
--
-- Choosing a demo calls app_launch(flag): the host re-execs itself with
-- `--<flag>-demo`, so each demo gets a clean process with its own asset and
-- audio setup. Changing game later means coming back here and picking again.
--
-- State lives in a NativeObject, not in file-level scalars: file-level locals
-- are const bindings, so cross-frame mutable state has to be a host-side object
-- whose fields are read and written like an ordinary table (S.sel = S.sel + 1).
--
-- Controls: UP/DOWN or mouse to select, ENTER or click to launch, TAB cycles
-- the category filter, PAGEUP/PAGEDOWN and the wheel scroll, ESC quits.

local W, H = 960, 540

local ROW_H = 24.0
local LIST_X = 24.0
local LIST_W = 288.0
local LIST_TOP = 112.0
local LIST_ROWS = 15

local PANEL_X = 332.0
local PANEL_W = 604.0

-- Category filter values; index into CATS.
local CATS_ALL, CATS_GAME, CATS_ENGINE = 0, 1, 2

local CATS = {
    {id = "all", label = "ALL"},
    {id = "game", label = "GAMES"},
    {id = "engine", label = "ENGINE"},
}

-- Presentation records, in the order the menu lists them. `cover` is shipped
-- art from assets/; a missing or unreadable cover falls back to a flat panel.
local INFO = {
    {flag = "breakout", title = "BRICK BREAKER", cat = CATS_GAME,
     blurb = "Neon arcade breakout with an illustrated brick, paddle and coin.",
     controls = "MOUSE / ARROWS move   SPACE launch",
     cover = "assets/arcade/bg.png"},
    {flag = "angry-birds", title = "ANGRY BIRDS", cat = CATS_GAME,
     blurb = "Slingshot physics: drag the bird, topple the towers, pop the pigs.",
     controls = "MOUSE drag + release   R restart",
     cover = "assets/art/ab_sky.png"},
    {flag = "limbo", title = "LIMBO SANDBOX", cat = CATS_GAME,
     blurb = "Jointed ragdoll, rope bridge and buoyant water in silhouette.",
     controls = "A/D move  SPACE jump  E rope  K sever  R reset",
     cover = "assets/art/limbo_bg.png"},
    {flag = "platformer", title = "PLATFORMER", cat = CATS_GAME,
     blurb = "Scrolling level: coyote time, jump buffering, one-way ledges, coins.",
     controls = "ARROWS move   SHIFT run   SPACE jump",
     cover = "assets/art/night_sky.png"},
    {flag = "mario", title = "WORLD 1-1", cat = CATS_GAME,
     blurb = "Super Mario Bros 1-1: two forms, goombas, brick shatter, flagpole.",
     controls = "ARROWS move   SHIFT run   SPACE jump   ESC quit",
     cover = "assets/art/mario_sky.png"},
    {flag = "beat-em-up", title = "DINO BRAWL", cat = CATS_GAME,
     blurb = "2.5D belt-scroller: combos, hit-stop, waves and a boss on CC0 art.",
     controls = "ARROWS move   J punch   K kick   L spin",
     cover = "assets/brawler/bg/city_scitymid.png"},
    {flag = "shmup", title = "VERTICAL SHOOTER", cat = CATS_GAME,
     blurb = "Raiden-style: three upgradeable weapons, bombs, waves and a boss.",
     controls = "ARROWS fly   Z fire   X bomb",
     cover = "assets/shmup/bg/desert.png"},
    {flag = "snooker", title = "SNOOKER", cat = CATS_GAME,
     blurb = "Full frame rules on a purpose-built billiards solver with spin.",
     controls = "MOUSE aim + charge   click spin pad   R new frame",
     cover = "assets/art/snooker_table.png"},
    {flag = "contra", title = "RUN AND GUN", cat = CATS_GAME,
     blurb = "Jungle stage, weapon capsules, turrets and a fortress boss.",
     controls = "ARROWS move   SPACE jump   Z fire",
     cover = "assets/art/contra_jungle.png"},
    {flag = "city", title = "CITY BUILDER", cat = CATS_GAME,
     blurb = "Zone, power and pave a growing town; monthly economy and demand.",
     controls = "1-7 tools   LMB paint   WASD pan   wheel zoom",
     cover = "assets/city/city_tiles.png"},
    {flag = "rts", title = "REAL-TIME STRATEGY", cat = CATS_GAME,
     blurb = "Harvest ore, build a base, train an army and break the enemy's.",
     controls = "LMB select   RMB order   1-5 build   6-9 units",
     cover = "assets/rts/tiles.png"},
    {flag = "vn", title = "DATING SIM", cat = CATS_GAME,
     blurb = "Fourteen days, four heroines, illustrated portraits and endings.",
     controls = "MOUSE choose   clicks advance dialogue",
     cover = "assets/vn/bg/gate.png"},
    {flag = "light", title = "2D LIGHTING", cat = CATS_ENGINE,
     blurb = "Moving lights with projected shadows: a carried lantern, torches, an orbiting orb, and a physics crate that drags its shadow.",
     controls = "LEFT/RIGHT walk   SPACE light burst   L toggle",
     cover = "assets/art/limbo_ground.png"},
    {flag = "scene", title = "SCENE GRAPH", cat = CATS_ENGINE,
     blurb = "C++ node tree: transforms, y-sorted batches and camera culling.",
     controls = "ARROWS pan the camera",
     cover = "assets/art/forest_canopy.png"},
    {flag = "slope", title = "SLOPE LAB", cat = CATS_ENGINE,
     blurb = "Tilemap slope traversal at any rise, with live ground probing.",
     controls = "A/D move   SPACE jump   V toggle surface overlay",
     cover = "assets/art/pine_top.png"},
    {flag = "path", title = "A* PATHFINDING", cat = CATS_ENGINE,
     blurb = "Generated maze, staggered chasers and an autopilot mode.",
     controls = "ARROWS move   P toggle autopilot",
     cover = "assets/art/stone_tex.png"},
    {flag = "map", title = "TILED OBJECTS", cat = CATS_ENGINE,
     blurb = "Tiled object layers: spawn points, triggers and per-object props.",
     controls = "ARROWS move the probe",
     cover = "assets/art/rail_wood.png"},
    {flag = "tile", title = "ANIMATED TILES", cat = CATS_ENGINE,
     blurb = "Tiled tileset cycles driven by the engine clock: water and torches.",
     controls = "watch the pond",
     cover = "assets/art/question_block.png"},
    {flag = "rt", title = "RENDER TARGETS", cat = CATS_ENGINE,
     blurb = "Bake-once offscreen canvas beside an accumulating motion trail.",
     controls = "MOUSE draws the trail",
     cover = "assets/art/moon.png"},
    {flag = "ui", title = "IMMEDIATE UI I", cat = CATS_ENGINE,
     blurb = "Sliders, checkboxes, text fields and scroll areas.",
     controls = "MOUSE operate the widgets",
     cover = "assets/art/arcade_bg.png"},
    {flag = "ui2", title = "IMMEDIATE UI II", cat = CATS_ENGINE,
     blurb = "Progress bars, radio groups and combo popups.",
     controls = "MOUSE operate the widgets",
     cover = "assets/art/baize.png"},
    {flag = "ui3", title = "IMMEDIATE UI III", cat = CATS_ENGINE,
     blurb = "A draggable window, a tab bar and collapsible trees.",
     controls = "MOUSE drag the window titlebar",
     cover = "assets/art/ore.png"},
    {flag = "fog", title = "FOG OF WAR", cat = CATS_ENGINE,
     blurb = "Cumulative exploration mask multiplied over the world, plus minimap.",
     controls = "WASD explore",
     cover = "assets/art/limbo_tree.png"},
    {flag = "post", title = "POST PROCESSING", cat = CATS_ENGINE,
     blurb = "Desaturation, lift, vignette and scanlines over the whole world.",
     controls = "ARROWS pan   sliders tune the grade",
     cover = "assets/art/goal_flag.png"},
    {flag = "fx", title = "MULTI-PASS POST FX", cat = CATS_ENGINE,
     blurb = "Downsampled separable Gaussian bloom, wave wobble, split screen.",
     controls = "keys toggle each pass",
     cover = "assets/art/pipe.png"},
    {flag = "audio", title = "AUDIO MIXER", cat = CATS_ENGINE,
     blurb = "Three gain buses, cross-fades, a frozen mix and a 24-segment VU.",
     controls = "MOUSE drag the faders",
     cover = "assets/art/explosion.png"},
    {flag = "spatial", title = "POSITIONAL AUDIO", cat = CATS_ENGINE,
     blurb = "Four world emitters with distance rolloff, stereo pan and range rings.",
     controls = "WASD walk the listener",
     cover = "assets/art/ab_sling.png"},
    {flag = "text", title = "TEXT AND PLATFORM", cat = CATS_ENGINE,
     blurb = "UTF-8/CJK layout, live language switching, save v2 and Steam state.",
     controls = "keys switch language and exercise saves",
     cover = "assets/art/ab_ground.png"},
}

-- Cross-frame state: selection, category filter, scroll offset, cursor.
-- File-level locals are const bindings, so mutable state lives in a
-- NativeObject whose fields read and write like an ordinary table.
local S = new_global_obj("launcher", "state")

--- A numeric environment variable, or -1 when unset. Headless traces drive the
--- same selection, filter and click paths a player uses with a mouse.
local function env_num(name)
    local v = os.getenv(name)
    if v == nil then
        return -1
    end
    return tonumber(v)
end

local PICK = env_num("LAUNCHER_TEST_PICK")
local TEST_CAT = env_num("LAUNCHER_TEST_CAT")
local TEST_MX = env_num("LAUNCHER_TEST_MX")
local TEST_MY = env_num("LAUNCHER_TEST_MY")
local TEST_CLICK = env_num("LAUNCHER_TEST_CLICK")

--- The presentation record for a flag, or nil when the catalogue has an entry
--- this file does not describe yet (it still gets listed, just plainly).
local function info_of(flag)
    for i = 1, #INFO do
        if INFO[i].flag == flag then
            return INFO[i]
        end
    end
    return nil
end

--- Does catalogue index i pass the current filter?
local function matches(i)
    if S.cat == CATS_ALL then
        return true
    end
    local want = "game"
    if S.cat == CATS_ENGINE then
        want = "engine"
    end
    return demo_category(i) == want
end

--- Catalogue indices the filter currently shows, in catalogue order. Rebuilt
--- every frame: per-frame tables are arena memory, so this is free and there is
--- no cache to invalidate when the filter changes. The count rides along on the
--- object because the caller needs both and the loop already computes it; `#`
--- works on a table like this, it is just a second pass over it.
local function visible()
    local v = {}
    local n = 0
    for i = 0, S.total - 1 do
        if matches(i) then
            n = n + 1
            v[n] = i
        end
    end
    S.nvis = n
    return v
end

--- Move a position within the visible list, wrapping.
local function step(pos, dir, n)
    if n <= 0 then
        return 0
    end
    local p = pos + dir
    while p < 0 do
        p = p + n
    end
    return p % n
end

--- Keep the scroll offset so the selection stays inside the visible window.
local function clamp_scroll(n)
    if S.sel < S.top then
        S.top = S.sel
    end
    if S.sel > S.top + LIST_ROWS - 1 then
        S.top = S.sel - (LIST_ROWS - 1)
    end
    if S.top > n - LIST_ROWS then
        S.top = n - LIST_ROWS
    end
    if S.top < 0 then
        S.top = 0
    end
end

--- The filter changed: the visible list is a different set, so start over.
local function refilter()
    S.sel = 0
    S.top = 0
end

local function label(text, x, y, scale, r, g, b, a)
    draw_text(text, x, y, scale, r, g, b, a)
end

--- A card background: the demo's shipped cover art, or a flat panel without it.
local function card(tex, x, y, w, h, r, g, b)
    if tex ~= 0 then
        sprite_draw(tex, x, y, w, h)
        -- Dim the art so the text over it stays readable.
        draw_quad(x, y, w, h, r, g, b, 0.45)
    else
        draw_quad(x, y, w, h, r, g, b, 1.0)
    end
end

local function draw_list(v, n)
    for row = 0, LIST_ROWS - 1 do
        local pos = S.top + row
        if pos < n then
            local i = v[pos + 1]
            local y = LIST_TOP + row * ROW_H
            local info = info_of(demo_flag(i))
            local name = string.upper(demo_flag(i))
            local kind = demo_category(i)
            if info ~= nil then
                name = info.title
                if info.cat == CATS_GAME then
                    kind = "game"
                else
                    kind = "engine"
                end
            end
            local on = pos == S.sel
            if on then
                draw_quad(LIST_X, y - 3.0, LIST_W, ROW_H - 2.0, 0.20, 0.42, 0.52, 1.0)
                draw_quad(LIST_X, y - 3.0, 3.0, ROW_H - 2.0, 0.45, 0.90, 1.0, 1.0)
            elseif row % 2 == 0 then
                draw_quad(LIST_X, y - 3.0, LIST_W, ROW_H - 2.0, 0.10, 0.12, 0.17, 1.0)
            end
            local r, g, b = 0.78, 0.84, 0.90
            if on then
                r, g, b = 1.0, 1.0, 1.0
            end
            label(name, LIST_X + 12.0, y, 0.46, r, g, b, 1.0)
            local ar, ag, ab = 0.40, 0.70, 0.95
            if kind == "game" then
                ar, ag, ab = 0.95, 0.65, 0.25
            end
            draw_quad(LIST_X + LIST_W - 14.0, y + 3.0, 8.0, 8.0, ar, ag, ab, 1.0)
        end
    end
end

local function draw_detail(v)
    local flag = demo_flag(v[S.sel + 1])
    local info = info_of(flag)
    local title = string.upper(flag)
    local blurb = "Engine showcase."
    local controls = ""
    local cover = ""
    if info ~= nil then
        title = info.title
        blurb = info.blurb
        controls = info.controls
        cover = info.cover
    end

    card(sprite_load(cover), PANEL_X, LIST_TOP, PANEL_W, 224.0, 0.06, 0.10, 0.14)
    draw_quad(PANEL_X, LIST_TOP + 224.0, PANEL_W, 2.0, 0.45, 0.90, 1.0, 0.8)

    label(title, PANEL_X + 4.0, LIST_TOP + 240.0, 0.92, 1.0, 1.0, 1.0, 1.0)
    label(blurb, PANEL_X + 4.0, LIST_TOP + 278.0, 0.46, 0.80, 0.86, 0.92, 1.0)
    label(controls, PANEL_X + 4.0, LIST_TOP + 300.0, 0.42, 0.55, 0.85, 0.95, 1.0)
    label("fake2d_hello --" .. flag .. "-demo", PANEL_X + 4.0, LIST_TOP + 328.0,
          0.42, 0.78, 0.74, 0.42, 1.0)

    local script = demo_script(v[S.sel + 1])
    local sw = text_width(script, 0.36)
    label(script, PANEL_X + PANEL_W - sw - 4.0, LIST_TOP + 328.0,
          0.36, 0.42, 0.48, 0.56, 1.0)
end

--- Left edge of category chip i, and its width.
local function chip_box(i)
    local x = LIST_X
    local k = 1
    while k < i do
        x = x + text_width(CATS[k].label, 0.44) + 20.0 + 8.0
        k = k + 1
    end
    return x, text_width(CATS[i].label, 0.44) + 20.0
end

local function draw_chips()
    for i = 1, #CATS do
        local x, w = chip_box(i)
        local on = (i - 1) == S.cat
        if on then
            draw_quad(x, 70.0, w, 26.0, 0.20, 0.42, 0.52, 1.0)
        else
            draw_quad(x, 70.0, w, 26.0, 0.10, 0.12, 0.17, 1.0)
        end
        local a = 0.65
        if on then
            a = 1.0
        end
        label(CATS[i].label, x + 10.0, 76.0, 0.44, 1.0, 1.0, 1.0, a)
    end
end

local function draw(v, n)
    draw_quad(0, 0, W, H, 0.045, 0.055, 0.085, 1.0)
    draw_quad(0, 0, W, 56.0, 0.075, 0.10, 0.15, 1.0)
    label("FAKE2D DEMO LAUNCHER", 24.0, 16.0, 0.86, 1.0, 1.0, 1.0, 1.0)

    local count = tostring(n) .. " OF " .. tostring(S.total) .. " DEMOS"
    local cw = text_width(count, 0.44)
    label(count, W - cw - 24.0, 22.0, 0.44, 0.55, 0.85, 0.95, 1.0)

    draw_chips()
    draw_list(v, n)
    draw_detail(v)

    draw_quad(0, H - 30.0, W, 30.0, 0.075, 0.10, 0.15, 1.0)
    label("UP/DOWN select    ENTER launch    TAB category    ESC quit",
          24.0, H - 22.0, 0.42, 0.72, 0.78, 0.86, 1.0)
end

--- Which category chip is under the cursor, -1 for none.
local function chip_under()
    if S.my < 70.0 or S.my > 96.0 then
        return -1
    end
    for i = 1, #CATS do
        local x, w = chip_box(i)
        if S.mx >= x and S.mx <= x + w then
            return i - 1
        end
    end
    return -1
end

--- Catalogue index under the cursor in the list column, or -1.
local function row_under(n)
    if S.mx < LIST_X or S.mx > LIST_X + LIST_W then
        return -1
    end
    if S.my < LIST_TOP - 3.0 then
        return -1
    end
    if S.my > LIST_TOP + LIST_ROWS * ROW_H then
        return -1
    end
    local row = math.floor((S.my - (LIST_TOP - 3.0)) / ROW_H)
    if S.top + row < n then
        return S.top + row
    end
    return -1
end

function update(dt)
    if S.total == nil then
        S.total = demo_count()
        S.sel = 0
        S.cat = CATS_ALL
        S.top = 0
        S.frame = 0
        S.mx = 0.0
        S.my = 0.0
        window_set_title("fake2d - demo launcher")
    end
    S.frame = S.frame + 1

    -- The list shows the filtered subset, so rebuild it before anything indexes
    -- it and keep the selection inside it. Positions are within this view; the
    -- catalogue index behind a position is `v[position + 1]`.
    local v = visible()
    local n = S.nvis
    if S.sel > n - 1 then
        S.sel = n - 1
    end
    if S.sel < 0 then
        S.sel = 0
    end

    S.mx = input_mouse_x()
    S.my = input_mouse_y()
    if TEST_MX >= 0 then
        S.mx = TEST_MX
    end
    if TEST_MY >= 0 then
        S.my = TEST_MY
    end

    local wheel = input_mouse_wheel()
    if wheel > 0.5 then
        S.top = S.top - 3
    end
    if wheel < -0.5 then
        S.top = S.top + 3
    end

    if input_key_pressed("up") then
        S.sel = step(S.sel, -1, n)
    end
    if input_key_pressed("down") then
        S.sel = step(S.sel, 1, n)
    end
    if input_key_pressed("pageup") then
        S.sel = step(S.sel, -LIST_ROWS, n)
    end
    if input_key_pressed("pagedown") then
        S.sel = step(S.sel, LIST_ROWS, n)
    end
    if input_key_pressed("home") then
        S.sel = 0
    end
    if input_key_pressed("tab") then
        S.cat = S.cat + 1
        if S.cat > CATS_ENGINE then
            S.cat = CATS_ALL
        end
        refilter()
    end

    local row = row_under(n)
    if row >= 0 then
        S.sel = row
    end

    clamp_scroll(n)

    if input_key_pressed("escape") then
        window_quit()
    end

    local commit = input_key_pressed("return") or input_key_pressed("space")
    local clicked = input_mouse_pressed(0) or (TEST_CLICK >= 0 and S.frame == TEST_CLICK)
    if clicked then
        local chip = chip_under()
        if chip >= 0 then
            S.cat = chip
            log_info("launcher: filter " .. CATS[chip + 1].id)
            refilter()
        elseif row >= 0 then
            S.sel = row
            commit = true
        end
    end

    -- The trace changes the filter on frame 15 and commits on frame 20, so the
    -- pick sees the list as the player would after the filter has settled.
    if TEST_CAT >= 0 and S.frame == 15 and S.cat ~= TEST_CAT then
        S.cat = TEST_CAT
        log_info("launcher: filter " .. CATS[TEST_CAT + 1].id)
        refilter()
    end

    -- Headless trace: LAUNCHER_TEST_PICK=<n> commits the n-th visible entry on
    -- frame 20, so CI can prove selection and the relaunch request work without
    -- a mouse or keyboard.
    if PICK >= 0 and S.frame == 20 then
        if PICK < n then
            S.sel = PICK
            commit = true
        end
    end

    if commit then
        local flag = demo_flag(v[S.sel + 1])
        log_info("launcher: starting " .. flag)
        app_launch(flag)
    end

    draw(v, n)
    return 0
end
