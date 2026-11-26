-- fake2d pure-Lua UI demo round 3 (--ui3-demo).
--
-- The pieces Phase 28 added, put to work:
--
--   ui_root_begin(x, y, w, "auto") + ui_window(key, title)  a titled panel dragged by its bar.
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

local function draw_audio()
    ui_text("MIXER", 0.7, "ellipsis", 0, 0.55, 0.92, 0.94, 1.0)
    ui_hstack(8, 0, "center") ui_next_grow(1) S.master = ui_slider("master", S.master, 0.0, 1.0) ui_text("master " .. pct(S.master), 0.55, "ellipsis", 2, 0.75, 0.85, 0.95, 1.0) ui_hstack_end()
    ui_hstack(8, 0, "center") ui_next_grow(1) S.music = ui_slider("music", S.music, 0.0, 1.0) ui_text("music  " .. pct(S.music), 0.55, "ellipsis", 2, 0.75, 0.85, 0.95, 1.0) ui_hstack_end()
    ui_hstack(8, 0, "center") ui_next_grow(1) S.sfx = ui_slider("sfx", S.sfx, 0.0, 1.0) ui_text("sfx    " .. pct(S.sfx), 0.55, "ellipsis", 2, 0.75, 0.85, 0.95, 1.0) ui_hstack_end()
    ui_checkbox("mute", S.muted, "mute everything", 0.6)
    return 0
end

local function draw_video()
    ui_tree("grp_display", "display", S.open_display, 0.65)
    if S.open_display then
        ui_next_pad(16, 0, 0, 0)
        ui_vstack(6, 0, "stretch")
        ui_text("brightness", 0.6, "ellipsis", 0, 0.72, 0.78, 0.86, 1.0)
        S.bright = ui_slider("bright", S.bright, 0.0, 1.0)
        ui_text("gamma", 0.6, "ellipsis", 0, 0.72, 0.78, 0.86, 1.0)
        S.gamma = ui_slider("gamma", S.gamma, 0.0, 1.0)
        ui_vstack_end()
    end
    ui_tree("grp_effects", "effects", S.open_effects, 0.65)
    if S.open_effects then
        ui_next_pad(16, 0, 0, 0)
        ui_vstack(6, 0, "stretch")
        ui_checkbox("bloom", S.bloom, "bloom", 0.6)
        ui_checkbox("fog", S.fog, "fog of war", 0.6)
        ui_vstack_end()
    end
    return 0
end

local function draw_controls()
    ui_text("INPUT", 0.7, "ellipsis", 0, 0.75, 0.65, 0.95, 1.0)
    ui_radio("ctl0", S.control == 0, control_name(0), 0.6)
    ui_radio("ctl1", S.control == 1, control_name(1), 0.6)
    ui_text("SCHEME", 0.7, "ellipsis", 0, 0.75, 0.65, 0.95, 1.0)
    ui_combo("scheme", scheme_name(S.scheme), S.scheme_open, 0.6)
    if S.scheme_open then
        ui_card(4, 2, 0.08, 0.10, 0.15, 1.0)
        local k = 0 + 0
        while k < 3 do
            ui_combo_item("sc" .. istr(k), scheme_name(k), S.scheme == k, 0.6)
            k = k + 1
        end
        ui_card_end()
    end
    return 0
end

local function update_body(dt)
    if S.seeded == nil then
        S.seeded = true
        reset_state()
    end

    if S.wx < 0.0 then S.wx = 0.0 end
    if S.wy < 0.0 then S.wy = 0.0 end
    if S.wx > SCREEN_W - WIN_W then S.wx = SCREEN_W - WIN_W end
    if S.wy > SCREEN_H - WIN_H then S.wy = SCREEN_H - WIN_H end

    ui_root_begin(S.wx, S.wy, WIN_W, "auto", 0, true)
    ui_panel()
    ui_window("settings", "SETTINGS", 0.7)
    ui_next_pad(10, 8, 10, 10)
    ui_vstack(8, 0, "stretch")
    ui_hstack(6, 0, "center")
    local i = 0 + 0
    while i < 3 do
        ui_next_grow(1)
        ui_tab("tab" .. istr(i), tab_name(i), S.tab == i, 0.6)
        i = i + 1
    end
    ui_hstack_end()
    if S.tab == 0 then draw_audio() end
    if S.tab == 1 then draw_video() end
    if S.tab == 2 then draw_controls() end
    ui_text("drag the title bar - the position lives in the script", 0.55, "wrap", 0, 0.5, 0.58, 0.66, 1.0)
    ui_vstack_end()
    ui_root_end()

    ui_root_begin(40, 470, 120, "auto", 0, true)
    ui_button("reset", "RECENTER", 0.6)
    ui_root_end()

    draw_text("UI WIDGETS ROUND 3", 40, 24, 1.2, 0.94, 0.96, 1.0, 1.0)
    draw_text("draggable window, tab bar, collapsible tree", 40, 52, 0.7,
              0.55, 0.66, 0.75, 1.0)
    draw_text("hover a control for a hint - it is a panel plus ui_hovered", 40,
              76, 0.6, 0.5, 0.58, 0.66, 1.0)


    -- The logical canvas is 960x540 (1920x1080 at 2x), so nothing here may
    -- sit below y=540 or past x=960.
    return 0
end

function update(dt)
    ui_begin()
    local result = update_body(dt)
    ui_end()
    tooltip("master", "master bus, 0-100%")
    tooltip("music", "music bus")
    tooltip("sfx", "one-shot bus")
    tooltip("grp_display", "click to collapse")
    tooltip("reset", "put the panel back")
    S.master = ui_result("master", S.master)
    S.music = ui_result("music", S.music)
    S.sfx = ui_result("sfx", S.sfx)
    S.bright = ui_result("bright", S.bright)
    S.gamma = ui_result("gamma", S.gamma)
    if ui_clicked("mute") then
        S.muted = not S.muted
    end
    if ui_clicked("grp_display") then
        S.open_display = not S.open_display
    end
    if ui_clicked("grp_effects") then
        S.open_effects = not S.open_effects
    end
    if ui_clicked("bloom") then
        S.bloom = not S.bloom
    end
    if ui_clicked("fog") then
        S.fog = not S.fog
    end
    if ui_clicked("ctl0") then
        S.control = 0
    end
    if ui_clicked("ctl1") then
        S.control = 1
    end
    if ui_clicked("scheme") then
        S.scheme_open = not S.scheme_open
    end
    local k = 0 + 0
    while k < 3 do
        if ui_clicked("sc" .. istr(k)) then
            S.scheme = k
            S.scheme_open = false
        end
        k = k + 1
    end
    if ui_clicked("settings") then
        S.wx = S.wx + input_mouse_dx()
        S.wy = S.wy + input_mouse_dy()
    end
    local i = 0 + 0
    while i < 3 do
        if ui_clicked("tab" .. istr(i)) then
            S.tab = i
        end
        i = i + 1
    end
    if ui_clicked("reset") then
        S.wx = 300.0
        S.wy = 90.0
    end
    return result
end
