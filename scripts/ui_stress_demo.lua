-- Seeded dynamic layout laboratory. Same states are reproducible headless or windowed.
-- SPACE pause, N step, R reset seed, A audit. Stage sequence is short -> long -> short.
local S = new_global_obj("ui_stress_demo", "state")
local function next_random()
    S.seed = (S.seed * 25173 + 13849) % 65521
    return S.seed
end
local function next_state()
    S.step = S.step + 1
    S.long = S.step % 3 == 1 S.chinese = next_random() % 2 == 0
    S.width = 280 + next_random() % 381
    S.font = 0.45 + (next_random() % 4) * 0.05
    S.height_mode = next_random() % 2 == 0
    S.sample = "田 short"
    if S.long then
        S.sample = ""
        local i = 0 + 0
        while i < 40 do
            if S.chinese then S.sample = S.sample .. S.cjk else S.sample = S.sample .. S.latin end
            i = i + 1
        end
    end
    if S.long then ui_set_scroll_offset("dynamic-stress", 100000) else ui_set_scroll_offset("dynamic-stress", 0) end
    if S.step % 6 == 4 then ui_modal_open("stress-dialog") else ui_modal_close() end
    log_number(149100 + S.step % 6) log_number(S.seed)
    return 0
end
function update(dt)
    if S.ready == nil then
        S.ready = true S.frame = 0 + 0 S.step = -1 + 0 S.seed = 14926 + 0
        S.hits = 0 + 0 S.auto = true S.audit = true S.last_state = -1 + 0
        S.cjk = "能吏抚恤百姓修复水利田亩营制长句自动换行国朝纪事"
        S.latin = "LongUnbreakableLatinPopulation123456789012345678901234567890"
        font_add_fallback("assets/fonts/ui_layout_test.otf", 28)
        next_state()
    end
    S.frame = S.frame + 1
    if input_key_pressed("space") then S.auto = not S.auto end
    if input_key_pressed("n") then S.auto = false next_state() end
    if input_key_pressed("r") then S.seed = 14926 + 0 S.step = -1 + 0 S.auto = true next_state() end
    if input_key_pressed("a") then S.audit = not S.audit end
    if S.auto and S.frame > 1 and (S.frame - 1) % 30 == 0 then next_state() end
    ui_scale(900, 680, "contain")
    if S.height_mode then ui_scale(900, 680, "height") end
    ui_set_theme_metric("font_scale", S.font)
    ui_set_audit(S.audit)
    ui_begin()
    ui_push(18, 14, 724, 650, 10)
        ui_panel()
        ui_text("UI 1.49 / seeded dynamic layout stress", 0.75)
        ui_text("SPACE pause | N next | R seed reset | A audit | Tab / pad focus", 0.45)
        ui_text("seed=" .. string.format("%d", S.seed) .. " step=" .. string.format("%d", S.step) .. " width=" .. string.format("%d", S.width), 0.45)
        ui_row(8, -1, -1, -1)
            ui_col() ui_button("step", 25, "Next deterministic state") ui_col_end()
            ui_col() ui_button("pause", 25, "Pause / auto") ui_col_end()
            ui_col() ui_button("audit", 25, "Audit toggle") ui_col_end()
        ui_row_end()
        ui_scroll_begin("dynamic-stress", 330)
            ui_push(0, 0, S.width, 0, 4)
                ui_panel(0.1, 0.18, 0.22, 1)
                ui_text(S.sample)
                S.text_h = ui_last_rect("h")
                ui_row(8, 100, -1) ui_row_align(2)
                    ui_col() ui_button("aligned", 24, "Bottom aligned") ui_col_end()
                    ui_col() ui_text(S.sample) ui_col_end()
                ui_row_end()
            ui_pop()
            S.auto_h = ui_last_rect("h")
            ui_button("follows", 24, "Following sibling: follows this frame's measured panel")
            S.follows_y = ui_last_rect("y")
            ui_scroll_begin("nested-stress", 60)
                ui_text(S.sample .. S.sample)
            ui_scroll_end()
        ui_scroll_end()
        ui_text("Measured auto=" .. string.format("%.2f", S.auto_h) .. " following.y=" .. string.format("%.2f", S.follows_y), 0.45)
        ui_text("scroll.h=" .. string.format("%.2f", ui_scroll_content_h("dynamic-stress")) .. " offset=" .. string.format("%.2f", ui_scroll_offset("dynamic-stress")), 0.45)
        ui_text("Long Chinese/English words, font/width changes, nested clipping and modal precedence.", 0.45)
        ui_text("Current state: " .. string.format("%d", S.step % 3) .. " (short / long / short)", 0.45)
    ui_pop()
    if ui_modal_begin("stress-dialog", 340, 140) then
        ui_text("Modal declared after all controls", 0.6)
        ui_text("Only this dialog receives current-frame focus/input.", 0.45)
        ui_button("close", 26, "Close / Escape / B")
        ui_modal_end()
    end
    ui_end()
    if S.last_state ~= S.step then
        S.last_state = S.step
        log_number(S.auto_h) log_number(ui_scroll_content_h("dynamic-stress")) log_number(ui_scroll_offset("dynamic-stress"))
    end
    if ui_clicked("aligned") or ui_clicked("follows") then S.hits = S.hits + 1 end
    ui_push(28, 630, 700, 20)
        ui_text("Hit validation=" .. string.format("%d", S.hits) .. " focus=" .. ui_focus() .. " aligned.y=" .. string.format("%.1f", ui_widget_rect("aligned", "y")), 0.42, "ellipsis")
    ui_pop()
    if ui_clicked("step") then S.auto = false next_state() end
    if ui_clicked("pause") then S.auto = not S.auto end
    if ui_clicked("audit") then S.audit = not S.audit end
    if ui_clicked("close") then ui_modal_close() S.auto = false end
    return 0
end
