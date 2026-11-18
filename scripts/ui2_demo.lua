-- fake2d pure-Lua UI demo round 2 (--ui2-demo).
--
-- The widgets added in Phase 25: a progress bar, a radio row and a combo
-- box. All three are thinner than the Phase 20 widgets on purpose — they are
-- immediate-mode pieces the *script* composes:
--
--   ui_bar        pure draw; the value is clamped to 0..1, so a health bar
--                 fed 1.4 or -0.2 still looks like a bar.
--   ui_radio      one row; which row is selected lives here, not in the
--                 widget, so a group is just N calls and one index.
--   ui_combo      the header, then one ui_combo_item per row. The popup's
--                 open state is a local, so "an open list" is a normal
--                 if-block rather than a mode inside the widget.
--
-- Headless runs have no mouse, so everything sits at its initial value
-- there; run in a window to pick rows and open the list.
--
-- FakeLua notes: `math.fmod` is not supported by the JIT codegen (see
-- scripts/slope_demo.lua), and state lives in plain file locals — this
-- script keeps its label sets in functions rather than tables.

local ITEM_COUNT = 4 + 0
local ITEM_H = 26 + 0


-- FakeLua binds file-level locals as const, so everything that changes
-- across frames lives in a host-side NativeObject instead (see
-- docs/SCRIPTING.md rule 3). Fields read and write like a plain table.
local S = new_global_obj("ui2_demo", "state")

local function reset_state()
    S.t = 0.0 + 0.0
    S.sel = 1 + 0
    S.open = false
    S.pick = 0 + 0
    S.mana = 0.60 + 0.0
end

local function istr(n)
    return string.format("%d", n)
end

local function pct(v)
    return istr(math.floor(v * 100 + 0.5)) .. "%"
end

-- Sawtooth in 0..1: the loading bar refills forever, which exercises both
-- the empty frame and the full one without any extra state.
local function saw(v)
    return v - math.floor(v)
end

local function unit_name(k)
    if k == 0 then
        return "soldier"
    end
    if k == 1 then
        return "archer"
    end
    if k == 2 then
        return "cavalry"
    end
    return "siege"
end

local function diff_name(k)
    if k == 0 then
        return "easy"
    end
    if k == 1 then
        return "normal"
    end
    return "hard"
end

local function update_body(dt)
    if S.seeded == nil then
        S.seeded = true
        reset_state()
    end

    S.t = S.t + dt

    draw_text("UI WIDGETS ROUND 2", 40, 24, 1.2, 0.94, 0.96, 1.0, 1.0)
    draw_text("progress bars, radio groups, combo boxes", 40, 52, 0.7,
              0.55, 0.66, 0.75, 1.0)

    -- --- progress bars: pure draw, value clamped to 0..1 ------------------
    draw_text("PROGRESS", 40, 84, 0.75, 0.55, 0.92, 0.94, 1.0)

    local load_v = saw(S.t * 0.25)
    local hp = 0.35 + 0.3 * (0.5 + 0.5 * math.sin(S.t * 1.7))
    ui_root_begin(40, 104, 340, "auto", 0, true)
    ui_vstack(12, 0, "stretch")
    ui_hstack(10, 0, "center")
    ui_next_grow(1)
    ui_next_height(18)
    ui_bar(load_v, 0.35, 0.72, 0.95, 1.0)
    ui_text("loading " .. pct(load_v), 0.6, "ellipsis", 2, 0.75, 0.85, 0.95, 1.0)
    ui_hstack_end()
    ui_hstack(10, 0, "center")
    ui_next_grow(1)
    ui_next_height(18)
    ui_bar(hp, 0.85, 0.30, 0.32, 1.0)
    ui_text("health " .. pct(hp), 0.6, "ellipsis", 2, 0.75, 0.85, 0.95, 1.0)
    ui_hstack_end()
    S.mana = ui_slider("mana", S.mana, 0.0, 1.5)
    ui_hstack(10, 0, "center")
    ui_next_grow(1)
    ui_next_height(18)
    ui_bar(S.mana, 0.55, 0.40, 0.90, 1.0)
    ui_text("mana " .. pct(S.mana), 0.6, "ellipsis", 2, 0.75, 0.85, 0.95, 1.0)
    ui_hstack_end()
    ui_text("the slider reaches 150% - past 100% the bar clamps", 0.55, "wrap", 0, 0.5, 0.58, 0.66, 1.0)
    ui_vstack_end()
    ui_root_end()

    -- --- radio group: N rows, one index, the script owns the choice -------
    ui_root_begin(400, 84, 210, "auto", 0, true)
    ui_vstack(8, 0, "start")
    ui_text("DIFFICULTY", 0.75, "ellipsis", 0, 1.0, 0.80, 0.35, 1.0)
    local i = 0 + 0
    while i < 3 do
        ui_radio("d" .. istr(i), S.sel == i, diff_name(i), 0.65)
        i = i + 1
    end
    ui_text("selected: " .. diff_name(S.sel), 0.7, "ellipsis", 0, 0.94, 0.96, 1.0, 1.0)
    ui_vstack_end()
    ui_root_end()

    -- --- combo: a header plus one row per item ---------------------------
    ui_root_begin(640, 84, 280, "auto", 0, true)
    ui_vstack(6, 0, "stretch")
    ui_text("UNIT", 0.75, "ellipsis", 0, 0.75, 0.65, 0.95, 1.0)
    ui_combo("unit", unit_name(S.pick), S.open, 0.65)
    if S.open then
        ui_card(4, 2, 0.08, 0.10, 0.15, 1.0)
        local k = 0 + 0
        while k < ITEM_COUNT do
            ui_combo_item("u" .. istr(k), unit_name(k), S.pick == k, 0.65)
            k = k + 1
        end
        ui_card_end()
    end
    ui_text("picked: " .. unit_name(S.pick), 0.7, "ellipsis", 0, 0.94, 0.96, 1.0, 1.0)
    if S.open then
        ui_text("list open", 0.6, "ellipsis", 0, 0.55, 0.66, 0.75, 1.0)
    end
    ui_vstack_end()
    ui_root_end()

    -- The logical canvas is 960x540 (1920x1080 at 2x), so nothing here may
    -- sit below y=540 or past x=960.
    draw_text("the script owns the state: a group is N rows plus one index",
              40, 470, 0.7, 0.5, 0.6, 0.7, 1.0)
    draw_text("960x540 logical canvas", 40, 496, 0.6, 0.45, 0.52, 0.6, 1.0)
    return 0
end

function update(dt)
    ui_begin()
    local result = update_body(dt)
    ui_end()
    S.mana = ui_result("mana", S.mana)
    local i = 0 + 0
    while i < 3 do
        if ui_clicked("d" .. istr(i)) then
            S.sel = i
        end
        i = i + 1
    end
    if ui_clicked("unit") then
        S.open = not S.open
    end
    local k = 0 + 0
    while k < ITEM_COUNT do
        if ui_clicked("u" .. istr(k)) then
            S.pick = k
            S.open = false
        end
        k = k + 1
    end
    return result
end
