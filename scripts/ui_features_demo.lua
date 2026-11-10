-- 1.48 code-driven widgets: no editor. SPACE toggles deterministic 40-frame
-- page cycling. Keys 1/2/3 select pages; Tab/arrows/pad navigate, Enter/A select.
local S = new_global_obj("ui_features_demo", "state")
function update(dt)
    if S.ready == nil then
        S.ready = true S.frame = 0 + 0 S.auto = true S.page = 0 + 0
        S.skin = sprite_load("assets/art/ore.png")
        S.disabled = false S.theme = false S.skin_on = true S.check = false
        S.slide = 0.4 + 0.0 S.split = 220 + 0.0 S.msg = "Ready"
        font_add_fallback("assets/fonts/ui_layout_test.otf", 28)
        ui_rich_icon("ore", S.skin, 0, 0, 32, 32)
        ui_text_set("notes", "Multiline UTF-8: 田亩\nShift-arrows select; Ctrl/Cmd A C X V.")
    end
    S.frame = S.frame + 1
    if input_key_pressed("space") then S.auto = not S.auto end
    if S.auto then S.page = math.floor((S.frame - 1) / 40) % 3 end
    if input_key_pressed("1") then S.page = 0 + 0 S.auto = false end
    if input_key_pressed("2") then S.page = 1 + 0 S.auto = false end
    if input_key_pressed("3") then S.page = 2 + 0 S.auto = false end
    if S.auto then
        if S.frame == 50 then S.theme = true end
        if S.frame == 60 then S.skin_on = false end
        if S.frame == 70 then S.theme = false S.skin_on = true end
        if S.frame == 85 then ui_modal_open("dialog") end
        if S.frame == 95 then ui_modal_close() S.slide = 0.9 + 0.0 end
        if S.frame == 100 then ui_popup_open("menu", 300, 180) end
        if S.frame == 108 then ui_popup_close() ui_set_scroll_offset("virtual", 440) end
    end
    if S.auto and (S.frame - 1) % 40 == 0 then log_number(148100 + S.page) end
    ui_scale(800, 620, "contain")
    ui_set_page("features", S.page)
    ui_set_audit(true)
    if S.skin_on then ui_panel_slice(S.skin, 0, 0, 32, 32, 6, 6) else ui_panel_slice(0, 0, 0, 0, 0, 0, 0) end
    ui_push(24, 18, 752, 584, 12)
        ui_panel()
        ui_text("UI CAPABILITIES / 1.48", 0.75)
        ui_text("1 containers | 2 text/theme | 3 interaction/list | SPACE auto", 0.45)
        if ui_page_begin("features", 0) then
            ui_text("Auto-height + flow + fixed/flex grid", 0.6)
            ui_flow_begin()
            local i = 0 + 0
            while i < 5 do
                ui_flow_item(130 + i * 5)
                    ui_button("flow" .. string.format("%d", i), 24 + i * 3, "flow item")
                ui_flow_item_end()
                i = i + 1
            end
            ui_flow_end()
            ui_grid_begin(3, 8, 100, -1, -2)
            i = 0 + 0
            while i < 6 do
                ui_grid_cell() ui_button("grid" .. string.format("%d", i), 28, "grid cell") ui_grid_cell_end()
                i = i + 1
            end
            ui_grid_end()
            ui_row(8, -1, -1) ui_row_align(2)
                ui_constrain(100, 300, 44, 90)
                ui_col() ui_text("Bottom-aligned short", 0.5) ui_col_end()
                ui_col() ui_text("This taller cell wraps and the row uses its measured maximum height.", 0.7) ui_col_end()
            ui_row_end()
            S.split = ui_splitter("split", 50, S.split, 40)
            ui_remember("panes")
            if ui_push_related("panes", "left", 0, S.split - 4, 50) then ui_panel(0.08, 0.18, 0.28, 1) ui_text("Left pane", 0.5) ui_pop() end
            if ui_push_related("panes", "left", S.split + 4, ui_avail_w() - S.split - 4, 50) then ui_panel(0.15, 0.12, 0.24, 1) ui_text("Right pane", 0.5) ui_pop() end
            ui_text("Drag divider. Left pane=" .. string.format("%.0f", S.split) .. " right=" .. string.format("%.0f", ui_avail_w() - S.split), 0.45)
            ui_row(8, 100, -1) ui_col() ui_text("Anchor", 0.45) ui_remember("anchor") ui_col_end() ui_row_end()
            if ui_push_related("anchor", "right", 8, 160, 24) then ui_text("related", 0.45) ui_pop() end
            ui_text("Relations can also stretch from parent edges.", 0.45)
            ui_page_end()
        end
        if ui_page_begin("features", 1) then
            if ui_button("theme", 26, "Toggle theme palette / spacing") then S.theme = not S.theme end
            if S.theme then ui_set_theme_color("button", 0.30, 0.14, 0.34, 1) ui_set_theme_metric("spacing", 14) ui_set_theme_metric("font_scale", 0.65) else ui_set_theme_color("button", 0.16, 0.22, 0.34, 1) ui_set_theme_metric("spacing", 8) ui_set_theme_metric("font_scale", 0.6) end
            if ui_button("skin", 26, "Toggle nine-slice / solid panel") then S.skin_on = not S.skin_on end
            local clicked = ui_rich_text("[color=#ffbb55][b]Rich text[/b][/color] [img=ore] [link=manual]Click this link[/link]", 0.65)
            if clicked ~= "" then S.msg = "link: " .. clicked end
            ui_image_button("icon", S.skin, 0, 0, 32, 32, 40, "Image button")
            ui_image(S.skin, 0, 0, 32, 32, 35)
            ui_text_input("single", 28, 32)
            ui_text_area("notes", 110, 4096)
            ui_text("Committed IME characters work. GLFW 3.4 has no preedit API.", 0.45)
            ui_text(S.msg, 0.45)
            ui_page_end()
        end
        if ui_page_begin("features", 2) then
            if ui_checkbox("disable", 20, S.disabled, "Disable the following controls") then S.disabled = not S.disabled end
            ui_disabled_begin(S.disabled)
                if ui_checkbox("check", 20, S.check, "Keyboard / gamepad checkbox") then S.check = not S.check end
                S.slide = ui_slider("slide", 24, S.slide, 0, 1)
                if ui_button("modal", 26, "Open modal (Escape / B cancels)") then ui_modal_open("dialog") end
                if ui_button("menu", 26, "Popup / right-click this row") then ui_popup_open("menu", 300, 180) end
                ui_context_menu("menu")
            ui_disabled_end()
            local fade = ui_tween("fade", S.slide, 0.6, dt, "smooth")
            ui_bar(12, fade, 0.35, 0.65, 0.90, 1)
            ui_push(fade * 20, 0, ui_avail_w() - 20, 26, 2)
                ui_panel(0.25, 0.40, 0.70, fade)
                ui_text("Tween: slide + alpha", 0.45)
            ui_pop()
            ui_text("1000 rows: only visible slice is requested", 0.45)
            local start = ui_list_begin("virtual", 140, 1000, 22)
            local count = ui_list_count()
            local i = 0 + 0
            while i < count do
                local n = start + i
                if ui_button("row" .. string.format("%d", n), 22, "Row " .. string.format("%d", n)) then S.msg = "selected " .. string.format("%d", n) end
                i = i + 1
            end
            ui_list_end()
            ui_text("slice " .. string.format("%d", start) .. " + " .. string.format("%d", count) .. " | " .. S.msg, 0.45)
            ui_text("Focus: " .. ui_focus(), 0.45)
            ui_page_end()
        end
    ui_pop()
    local choice = ui_popup("menu", 180, 28, "One", "Two", "Three")
    if choice >= 0 then S.msg = "menu " .. string.format("%d", choice) end
    if ui_modal_begin("dialog", 300, 150) then
        ui_text("Modal blocks controls below", 0.6)
        ui_text("Tab / arrows / pad navigate", 0.5)
        if ui_button("close", 28, "Close") then ui_modal_close() end
        ui_modal_end()
    end
    return 0
end
