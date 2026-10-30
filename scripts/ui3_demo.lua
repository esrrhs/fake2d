-- fake2d pure-Lua UI demo round 3 (--ui3-demo).
--
-- The pieces Phase 28 added, put to work:
--
--   ui_window(key, ox, oy, w, h, title)  a titled panel dragged by its bar.
--                              It reports "being dragged" (not a position),
--                              so the caller moves it with input_mouse_dx/dy
--                              — and it stays silent on the frame the grab
--                              starts, so the panel never jumps by whatever
--                              distance the cursor travelled to reach it
--   input_mouse_dx / _dy       the per-frame cursor movement a drag needs
--   ui_tab(..., label, active) one tab; which tab is active is the caller's
--   ui_tree(..., label, open)  a disclosure row; the children are an if-block
--
-- Everything is still immediate-mode and caller-owned: the window's position
-- is two locals here, the tab is an index, and each tree node is a bool. The
-- one thing a tooltip needs that no widget has to provide is "is the cursor
-- on it", which is what ui_hovered(key) is for — so hints below are drawn
-- from plain quads and text.
--
-- Headless runs have no mouse, so nothing moves or gets clicked there; run in
-- a window to drag the panel, switch tabs and open the groups.
--
-- FakeLua notes: chunk-level locals must be initialised, natives take an
-- exact argument count, and state lives in plain file locals (label sets are
-- functions here, not tables).

local SCREEN_W = 960 + 0
local SCREEN_H = 540 + 0

local WIN_W = 360 + 0
local WIN_H = 330 + 0



-- FakeLua binds file-level locals as const, so everything that changes
-- across frames lives in a host-side NativeObject instead (see
-- docs/SCRIPTING.md rule 3). Fields read and write like a plain table.
local S = new_global_obj("ui3_demo", "state")

local function reset_state()
    S.wx = 300.0 + 0.0
    S.wy = 90.0 + 0.0
    S.tab = 0 + 0
    S.master = 0.80 + 0.0
    S.music = 0.55 + 0.0
    S.sfx = 0.70 + 0.0
    S.muted = false
    S.bright = 0.50 + 0.0
    S.gamma = 0.45 + 0.0
    S.bloom = true
    S.fog = true
    S.open_display = true
    S.open_effects = false
    S.scheme = 0 + 0
    S.scheme_open = false
    S.control = 0 + 0
end

local function istr(n)
    return string.format("%d", n)
end

local function pct(v)
    return istr(math.floor(v * 100 + 0.5)) .. "%"
end

local function tab_name(i)
    if i == 1 then
        return "VIDEO"
    end
    if i == 2 then
        return "CONTROLS"
    end
    return "AUDIO"
end

local function scheme_name(i)
    if i == 1 then
        return "gamepad"
    end
    if i == 2 then
        return "classic"
    end
    return "default"
end

local function control_name(i)
    if i == 1 then
        return "invert Y"
    end
    return "vibration"
end

-- A tooltip is just a panel and some text; ui_hovered(key) is the only part
-- that has to come from the widget.
local function tooltip(key, text)
    if not ui_hovered(key) then
        return 0
    end
    local mx = input_mouse_x()
    local my = input_mouse_y()
    local w = text_width(text, 0.6) + 16.0
    local h = 26 + 0
    local x = mx + 14.0
    local y = my + 14.0
    if x + w > SCREEN_W - 4.0 then
        x = SCREEN_W - 4.0 - w
    end
    if y + h > SCREEN_H - 4.0 then
        y = SCREEN_H - 4.0 - h
    end
    draw_quad(x - 1.0, y - 1.0, w + 2.0, h + 2.0, 0.35, 0.45, 0.65, 1.0)
    draw_quad(x, y, w, h, 0.07, 0.09, 0.14, 1.0)
    draw_text(text, x + 8.0, y + 6.0, 0.6, 0.90, 0.94, 1.0, 1.0)
    return 0
end

local function draw_audio(bx, by)
    draw_text("MIXER", bx, by, 0.7, 0.55, 0.92, 0.94, 1.0)
    S.master = ui_slider("master", 0, bx, by + 26, 280, 22, S.master, 0.0, 1.0)
    S.music = ui_slider("music", 0, bx, by + 66, 280, 22, S.music, 0.0, 1.0)
    S.sfx = ui_slider("sfx", 0, bx, by + 106, 280, 22, S.sfx, 0.0, 1.0)
    draw_text("master " .. pct(S.master), bx + 292, by + 30, 0.55,
              0.75, 0.85, 0.95, 1.0)
    draw_text("music  " .. pct(S.music), bx + 292, by + 70, 0.55,
              0.75, 0.85, 0.95, 1.0)
    draw_text("sfx    " .. pct(S.sfx), bx + 292, by + 110, 0.55,
              0.75, 0.85, 0.95, 1.0)
    if ui_checkbox("mute", 0, bx, by + 146, 18, S.muted, "mute everything", 0.6) then
        S.muted = not S.muted
    end
    return 0
end

local function draw_video(bx, by)
    if ui_tree("grp_display", 0, bx, by, 280, 22, "display", S.open_display, 0.65) then
        S.open_display = not S.open_display
    end
    if S.open_display then
        draw_text("brightness", bx + 18, by + 32, 0.6, 0.72, 0.78, 0.86, 1.0)
        S.bright = ui_slider("bright", 0, bx + 18, by + 50, 262, 20, S.bright, 0.0, 1.0)
        draw_text("gamma", bx + 18, by + 84, 0.6, 0.72, 0.78, 0.86, 1.0)
        S.gamma = ui_slider("gamma", 0, bx + 18, by + 102, 262, 20, S.gamma, 0.0, 1.0)
    end

    local ey = by + 40
    if S.open_display then
        ey = by + 140
    end
    if ui_tree("grp_effects", 0, bx, ey, 280, 22, "effects", S.open_effects, 0.65) then
        S.open_effects = not S.open_effects
    end
    if S.open_effects then
        if ui_checkbox("bloom", 0, bx + 18, ey + 30, 18, S.bloom, "bloom", 0.6) then
            S.bloom = not S.bloom
        end
        if ui_checkbox("fog", 0, bx + 18, ey + 58, 18, S.fog, "fog of war", 0.6) then
            S.fog = not S.fog
        end
    end
    return 0
end

local function draw_controls(bx, by)
    draw_text("INPUT", bx, by, 0.7, 0.75, 0.65, 0.95, 1.0)
    if ui_radio("ctl0", 0, bx, by + 26, 18, S.control == 0, control_name(0), 0.6) then
        S.control = 0
    end
    if ui_radio("ctl1", 0, bx, by + 54, 18, S.control == 1, control_name(1), 0.6) then
        S.control = 1
    end

    draw_text("SCHEME", bx, by + 92, 0.7, 0.75, 0.65, 0.95, 1.0)
    if ui_combo("scheme", 0, bx, by + 112, 200, 26, scheme_name(S.scheme),
                S.scheme_open, 0.6) then
        S.scheme_open = not S.scheme_open
    end
    if S.scheme_open then
        draw_quad(bx - 2.0, by + 142, 204, 84, 0.08, 0.10, 0.15, 1.0)
        local k = 0 + 0
        while k < 3 do
            local ry = by + 144 + k * 26
            if ui_combo_item("sc" .. istr(k), 0, bx, ry, 200, 24,
                             scheme_name(k), S.scheme == k, 0.6) then
                S.scheme = k
                S.scheme_open = false
            end
            k = k + 1
        end
    end
    return 0
end

function update(dt)
    if S.seeded == nil then
        S.seeded = true
        reset_state()
    end

    -- --- the window shell -------------------------------------------------
    if ui_window("settings", 0, S.wx, S.wy, WIN_W, WIN_H, "SETTINGS", 0.7) then
        S.wx = S.wx + input_mouse_dx()
        S.wy = S.wy + input_mouse_dy()
    end
    -- Keep it reachable: a panel dragged off the canvas can never be grabbed
    -- again, so the title bar always stays inside.
    if S.wx < 0.0 then
        S.wx = 0.0
    end
    if S.wy < 0.0 then
        S.wy = 0.0
    end
    if S.wx > SCREEN_W - WIN_W then
        S.wx = SCREEN_W - WIN_W
    end
    if S.wy > SCREEN_H - WIN_H then
        S.wy = SCREEN_H - WIN_H
    end

    -- --- tabs and their panels -------------------------------------------
    local i = 0 + 0
    while i < 3 do
        if ui_tab("tab" .. istr(i), 0, S.wx + 10 + i * 102, S.wy + 36, 100, 24,
                  tab_name(i), S.tab == i, 0.6) then
            S.tab = i
        end
        i = i + 1
    end

    local bx = S.wx + 20
    local by = S.wy + 70
    if S.tab == 0 then
        draw_audio(bx, by)
    end
    if S.tab == 1 then
        draw_video(bx, by)
    end
    if S.tab == 2 then
        draw_controls(bx, by)
    end

    -- --- the page frame ---------------------------------------------------
    draw_text("drag the title bar - the position lives in the script", S.wx + 10,
              S.wy + WIN_H - 20, 0.55, 0.5, 0.58, 0.66, 1.0)

    -- --- outside: a reset and the hover hints -----------------------------
    if ui_button("reset", 0, 40, 470, 120, 26, "RECENTER", 0.6) then
        S.wx = 300.0
        S.wy = 90.0
    end

    draw_text("UI WIDGETS ROUND 3", 40, 24, 1.2, 0.94, 0.96, 1.0, 1.0)
    draw_text("draggable window, tab bar, collapsible tree", 40, 52, 0.7,
              0.55, 0.66, 0.75, 1.0)
    draw_text("hover a control for a hint - it is a panel plus ui_hovered", 40,
              76, 0.6, 0.5, 0.58, 0.66, 1.0)

    tooltip("master", "master bus, 0-100%")
    tooltip("music", "music bus")
    tooltip("sfx", "one-shot bus")
    tooltip("grp_display", "click to collapse")
    tooltip("reset", "put the panel back")

    -- The logical canvas is 960x540 (1920x1080 at 2x), so nothing here may
    -- sit below y=540 or past x=960.
    return 0
end
