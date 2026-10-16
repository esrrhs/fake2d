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

local wx = 300.0 + 0.0
local wy = 90.0 + 0.0

local tab = 0 + 0
local master = 0.80 + 0.0
local music = 0.55 + 0.0
local sfx = 0.70 + 0.0
local muted = false
local bright = 0.50 + 0.0
local gamma = 0.45 + 0.0
local bloom = true
local fog = true
local open_display = true
local open_effects = false
local scheme = 0 + 0
local scheme_open = false
local control = 0 + 0

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
    master = ui_slider("master", 0, bx, by + 26, 280, 22, master, 0.0, 1.0)
    music = ui_slider("music", 0, bx, by + 66, 280, 22, music, 0.0, 1.0)
    sfx = ui_slider("sfx", 0, bx, by + 106, 280, 22, sfx, 0.0, 1.0)
    draw_text("master " .. pct(master), bx + 292, by + 30, 0.55,
              0.75, 0.85, 0.95, 1.0)
    draw_text("music  " .. pct(music), bx + 292, by + 70, 0.55,
              0.75, 0.85, 0.95, 1.0)
    draw_text("sfx    " .. pct(sfx), bx + 292, by + 110, 0.55,
              0.75, 0.85, 0.95, 1.0)
    if ui_checkbox("mute", 0, bx, by + 146, 18, muted, "mute everything", 0.6) then
        muted = not muted
    end
    return 0
end

local function draw_video(bx, by)
    if ui_tree("grp_display", 0, bx, by, 280, 22, "display", open_display, 0.65) then
        open_display = not open_display
    end
    if open_display then
        draw_text("brightness", bx + 18, by + 32, 0.6, 0.72, 0.78, 0.86, 1.0)
        bright = ui_slider("bright", 0, bx + 18, by + 50, 262, 20, bright, 0.0, 1.0)
        draw_text("gamma", bx + 18, by + 84, 0.6, 0.72, 0.78, 0.86, 1.0)
        gamma = ui_slider("gamma", 0, bx + 18, by + 102, 262, 20, gamma, 0.0, 1.0)
    end

    local ey = by + 40
    if open_display then
        ey = by + 140
    end
    if ui_tree("grp_effects", 0, bx, ey, 280, 22, "effects", open_effects, 0.65) then
        open_effects = not open_effects
    end
    if open_effects then
        if ui_checkbox("bloom", 0, bx + 18, ey + 30, 18, bloom, "bloom", 0.6) then
            bloom = not bloom
        end
        if ui_checkbox("fog", 0, bx + 18, ey + 58, 18, fog, "fog of war", 0.6) then
            fog = not fog
        end
    end
    return 0
end

local function draw_controls(bx, by)
    draw_text("INPUT", bx, by, 0.7, 0.75, 0.65, 0.95, 1.0)
    if ui_radio("ctl0", 0, bx, by + 26, 18, control == 0, control_name(0), 0.6) then
        control = 0
    end
    if ui_radio("ctl1", 0, bx, by + 54, 18, control == 1, control_name(1), 0.6) then
        control = 1
    end

    draw_text("SCHEME", bx, by + 92, 0.7, 0.75, 0.65, 0.95, 1.0)
    if ui_combo("scheme", 0, bx, by + 112, 200, 26, scheme_name(scheme),
                scheme_open, 0.6) then
        scheme_open = not scheme_open
    end
    if scheme_open then
        draw_quad(bx - 2.0, by + 142, 204, 84, 0.08, 0.10, 0.15, 1.0)
        local k = 0 + 0
        while k < 3 do
            local ry = by + 144 + k * 26
            if ui_combo_item("sc" .. istr(k), 0, bx, ry, 200, 24,
                             scheme_name(k), scheme == k, 0.6) then
                scheme = k
                scheme_open = false
            end
            k = k + 1
        end
    end
    return 0
end

function update(dt)
    -- --- the window shell -------------------------------------------------
    if ui_window("settings", 0, wx, wy, WIN_W, WIN_H, "SETTINGS", 0.7) then
        wx = wx + input_mouse_dx()
        wy = wy + input_mouse_dy()
    end
    -- Keep it reachable: a panel dragged off the canvas can never be grabbed
    -- again, so the title bar always stays inside.
    if wx < 0.0 then
        wx = 0.0
    end
    if wy < 0.0 then
        wy = 0.0
    end
    if wx > SCREEN_W - WIN_W then
        wx = SCREEN_W - WIN_W
    end
    if wy > SCREEN_H - WIN_H then
        wy = SCREEN_H - WIN_H
    end

    -- --- tabs and their panels -------------------------------------------
    local i = 0 + 0
    while i < 3 do
        if ui_tab("tab" .. istr(i), 0, wx + 10 + i * 102, wy + 36, 100, 24,
                  tab_name(i), tab == i, 0.6) then
            tab = i
        end
        i = i + 1
    end

    local bx = wx + 20
    local by = wy + 70
    if tab == 0 then
        draw_audio(bx, by)
    end
    if tab == 1 then
        draw_video(bx, by)
    end
    if tab == 2 then
        draw_controls(bx, by)
    end

    -- --- the page frame ---------------------------------------------------
    draw_text("drag the title bar - the position lives in the script", wx + 10,
              wy + WIN_H - 20, 0.55, 0.5, 0.58, 0.66, 1.0)

    -- --- outside: a reset and the hover hints -----------------------------
    if ui_button("reset", 0, 40, 470, 120, 26, "RECENTER", 0.6) then
        wx = 300.0
        wy = 90.0
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
