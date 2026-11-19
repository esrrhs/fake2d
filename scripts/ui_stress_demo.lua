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
    ui_root_begin(18, 14, 724, 650, 10, true)
        ui_panel()
        ui_vstack(6, 0, "stretch")
        ui_text("UI Two-Pass / seeded dynamic layout stress", 0.75)
        ui_text("SPACE pause | N next | R seed reset | A audit | Tab / pad focus", 0.45)
        ui_text("seed=" .. string.format("%d", S.seed) .. " step=" .. string.format("%d", S.step) .. " width=" .. string.format("%d", S.width), 0.45)
        ui_hstack(8, 0, "center")
            ui_next_grow(1) ui_button("step", "Next deterministic state")
            ui_next_grow(1) ui_button("pause", "Pause / auto")
            ui_next_grow(1) ui_button("audit", "Audit toggle")
        ui_hstack_end()
        ui_scroll("dynamic-stress", 330)
            ui_next_width(S.width)
            ui_card(4, 4, 0.1, 0.18, 0.22, 1)
                ui_remember_box("stress_auto_card")
                ui_text(S.sample)
                ui_table_begin("stress_tbl", 8, 4, 100, "grow")
                    ui_table_row("end")
                        ui_cell() ui_button("aligned", "Bottom aligned") ui_cell_end()
                        ui_cell() ui_text(S.sample) ui_cell_end()
                    ui_table_row_end()
                ui_table_end()
            ui_card_end()
            S.auto_h = ui_named("stress_auto_card", "h")
            ui_button("follows", "Following sibling: follows this frame's measured panel")
            S.follows_y = ui_widget_rect("follows", "y")
            ui_scroll("nested-stress", 60)
                ui_text(S.sample .. S.sample)
            ui_scroll_end()
        ui_scroll_end()
        ui_text("Measured auto=" .. string.format("%.2f", S.auto_h) .. " following.y=" .. string.format("%.2f", S.follows_y), 0.45)
        ui_text("scroll.h=" .. string.format("%.2f", ui_scroll_content_h("dynamic-stress")) .. " offset=" .. string.format("%.2f", ui_scroll_offset("dynamic-stress")), 0.45)
        ui_text("Long Chinese/English words, font/width changes, nested clipping and modal precedence.", 0.45)
        ui_text("Current state: " .. string.format("%d", S.step % 3) .. " (short / long / short)", 0.45)
        ui_vstack_end()
    ui_root_end()
    if ui_modal_begin("stress-dialog", 340, "auto") then
        ui_text("Modal declared after all controls", 0.6)
        ui_text("Only this dialog receives current-frame focus/input.", 0.45)
        ui_button("close", "Close / Escape / B")
        ui_modal_end()
    end
    ui_end()
    -- Observe this state after the deferred layout has been solved, not the
    -- preceding state's cached rectangle.
    S.auto_h = ui_named("stress_auto_card", "h")
    S.follows_y = ui_widget_rect("follows", "y")
    if S.last_state ~= S.step then
        S.last_state = S.step
        log_number(S.auto_h) log_number(ui_scroll_content_h("dynamic-stress")) log_number(ui_scroll_offset("dynamic-stress"))
    end
    if ui_clicked("aligned") or ui_clicked("follows") then S.hits = S.hits + 1 end
    ui_root_begin(28, 630, 700, "auto", 0, false)
        ui_text("Hit validation=" .. string.format("%d", S.hits) .. " focus=" .. ui_focus() .. " aligned.y=" .. string.format("%.1f", ui_widget_rect("aligned", "y")), 0.42, "ellipsis")
    ui_root_end()
    if ui_clicked("step") then S.auto = false next_state() end
    if ui_clicked("pause") then S.auto = not S.auto end
    if ui_clicked("audit") then S.audit = not S.audit end
    if ui_clicked("close") then ui_modal_close() S.auto = false end
    return 0
end
