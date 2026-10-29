-- Text & platform capabilities demo (Phase 33).
--
-- Validates, in one screen:
--   * UTF-8/CJK rendering with on-demand glyphs (font_load)
--   * CJK per-character wrapping vs Latin word wrapping (draw_text_ex)
--   * rotated text (draw_text_rotated) and shrink-to-fit text (draw_text_fit)
--   * runtime localization packs (i18n_*) with instant switching
--   * cross-platform user/saves/config paths (paths_*)
--   * binary save containers v2 (storage_blob_* + slot listing)
--   * the Steamworks adapter state (stub in normal builds)
--
-- Controls: L or the button switches language, SAVE writes a container
-- under the per-user saves directory, R resets the saved slot.

local loaded = 0 + 0
local font_ok = 0 + 0
local font_index = 0 + 0
local lang_index = 0 + 0
local save_bytes = 0 + 0
local save_ver = 0 + 0
local slot_count = 0 + 0
local header_len = 0 + 0
local cloud = 0 + 0

-- Runtime-constant candidate table; the first loadable font wins.
local fonts = {
    {path = "/System/Library/Fonts/Hiragino Sans GB.ttc", name = "Hiragino Sans GB"},
    {path = "/System/Library/Fonts/PingFang.ttc", name = "PingFang SC"},
    {path = "/System/Library/Fonts/Supplemental/Songti.ttc", name = "Songti SC"},
    {path = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", name = "Noto Sans CJK"},
    {path = "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc", name = "WenQuanYi Micro Hei"},
    {path = "C:\\Windows\\Fonts\\msyh.ttc", name = "Microsoft YaHei"},
    {path = "C:\\Windows\\Fonts\\simhei.ttf", name = "SimHei"}
}

local function font_name()
    if font_ok == 1 and font_index >= 1 and font_index <= 7 then
        return fonts[font_index].name
    end
    return i18n_t("Fallback ASCII only")
end

local function current_language_index()
    local n = i18n_language_count()
    local code = i18n_language()
    for i = 0, n - 1 do
        if i18n_language_code(i) == code then
            return i
        end
    end
    return 0
end

local function switch_language()
    local n = i18n_language_count()
    if n <= 1 then
        return
    end
    lang_index = (lang_index + 1) % n
    i18n_set_language(i18n_language_code(lang_index))
end

local function save_demo_slot()
    local dir = paths_saves_dir()
    paths_ensure_dir(dir)
    local lang = i18n_language()
    local payload = "FAKE2D-TEXT-DEMO|" .. lang .. "|payload"
    storage_blob_write("text_demo", dir, payload,
                        "name=demo;lang=" .. lang, 3)
    save_bytes = storage_blob_size("text_demo", dir)
    save_ver = storage_blob_version("text_demo", dir)
    slot_count = storage_slot_count(dir)
    header_len = string.len(storage_blob_header("text_demo", dir))
end

local function draw_background()
    local cam_x = camera_x()
    draw_quad(cam_x, 0.0, 960.0, 540.0, 0.94, 0.93, 0.88, 1.0)
    draw_quad(cam_x, 0.0, 960.0, 96.0, 0.10, 0.12, 0.16, 1.0)
    draw_quad(cam_x, 96.0, 960.0, 6.0, 0.72, 0.52, 0.24, 1.0)
end

local function draw_text_page()
    local tx = 24.0
    draw_text(i18n_t("FAKE2D TEXT & PLATFORM"), tx, 24.0, 1.15,
              0.95, 0.80, 0.45, 1.0)
    draw_text("UTF-8 / CJK / i18n / paths / save v2 / steam", tx, 62.0, 0.55,
              0.78, 0.82, 0.88, 1.0)

    local y = 126.0
    local step = 26.0
    draw_text(i18n_t("Language") .. ": " .. i18n_language()
                  .. "  [" .. i18n_language_name(lang_index) .. "]",
              tx, y, 0.66, 0.12, 0.14, 0.18, 1.0)
    y = y + step
    draw_text(i18n_t("Font") .. ": " .. font_name(), tx, y, 0.6,
              0.18, 0.20, 0.24, 1.0)
    y = y + step
    draw_text_fit(i18n_t("User data") .. ": " .. paths_user_dir(),
                  tx, y, 430.0, 0.52, 0.3, 0.28, 0.30, 0.34, 1.0)
    y = y + step
    draw_text_fit(i18n_t("Saves") .. ": " .. paths_saves_dir(),
                  tx, y, 430.0, 0.52, 0.3, 0.28, 0.30, 0.34, 1.0)
    y = y + step
    draw_text_fit(i18n_t("Config path") .. ": " .. paths_config_path(),
                  tx, y, 430.0, 0.52, 0.3, 0.28, 0.30, 0.34, 1.0)
    y = y + step + 10.0
    local steam_line = i18n_t("Steam") .. ": "
    if steam_available() then
        steam_line = steam_line .. "available"
    else
        steam_line = steam_line .. i18n_t("Steam unavailable")
    end
    if steam_cloud_enabled() then
        steam_line = steam_line .. "  (cloud on)"
    else
        steam_line = steam_line .. "  (cloud off)"
    end
    draw_text(steam_line .. "  [" .. steam_backend() .. "]  ACH " .. ach_count(),
              tx, y, 0.56, 0.20, 0.22, 0.26, 1.0)
    y = y + step
    draw_text(i18n_t("Save slot") .. ": " .. string.format("%d", save_bytes)
                  .. " " .. i18n_t("Saved bytes") .. "  "
                  .. i18n_t("Version") .. " " .. string.format("%d", save_ver)
                  .. "  header " .. string.format("%d", header_len) .. "  "
                  .. i18n_t("Slot list") .. " " .. string.format("%d", slot_count),
              tx, y, 0.52, 0.20, 0.22, 0.26, 1.0)

    -- Wrapped paragraph: CJK breaks anywhere, Latin words stay intact.
    draw_text_ex(i18n_t("CJK wraps per character; Latin words wrap at spaces.")
                     .. "  " ..
                     "The quick brown fox jumps over the lazy dog while the misty forest keeps its secrets.",
                 24.0, 330.0, 0.72, 0.14, 0.15, 0.18, 1.0, 0, 420.0)
end

local function draw_panel()
    local px = 520.0
    local py = 150.0
    ui_panel(0, px, py, 410.0, 250.0, 0.09, 0.10, 0.13, 0.96)
    ui_label(0, px + 18.0, py + 14.0, i18n_t("Tooltip"), 0.8,
             0.92, 0.84, 0.62, 1.0)
    ui_label(0, px + 18.0, py + 44.0,
             i18n_t("Hover this panel for a tooltip"), 0.56,
             0.80, 0.82, 0.86, 1.0)

    if ui_button("lang", 0, px + 18.0, py + 78.0, 190.0, 38.0,
                 i18n_t("Switch language"), 0.55) then
        switch_language()
    end
    if ui_button("save", 0, px + 220.0, py + 78.0, 170.0, 38.0,
                 i18n_t("Save"), 0.62) then
        save_demo_slot()
    end

    -- Shrink-to-fit: a long label never overflows its 360 px box.
    ui_label_fit(0, px + 18.0, py + 132.0, 370.0,
                 i18n_t("Shrink to fit")
                     .. " - long translated text shrinks to fit this box",
                 0.66, 0.42, 0.30, 0.31, 0.35, 1.0)

    -- Tooltip record is per frame; draw it after every widget.
    ui_tip(0, px, py, 410.0, 250.0, i18n_t("Hover this panel for a tooltip"))
    ui_tip_draw()
end

local function draw_footer()
    draw_text_rotated(i18n_t("Rotated text"), 742.0, 462.0, 0.9, -0.28,
                      0.16, 0.17, 0.20, 1.0)
    draw_text(i18n_t("Press L or the button to switch") .. " / U 成就",
              24.0, 500.0, 0.56, 0.28, 0.29, 0.32, 1.0)
end

function update(dt)
    if loaded == 0 then
        loaded = 1
        for i = 1, 7 do
            if font_ok == 0 and font_load(fonts[i].path, 30.0) then
                font_ok = 1
                font_index = i
            end
        end
        -- A fontless (ASCII-only) host starts in English so the demo stays
        -- readable; CJK hosts start in Chinese.
        local start = "en"
        if font_ok == 1 then
            start = "zh"
        end
        i18n_load(paths_resolve("assets/lang"), start)
        lang_index = current_language_index()

        -- Keep demo saves out of the real profile.
        local tmp = os.getenv("TMPDIR")
        if tmp == nil or tmp == "" then
            tmp = "/tmp/"
        end
        paths_set_user_dir(tmp .. "fake2d_text_demo")
        ui_set_tip_delay(0.25)
        -- Exercise the Phase 35 bindings once so their Lua conversions are
        -- covered by the demo smoke (values are unused on purpose).
        rng_seed(2026)
        rng_int(1, 6)
        ease("out_cubic", 0.5)
        stats_fps()
        window_set_min_size(640, 360)
        window_set_resizable(true)
        input_bind("jump", "space")
        save_demo_slot()
        cloud = 0
        if steam_cloud_enabled() then
            cloud = 1
        end
    end

    if input_key_pressed("l") then
        switch_language()
    end
    if input_key_pressed("u") then
        ach_unlock("ACH_TEXT_DEMO")
    end
    if input_key_pressed("r") then
        storage_slot_delete("text_demo", paths_saves_dir())
        save_bytes = 0
        save_ver = 0
        slot_count = storage_slot_count(paths_saves_dir())
    end
    if input_key_pressed("c") then
        if cloud == 1 then
            cloud = 0
        else
            cloud = 1
        end
        steam_set_cloud(cloud == 1)
    end

    draw_background()
    draw_text_page()
    draw_panel()
    draw_footer()
    return 0
end
