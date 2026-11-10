// Shared by --ui-test, --ui-stress and --ui-bench; this is not a second test runner.
// Included inside main.cpp's anonymous namespace, after engine headers.
void RunUI150Checks(fake2d::Engine &engine, const std::function<void(bool, const char *)> &check) {
    auto &renderer = engine.GetRenderer();
    fake2d::Input input;
    fake2d::Camera2D camera;
    camera.SetViewport(640, 480);
    fake2d::UISystem ui;
    ui.Bind(&input, &camera, &renderer);
    const int target = renderer.GetRenderTargets().Create(640, 480);
    // Pixel parity with the original independently-positioned glyph path, including
    // kerning-prone pairs. Batching must not silently change rich-text advances.
    const std::string parity_text = "AVATAR";
    const fake2d::Color red{1, 0, 0, 1};
    ui.NewFrame();
    renderer.BeginFrame(0, 0, 0, 1);
    renderer.BeginRenderTarget(target);
    renderer.ClearActiveTarget({0, 0, 0, 1});
    ui.BeginFrameUI();
    ui.Push(10, 10, 300, 100);
    ui.RichText("[color=#ff0000][b][link=parity]AVATAR[/link][/b][/color]", .7f);
    ui.Pop();
    ui.EndFrameUI();
    renderer.EndRenderTarget();
    renderer.EndFrame();
    auto *parity_target = renderer.GetRenderTargets().Get(target);
    std::vector<fake2d::Color> rich_pixels;
    fake2d::Color sampled;
    if (parity_target)
        for (int y = 10; y < 40; ++y)
            for (int x = 10; x < 150; ++x) {
                parity_target->ReadPixel(x, y, sampled);
                rich_pixels.push_back(sampled);
            }
    // Independently measured unkerned advances, but the same compositing
    // order as RichText's batched flush: one underline, then the base run,
    // then the fake-bold offset run. Interleaving base+bold per glyph
    // changes overlap blending on some software renderers (CGL / Apple
    // Software Renderer) even when advances match. A batched run that
    // applies kerning or a wrong width still fails the pixel compare.
    std::vector<float> pens;
    float pen = 10;
    const float line_h = renderer.GetFont().LineHeight() * .7f;
    for (std::size_t i = 0; i < parity_text.size(); ++i) {
        pens.push_back(pen);
        const auto ch = std::string_view(parity_text).substr(i, 1);
        pen += renderer.MeasureText(ch, .7f);
    }
    renderer.BeginFrame(0, 0, 0, 1);
    renderer.BeginRenderTarget(target);
    renderer.ClearActiveTarget({0, 0, 0, 1});
    if (!pens.empty())
        renderer.DrawQuad({pens.front(), 10 + line_h - 2, pen - pens.front(), 1}, red);
    for (std::size_t i = 0; i < parity_text.size(); ++i) {
        const auto ch = std::string_view(parity_text).substr(i, 1);
        renderer.DrawText(ch, pens[i], 10, .7f, red);
    }
    for (std::size_t i = 0; i < parity_text.size(); ++i) {
        const auto ch = std::string_view(parity_text).substr(i, 1);
        renderer.DrawText(ch, pens[i] + .7f, 10, .7f, red);
    }
    renderer.EndRenderTarget();
    renderer.EndFrame();
    bool parity = parity_target != nullptr;
    std::size_t pixel_index = 0;
    if (parity_target)
        for (int y = 10; y < 40; ++y)
            for (int x = 10; x < 150; ++x) {
                parity_target->ReadPixel(x, y, sampled);
                const auto expected = rich_pixels[pixel_index++];
                parity = parity && std::abs(sampled.r - expected.r) < .01f &&
                         std::abs(sampled.g - expected.g) < .01f;
            }
    check(parity, "150: batched bold/color/link pixels preserve original unkerned glyph positions");
    std::vector<std::string> ids, texts;
    const std::string chinese = "能吏抚恤百姓修复水利田亩营制长句自动换行国朝纪事";
    for (int i = 0; i < 1200; ++i) {
        ids.push_back("converge/" + std::to_string(i));
        texts.push_back(i % 3 == 0 ? chinese + chinese + chinese :
                        i % 3 == 1 ? "English mixed-height wrapped paragraph with population 12345678901234567890" :
                                     "田 short");
    }
    check(ui.ConfigureVariableList("converge", ids, 24, "text"), "150: configure 1200 mixed rows");
    for (int i = 0; i < 1200; ++i)
        check(ui.SetVariableRowText("converge", ids[i], texts[i], .55f), "150: text metadata accepted");
    float width = 300;
    bool pin = false;
    auto frame = [&] {
        input.InjectFrame({630, 470}, {}, {});
        ui.NewFrame();
        renderer.BeginFrame(0, 0, 0, 1);
        renderer.BeginRenderTarget(target);
        check(ui.BeginFrameUI(), "150: scoped frame begins");
        ui.Push(10, 10, width, 400);
        check(ui.VariableListBegin("converge", 260, pin), "150: list begins");
        for (int i = ui.VariableListNext(); i >= 0; i = ui.VariableListNext()) {
            ui.Text(texts[i], .55f);
            ui.VariableListRowEnd();
        }
        ui.VariableListEnd();
        ui.Button("scoped150", 20, "Scoped");
        ui.Pop();
        check(ui.EndFrameUI(), "150: frame ends");
        renderer.EndRenderTarget();
        renderer.EndFrame();
        check(ui.Stats().premeasured_rows <= 64, "150: bounded premeasurement");
    };
    frame();
    check(!ui.VariableListMeasured("converge", ids.back()), "150: unseen far tail initially estimated");
    ui.SetScrollOffset("converge", 15000);
    frame();
    const auto anchor = ui.VariableListAnchor("converge");
    auto inset = [&] {
        float prefix = 0;
        for (const auto &id : ids) {
            if (id == anchor)
                break;
            prefix += ui.VariableListHeight("converge", id);
        }
        return ui.ScrollOffset("converge") - prefix;
    };
    const float initial_inset = inset();
    float jump = 0;
    for (int i = 0; i < 24; ++i) {
        frame();
        check(ui.VariableListAnchor("converge") == anchor, "150: background convergence preserves visible ID");
        jump = std::max(jump, std::abs(inset() - initial_inset));
    }
    check(jump < .1f, "150: background convergence anchor displacement below 0.1 pixel");
    for (const auto &id : ids)
        check(ui.VariableListMeasured("converge", id), "150: all 1200 estimates converge");
    std::printf("UI_CONVERGENCE rows=1200 budget=64 max_anchor_displacement=%.6f pixels\n", jump);
    ui.SetScrollOffset("converge", 0);
    frame();
    check(ui.ScrollOffset("converge") == 0, "150: scroll back without drift");
    width = 190;
    frame();
    check(!ui.VariableListMeasured("converge", ids.back()), "150: width invalidates distant cached height");
    ui.SetThemeMetric("font_scale", .8f);
    frame();
    check(!ui.VariableListMeasured("converge", ids.back()), "150: theme font scale invalidation");
    const auto short_h = ui.VariableListHeight("converge", ids[0]);
    texts[0] += texts[0] + texts[0];
    ui.SetVariableRowText("converge", ids[0], texts[0], .55f);
    frame();
    check(ui.VariableListHeight("converge", ids[0]) > short_h, "150: short to long metadata updates height");
    texts[0] = "short";
    ui.SetVariableRowText("converge", ids[0], texts[0], .55f);
    frame();
    check(ui.VariableListHeight("converge", ids[0]) < short_h, "150: long to short height shrinks");
    ids.insert(ids.begin(), "inserted150");
    texts.insert(texts.begin(), "new row");
    ui.ConfigureVariableList("converge", ids, 24, "text");
    ui.SetVariableRowText("converge", ids[0], texts[0], .55f);
    frame();
    ids.erase(ids.begin()); texts.erase(texts.begin());
    ui.ConfigureVariableList("converge", ids, 24, "text");
    pin = true;
    frame();
    check(std::abs(ui.ScrollOffset("converge") - (ui.VariableListTotal("converge") - 260)) < .1f,
          "150: pin bottom survives estimates and insertion/deletion");
    ids.resize(3); texts.assign(3, "short");
    ui.ConfigureVariableList("converge", ids, 24, "shrunk");
    for (int i = 0; i < 3; ++i)
        ui.SetVariableRowText("converge", ids[i], texts[i], .55f);
    frame();
    check(ui.VariableListTotal("converge") < 260 && ui.ScrollOffset("converge") == 0,
          "150: shrink removes blank tail and stale scroll offset");
    check(ui.UnscopedWarnings() == 0, "150: scoped interactive calls are silent");
    // Interactive declarations after End are unscoped too.
    ui.Push(0, 0, 100, 100);
    ui.Button("legacy150", 20, "Legacy");
    ui.Button("legacy150b", 20, "Legacy");
    ui.Pop();
    check(ui.UnscopedWarnings() == 1, "150: unscoped use warns once, not per widget");
    ui.NewFrame();
    ui.Push(0, 0, 100, 100); ui.Button("legacy150", 20, "Legacy"); ui.Pop();
    check(ui.UnscopedWarnings() == 1, "150: legacy diagnostic remains rate limited");
    input.SetClipboard("田 clipboard\nEnglish");
    check(input.Clipboard() == "田 clipboard\nEnglish", "150: portable headless clipboard roundtrip");
    renderer.GetRenderTargets().Destroy(target);
}

void RunUI149Checks(fake2d::Engine &engine, const std::function<void(bool, const char *)> &check) {
    using fake2d::UISystem;
    auto &renderer = engine.GetRenderer();
    fake2d::Input input;
    fake2d::Camera2D camera;
    camera.SetViewport(256, 256);
    UISystem ui;
    ui.Bind(&input, &camera, &renderer);
    ui.SetAudit(true);
    const int target = renderer.GetRenderTargets().Create(256, 256);
    check(target > 0, "149: isolated render target");
    auto begin = [&] {
        ui.NewFrame();
        renderer.BeginFrame(0, 0, 0, 1);
        renderer.BeginRenderTarget(target);
        renderer.ClearActiveTarget({0, 0, 0, 1});
        check(ui.BeginFrameUI(), "149: begin declaration once");
    };
    auto end = [&] {
        check(ui.EndFrameUI(), "149: resolve balanced declaration");
        renderer.EndRenderTarget();
        renderer.EndFrame();
    };
    auto controls = [&](bool reverse = false, bool remove = false, bool disabled = false) {
        begin();
        ui.Push(10, 10, 220, 220);
        ui.DisabledBegin(disabled);
        if (reverse && !remove)
            ui.Button("b", 24, "B");
        ui.Button("a", 24, "A");
        if (!reverse && !remove)
            ui.Button("b", 24, "B");
        ui.DisabledEnd();
        ui.Pop();
        end();
    };
    input.InjectFrame({250, 250}, {"tab"}, {});
    controls();
    check(ui.FocusedWidget() == "a", "149: first-appearance Tab discovers this declaration");
    check(!ui.BeginFrameUI(), "149: cannot accidentally replay a completed frame");
    input.InjectFrame({250, 250}, {}, {});
    controls(true);
    input.InjectFrame({250, 250}, {"tab"}, {});
    controls(true);
    check(ui.FocusedWidget() == "b", "149: Tab uses reordered current ring");
    input.InjectFrame({250, 250}, {"enter"}, {});
    controls(false, true);
    check(ui.FocusedWidget().empty() && !ui.Clicked("a"),
          "149: removed focus cannot activate a replacement");
    input.InjectFrame({250, 250}, {"tab"}, {});
    controls(false, false, true);
    check(ui.FocusedWidget().empty(),
          "149: disabled current controls are absent from focus discovery");
    input.InjectFrame({250, 250}, {}, {});
    controls();
    input.InjectFrame({250, 250}, {"left_shift", "tab"}, {});
    controls();
    check(ui.FocusedWidget() == "b", "149: Shift-Tab on first eligible frame enters last control");
    input.InjectFrame({250, 250}, {"up"}, {});
    controls();
    check(ui.FocusedWidget() == "a", "149: arrows navigate current controls");
    fake2d::InputFrameSnapshot pad{};
    pad.pad_connected[0] = 1;
    pad.SetPadButton(0, 13, true);
    input.ApplySnapshot(pad);
    controls();
    check(ui.FocusedWidget() == "b", "149: gamepad dpad navigation is same-frame");
    pad.SetPadButton(0, 13, false);
    pad.SetPadButton(0, 0, true);
    input.ApplySnapshot(pad);
    controls();
    check(ui.Clicked("b"), "149: gamepad South activates resolved focus");
    pad.SetPadButton(0, 0, false);
    pad.pad_axes[0][1] = -.8f;
    input.ApplySnapshot(pad);
    controls();
    check(ui.FocusedWidget() == "a", "149: left stick edge navigates current focus");
    input.ApplySnapshot({});
    // A tall second column changes the FIRST column's final y in the same declaration.
    const std::string paragraph = "能吏抚恤百姓修复水利田亩营制长句自动换行国朝纪事";
    for (int state = 0; state < 3; ++state) {
        const std::string message = state == 1 ? paragraph + paragraph + paragraph : "short";
        const float height = std::max(20.0f, renderer.GetFont().MeasureTextEx(message, .5f, 140));
        const float scroll = std::max(0.0f, height + 20 - 160);
        for (int edge = 0; edge < 2; ++edge) {
            input.InjectFrame({30, 10 + height - 20 - scroll + 6}, {},
                              edge == 0 ? std::vector<std::int64_t>{0}
                                        : std::vector<std::int64_t>{});
            begin();
            ui.SetScrollOffset("aligned", 10000);
            ui.Push(10, 10, 220, 220);
            ui.ScrollBegin("aligned", 160);
            ui.Push(0, 0, 208, 0);
            ui.Row(8, {60, -1});
            ui.RowAlign(2);
            ui.Col();
            ui.Button("aligned_button", 20, "aligned");
            ui.ColEnd();
            ui.Col();
            ui.Text(message, .5f);
            ui.ColEnd();
            ui.RowEnd();
            ui.Pop();
            ui.Button("following", 20, "after auto");
            ui.ScrollEnd();
            ui.Pop();
            end();
            const auto rect = ui.WidgetRect("aligned_button"), after = ui.WidgetRect("following");
            check(std::abs(rect.y - (10 + height - 20 - scroll)) < .02f,
                  "149: short-long-short hit rectangle uses measured row, auto box and clamped "
                  "scroll");
            check(std::abs(after.y - (10 + height - scroll)) < .02f,
                  "149: following sibling grows AND shrinks without overlap");
            check(ui.Clicked("aligned_button") == static_cast<bool>(edge),
                  "149: click uses new aligned geometry, never previous-frame offsets");
            check(ui.OverflowCount() == 0,
                  "149: aligned dynamic geometry has no unintended audit entries");
        }
    }
    // The input remains in reference points even when the logical viewport changes now.
    for (int size : {128, 256, 192}) {
        camera.SetViewport(static_cast<float>(size), static_cast<float>(size));
        ui.SetScale(256, 256, "contain");
        const float scale = static_cast<float>(size) / 256;
        for (int edge = 0; edge < 2; ++edge) {
            input.InjectFrame({30 * scale, 25 * scale}, {},
                              edge == 0 ? std::vector<std::int64_t>{0}
                                        : std::vector<std::int64_t>{});
            begin();
            ui.Push(20, 20, 100, 40);
            ui.Button("scaled", 24, "scaled");
            ui.Pop();
            end();
            check(ui.Clicked("scaled") == static_cast<bool>(edge),
                  "149: first resized/scaled frame hit-tests inverse reference mapping");
        }
    }
    camera.SetViewport(256, 256);
    ui.SetScale(0, 0);
    // A modal declared after a button must win even on its first frame.
    for (int edge = 0; edge < 2; ++edge) {
        input.InjectFrame({30, 25}, {},
                          edge == 0 ? std::vector<std::int64_t>{0} : std::vector<std::int64_t>{});
        begin();
        ui.Push(20, 20, 160, 100);
        ui.Button("under", 24, "under");
        ui.Pop();
        ui.OpenModal("late");
        if (ui.ModalBegin("late", 150, 90)) {
            ui.Button("dialog", 24, "dialog");
            ui.ModalEnd();
        }
        end();
        check(!ui.Clicked("under") && !ui.IsHovered("under"),
              "149: late modal has current-frame pointer precedence");
    }
    ui.CloseModal();
    input.InjectFrame({250, 250}, {"tab"}, {});
    begin();
    ui.Push(20, 20, 160, 100);
    ui.Button("under", 24, "under");
    ui.Pop();
    ui.OpenModal("late");
    if (ui.ModalBegin("late", 150, 90)) {
        ui.Button("dialog", 24, "dialog");
        ui.ModalEnd();
    }
    end();
    check(ui.FocusedWidget() == "dialog", "149: first modal Tab cannot focus underlying controls");
    ui.CloseModal();
    // Painter insertion points preserve layering: later solid panels cover earlier buttons.
    input.InjectFrame({250, 250}, {}, {});
    begin();
    ui.Push(10, 10, 120, 80);
    ui.Button("covered", 24, "covered");
    ui.PushRelated("", "left", 0, 120, 24);
    ui.Panel({1, 0, 0, 1}, {0, 0, 0, 0});
    ui.Pop();
    ui.Pop();
    end();
    fake2d::Color pixel{};
    auto *rt = renderer.GetRenderTargets().Get(target);
    check(rt && rt->ReadPixel(20, 20, pixel) && pixel.r > .9f && pixel.g < .1f,
          "149: deferred interactive painters retain declaration draw order");
    // Rich-link glyph nodes share one focus ID without resetting each other's press state.
    for (int edge = 0; edge < 2; ++edge) {
        input.InjectFrame({22, 24}, {},
                          edge == 0 ? std::vector<std::int64_t>{0} : std::vector<std::int64_t>{});
        begin();
        ui.Push(20, 20, 180, 50);
        ui.RichText("[link=manual][color=#ff0000]Read[/color][/link]", .6f);
        ui.Pop();
        end();
        check(edge == 0 ? ui.ClickedLink().empty() : ui.ClickedLink() == "manual",
              "149: resolved rich link reports once across multiple glyphs");
    }
    ui.OpenPopup("popup149", 20, 20);
    auto popup_frame = [&] {
        begin();
        ui.Popup("popup149", {"one", "two", "three"}, 160, 26);
        end();
    };
    input.InjectFrame({250, 250}, {}, {});
    popup_frame();
    input.InjectFrame({250, 250}, {"down"}, {});
    popup_frame();
    input.InjectFrame({250, 250}, {"enter"}, {});
    popup_frame();
    check(ui.ResultNumber("popup149", -1) == 1,
          "149: popup current-ring down/enter reports parent result and dismisses");
    input.InjectFrame({250, 250}, {}, {});
    popup_frame();
    check(ui.WidgetRect("popup149/0").width == 0,
          "149: dismissed popup contributes no stale focus node");
    ui.OpenPopup("outside149", 150, 150);
    for (int edge = 0; edge < 2; ++edge) {
        input.InjectFrame({25, 25}, {},
                          edge == 0 ? std::vector<std::int64_t>{0} : std::vector<std::int64_t>{});
        begin();
        ui.Push(20, 20, 100, 80);
        ui.Button("dismiss-under", 24, "under");
        ui.Pop();
        ui.Popup("outside149", {"one", "two"}, 90, 24);
        end();
        check(!ui.Clicked("dismiss-under"),
              "149: outside popup dismissal never clicks through below");
    }
    // The explicit barrier is necessary only when a virtual slice depends on
    // a viewport whose final row alignment / outer scroll is not known yet.
    for (int state = 0; state < 3; ++state) {
        const std::string message = state == 1 ? paragraph + paragraph + paragraph : "short";
        const float row_height =
            std::max(60.0f, renderer.GetFont().MeasureTextEx(message, .5f, 112));
        const float outer_offset = std::max(0.0f, row_height + 20 - 180);
        const float view_y = 20 + row_height - 60 - outer_offset;
        for (int edge = 0; edge < 2; ++edge) {
            input.InjectFrame({35, view_y + 5}, {},
                              edge == 0 ? std::vector<std::int64_t>{0}
                                        : std::vector<std::int64_t>{},
                              edge == 0 ? -3 : 0);
            begin();
            ui.SetScrollOffset("late-outer", 10000);
            ui.Push(20, 20, 232, 220);
            ui.ScrollBegin("late-outer", 180);
            ui.Row(8, {100, -1});
            ui.RowAlign(2);
            ui.Col();
            check(ui.ListView("late-view", 60),
                  "149: reserve list viewport before its aligned y is known");
            ui.ColEnd();
            ui.Col();
            ui.Text(message, .5f);
            ui.ColEnd();
            ui.RowEnd();
            ui.Button("late-after", 20, "after");
            ui.ScrollEnd();
            ui.Pop();
            check(!ui.ListViewBegin("late-view"),
                  "149: late-bound rows require an explicit balanced layout barrier");
            check(ui.EndLayout(),
                  "149: barrier resolves final scroll and aligned view, without caller replay");
            check(ui.ListViewBegin("late-view"), "149: visible rows enter the final-position view");
            const auto slice = ui.ListBegin("late-rows", 60, 1000, 20);
            for (int i = 0; i < slice.count; ++i)
                ui.Button("late-row/" + std::to_string(slice.first + i), 20, "row");
            ui.ListEnd();
            check(ui.ListViewEnd(), "149: late-bound view restores declaration state");
            end();
            const int expected_first = (state + 1) * 6;
            const auto first_rect = ui.WidgetRect("late-row/" + std::to_string(expected_first));
            check(
                slice.first == expected_first && slice.count == 3,
                "149: wheel uses CURRENT aligned viewport before requesting exactly the new slice");
            check(std::abs(ui.ScrollOffset("late-outer") - outer_offset) < .02f,
                  "149: reserved virtual viewport prevents outer scroll consuming the same wheel");
            check(std::abs(first_rect.y - view_y) < .02f,
                  "149: late virtual rows follow current grow-shrink and outer clamp geometry");
            check(ui.Clicked("late-row/" + std::to_string(expected_first)) ==
                      static_cast<bool>(edge),
                  "149: current late-bound virtual row receives its own click");
            check(ui.OverflowCount() == 0,
                  "149: two-stage list layout retains intersected clips and audit");
        }
    }
    const float blocked_offset = ui.ScrollOffset("late-rows");
    input.InjectFrame({35, 25}, {}, {}, -3);
    begin();
    ui.Push(20, 20, 150, 100);
    ui.ListView("blocked", 60);
    ui.Pop();
    ui.OpenModal("late-view-modal");
    if (ui.ModalBegin("late-view-modal", 120, 80)) {
        ui.Button("only-dialog", 24, "dialog");
        ui.ModalEnd();
    }
    check(ui.EndLayout(), "149: modal precedence is established before virtual input");
    check(ui.ListViewBegin("blocked"), "149: blocked list can still paint under modal");
    auto blocked = ui.ListBegin("late-rows", 60, 1000, 20);
    for (int i = 0; i < blocked.count; ++i)
        ui.Button("blocked-row/" + std::to_string(blocked.first + i), 20, "blocked");
    ui.ListEnd();
    ui.ListViewEnd();
    end();
    check(std::abs(ui.ScrollOffset("late-rows") - blocked_offset) < .02f,
          "149: late-declared modal blocks underlying virtual wheel in same frame");
    ui.CloseModal();

    // Fixed and variable virtual lists keep workload proportional to the viewport.
    std::vector<std::string> ids;
    for (int i = 0; i < 1200; ++i)
        ids.push_back("id" + std::to_string(i));
    check(ui.ConfigureVariableList("mixed", ids, 32, "v1"), "149: configure 1200 stable row IDs");
    check(!ui.ConfigureVariableList("invalid", {"a", "a"}, 24),
          "149: duplicate row IDs rejected atomically");
    bool active_mutation_checked = false;
    auto list_frame = [&](float width = 200, bool bottom = false, bool short_rows = false) {
        input.InjectFrame({250, 250}, {}, {});
        begin();
        ui.Push(10, 10, width, 200);
        check(ui.ListView("mixed", 100), "149: reserve virtual viewport in parent");
        ui.Pop();
        check(ui.EndLayout(), "149: resolve viewport before visible-row declaration");
        check(ui.ListViewBegin("mixed"), "149: enter final-layout virtual viewport");
        check(ui.VariableListBegin("mixed", 100, bottom), "149: variable list begins");
        std::vector<int> submitted;
        for (int row = ui.VariableListNext(); row >= 0; row = ui.VariableListNext()) {
            submitted.push_back(row);
            if (!active_mutation_checked) {
                check(!ui.ConfigureVariableList("mixed", {"invalid-active-mutation"}, 32),
                      "149: changing IDs inside an active row is rejected safely");
                active_mutation_checked = true;
            }
            const int data_index = std::atoi(ids[row].c_str() + 2);
            const auto text =
                short_rows            ? std::string("short")
                : data_index % 3 == 0 ? paragraph + paragraph
                : data_index % 3 == 1
                    ? std::string("LongUnbreakableEnglishWordThatMustWrapInsideTheCell")
                    : std::string("田 English");
            ui.Text(text, .5f);
            ui.Button(ids[row], 18, "row");
            ui.VariableListRowEnd();
            if (submitted.size() > 150) {
                check(false, "149: virtualization convergence bounded");
                break;
            }
        }
        ui.VariableListEnd();
        check(ui.ListViewEnd(), "149: close late-bound virtual viewport");
        end();
        check(submitted.size() < 15, "149: only a visible window of 1200 mixed rows is submitted");
        check(ui.Stats().tree_steps < 1500,
              "149: Fenwick lookup/update complexity is viewport-sized");
        check(ui.OverflowCount() == 0,
              "149: variable rows have intersecting clips and no false audit");
        return submitted;
    };
    auto first = list_frame();
    check(!first.empty() && first.front() == 0, "149: variable first visible row is correct");
    ui.SetScrollOffset("mixed", 700);
    first = list_frame();
    auto anchor = ui.VariableListAnchor("mixed");
    const float old_offset = ui.ScrollOffset("mixed");
    const auto anchor_rect = ui.WidgetRect(anchor);
    ids.insert(ids.begin(), "inserted");
    ui.ConfigureVariableList("mixed", ids, 32, "v1");
    list_frame();
    check(ui.VariableListAnchor("mixed") == anchor &&
              std::abs(ui.ScrollOffset("mixed") - old_offset - 32) < .02f,
          "149: inserting above preserves stable visible ID and intra-row anchor");
    check(std::abs(ui.WidgetRect(anchor).y - anchor_rect.y) < .02f,
          "149: insertion causes no visible anchored-row jump");
    ids.erase(ids.begin());
    ui.ConfigureVariableList("mixed", ids, 32, "v1");
    list_frame();
    check(ui.VariableListAnchor("mixed") == anchor, "149: deleting above preserves anchor");
    auto removed = std::find(ids.begin(), ids.end(), anchor);
    const std::string successor = removed + 1 != ids.end() ? *(removed + 1) : *(removed - 1);
    ids.erase(removed);
    ui.ConfigureVariableList("mixed", ids, 32, "v1");
    list_frame();
    check(ui.VariableListAnchor("mixed") == successor,
          "149: deleting the anchor chooses nearest surviving successor");
    anchor = successor;
    check(ui.VariableListID("mixed", 0) == ids[0] && ui.VariableListID("mixed", -1).empty(),
          "149: row ID lookup is bounded and stable");
    check(ui.VariableListMeasured("mixed", anchor) && ui.VariableListHeight("mixed", anchor) > 0,
          "149: estimated versus measured row metadata is explicit");
    const std::string inset_id =
        std::find(ids.begin(), ids.end(), "id30") != ids.end() ? "id30" : "id33";
    float prefix = 0;
    for (const auto &id : ids) {
        if (id == inset_id)
            break;
        prefix += ui.VariableListHeight("mixed", id);
    }
    ui.SetScrollOffset("mixed", prefix);
    list_frame();
    check(ui.VariableListHeight("mixed", inset_id) > 42,
          "149: mixed list contains a tall measured anchor row");
    ui.SetScrollOffset("mixed", prefix + 40);
    list_frame();
    anchor = ui.VariableListAnchor("mixed");
    check(anchor == inset_id, "149: explicit scroll chooses the measured tall row inset");
    const float before_invalidation = ui.ScrollOffset("mixed");
    ui.InvalidateVariableList("mixed", anchor);
    list_frame();
    check(ui.VariableListAnchor("mixed") == anchor &&
              std::abs(ui.ScrollOffset("mixed") - before_invalidation) < .02f,
          "149: invalidating an anchor preserves its inset until actual height is remeasured");
    list_frame(120);
    check(ui.VariableListAnchor("mixed") == anchor,
          "149: width invalidation preserves anchor while visible heights remeasure");
    ui.SetThemeMetric("font_scale", .75f);
    list_frame(120);
    check(ui.VariableListAnchor("mixed") == anchor,
          "149: font-scale invalidation preserves stable row ID");
    ui.SetThemeMetric("font_scale", .6f);
    ui.InvalidateVariableList("mixed", anchor);
    list_frame(120, true, true);
    check(std::abs(ui.ScrollOffset("mixed") - std::max(0.0f, ui.VariableListTotal("mixed") - 100)) <
              .02f,
          "149: intentional bottom pin converges during remeasurement");
    ids.resize(2);
    ui.ConfigureVariableList("mixed", ids, 32, "short");
    list_frame(200, false, true);
    check(ui.ScrollOffset("mixed") == 0 && std::abs(ui.VariableListTotal("mixed") - 64) < .02f,
          "149: content shrink clamps range immediately with no old blank tail");
    // Highly overestimated bottom-pinned rows must request predecessors in this same frame.
    ids.clear();
    for (int i = 0; i < 1200; ++i)
        ids.push_back("pin" + std::to_string(i));
    ui.ConfigureVariableList("mixed", ids, 150, "pin");
    first = list_frame(200, true, true);
    check(first.size() >= 4 && first.size() < 10,
          "149: bottom-pin estimate shrink fills its viewport without drawing all rows");
    // UTF-8 editing, history, drag selection, word selection and safe password clipboard.
    const auto font_revision = renderer.GetFont().Revision();
    renderer.GetFont().ClearFallbacks();
    renderer.AddFallbackFont("assets/fonts/ui_layout_test.otf", 28);
    check(renderer.GetFont().Revision() > font_revision,
          "149: real font/fallback changes advance layout revision");
    double editing_time = 0;
    ui.SetInteractionClock([&] { return editing_time; });
    auto edit_frame = [&](const std::vector<std::string_view> &keys,
                          const std::vector<unsigned int> &chars = {},
                          const std::vector<std::int64_t> &buttons = {},
                          fake2d::Vec2 mouse = {25, 25}, bool password = false) {
        editing_time += .04;
        input.InjectFrame(mouse, keys, buttons, 0, chars);
        begin();
        ui.Push(20, 20, 200, 120);
        if (password)
            ui.TextPassword("edit149", 28);
        else
            ui.TextArea("edit149", 100);
        ui.Pop();
        end();
    };
    ui.SetTextValue("edit149", "AB");
    edit_frame({}, {0x7530}, {0}, {26, 28});
    check(ui.TextValue("edit149") == "田AB",
          "149: same-snapshot mouse placement happens before committed UTF-8 insertion");
    edit_frame({}, {}, {}, {26, 28});
    ui.SetTextValue("edit149", "");
    edit_frame({}, {}, {0});
    edit_frame({}, {'A', 0x7530});
    edit_frame({"enter"});
    edit_frame({}, {'B'});
    check(ui.TextValue("edit149") == "A田\nB",
          "149: current-frame text focus accepts mixed UTF-8 and newline");
    edit_frame({"left_control", "z"});
    check(ui.TextValue("edit149") == "A田\n", "149: Ctrl-Z undoes one edit transaction");
    edit_frame({"left_control", "y"});
    check(ui.TextValue("edit149") == "A田\nB", "149: Ctrl-Y redoes UTF-8 transaction");
    check(ui.Undo("edit149") && ui.Redo("edit149"),
          "149: C++/Lua history API shares keyboard stacks");
    edit_frame({}, {});
    edit_frame({"left_control", "z"});
    edit_frame({}, {});
    edit_frame({"left_control", "left_shift", "z"});
    check(ui.TextValue("edit149") == "A田\nB", "149: Ctrl/Cmd Shift-Z redo is deterministic");
    edit_frame({"left"});
    check(ui.FocusedWidget() == "edit149" && ui.Selection("edit149", false) == 5,
          "149: focused multiline owns caret arrows instead of moving focus");
    edit_frame({"end"});
    edit_frame({}, {});
    edit_frame({"left_control", "z"});
    edit_frame({}, {'C'});
    check(!ui.Redo("edit149") && ui.TextValue("edit149") == "A田\nC",
          "149: new edit invalidates redo, underlying UTF-8 preserved");
    const float cjk_end = 26 + renderer.MeasureText("田亩", .6f);
    ui.SetTextValue("edit149", "田亩 English");
    edit_frame({}, {}, {0}, {26, 28});
    edit_frame({}, {}, {0}, {cjk_end, 28});
    edit_frame({}, {}, {}, {cjk_end, 28});
    check(ui.Selection("edit149", false) == 0 && ui.Selection("edit149", true) == 6,
          "149: mouse drag selects complete Chinese UTF-8 codepoints");
    // Two quick presses in the ASCII word (same deterministic injected position).
    ui.SetTextValue("edit149", "hello world");
    edit_frame({}, {}, {0}, {35, 28});
    edit_frame({}, {}, {}, {35, 28});
    edit_frame({}, {}, {0}, {35, 28});
    check(ui.Selection("edit149", false) == 0 && ui.Selection("edit149", true) == 5,
          "149: double click selects a word, not a byte");
    edit_frame({}, {}, {}, {35, 28});
    ui.SetTextValue("edit149", "secret田");
    input.SetClipboard("keep this clipboard");
    edit_frame({"left_control", "a"}, {}, {}, {25, 25}, true);
    edit_frame({"left_control", "c"}, {}, {}, {25, 25}, true);
    check(input.Clipboard() == "keep this clipboard" && ui.TextValue("edit149") == "secret田",
          "149: password copy never exports underlying value");
    edit_frame({"left_control", "x"}, {}, {}, {25, 25}, true);
    check(input.Clipboard() == "keep this clipboard" && ui.TextValue("edit149").empty(),
          "149: password cut deletes without leaking clipboard");
    check(ui.Undo("edit149") && ui.TextValue("edit149") == "secret田",
          "149: password history restores raw value, never masking data");
    input.SetClipboard("paste田");
    edit_frame({"left_control", "a"}, {}, {}, {25, 25}, true);
    edit_frame({"left_control", "v"}, {}, {}, {25, 25}, true);
    check(ui.TextValue("edit149") == "paste田" && input.Clipboard() == "paste田",
          "150: password paste accepts clipboard without exporting secret");
    ui.SetTextValue("edit149", "A田B");
    edit_frame({}, {}, {}, {250, 250}, true);
    std::vector<fake2d::Color> masked;
    if (rt)
        for (int y = 20; y < 48; ++y)
            for (int x = 20; x < 220; ++x) {
                rt->ReadPixel(x, y, pixel);
                masked.push_back(pixel);
            }
    ui.SetTextValue("edit149", "***");
    edit_frame({}, {}, {}, {250, 250}, true);
    bool identical = true;
    std::size_t p = 0;
    if (rt)
        for (int y = 20; y < 48; ++y)
            for (int x = 20; x < 220; ++x) {
                rt->ReadPixel(x, y, pixel);
                identical = identical && std::abs(masked[p].r - pixel.r) < .001f &&
                            std::abs(masked[p].g - pixel.g) < .001f;
                ++p;
            }
    check(identical && ui.TextValue("edit149") == "***",
          "149: password renders identical masks per Unicode codepoint");
    auto single_frame = [&] {
        begin();
        ui.Push(20, 20, 200, 100);
        ui.TextInput("single149", 28);
        ui.Button("single-next", 24, "next");
        ui.Pop();
        end();
    };
    input.InjectFrame({250, 250}, {"tab"}, {});
    single_frame();
    check(ui.FocusedTextInput() == "single149",
          "149: Tab focuses first-appearance single-line editor");
    input.InjectFrame({250, 250}, {"enter"}, {});
    single_frame();
    check(ui.FocusedTextInput().empty(), "149: single-line Enter commits and blurs editing");
    input.InjectFrame({250, 250}, {}, {});
    single_frame();
    input.InjectFrame({250, 250}, {"enter"}, {});
    single_frame();
    check(ui.FocusedTextInput() == "single149",
          "149: Enter reactivates a blurred text focus stop without immediately blurring again");
    input.InjectFrame({250, 250}, {}, {});
    single_frame();
    input.InjectFrame({250, 250}, {"enter"}, {});
    single_frame();
    fake2d::InputFrameSnapshot edit_pad{};
    edit_pad.pad_connected[0] = 1;
    edit_pad.SetPadButton(0, 0, true);
    input.ApplySnapshot(edit_pad);
    single_frame();
    check(ui.FocusedTextInput() == "single149", "149: gamepad A activates text editing");
    edit_pad.SetPadButton(0, 0, false);
    edit_pad.SetPadButton(0, 1, true);
    input.ApplySnapshot(edit_pad);
    single_frame();
    check(ui.FocusedTextInput().empty(), "149: gamepad B cancels text editing");
    edit_pad.SetPadButton(0, 1, false);
    edit_pad.SetPadButton(0, 13, true);
    input.ApplySnapshot(edit_pad);
    single_frame();
    check(ui.FocusedWidget() == "single-next",
          "149: cancelled text editing releases dpad navigation");
    renderer.GetRenderTargets().Destroy(target);
}

void RunUIStressChecks(fake2d::Engine &engine, std::uint32_t seed, int frames,
                       const std::function<void(bool, const char *)> &check) {
    auto &renderer = engine.GetRenderer();
    fake2d::Input input;
    fake2d::Camera2D camera;
    camera.SetViewport(320, 240);
    fake2d::UISystem ui;
    ui.Bind(&input, &camera, &renderer);
    ui.SetAudit(true);
    const int target = renderer.GetRenderTargets().Create(320, 240);
    const std::string chinese = "能吏抚恤百姓修复水利田亩营制长句自动换行国朝纪事";
    const std::string latin = "LongUnbreakableLatinPopulation123456789012345678901234567890";
    std::uint32_t random = seed;
    auto next = [&] {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        return random;
    };
    std::vector<std::string> ids;
    std::printf("UI_STRESS seed=%u frames=%d\n", seed, frames);
    for (int frame = 0; frame < frames; ++frame) {
        const auto previous_anchor = ui.VariableListAnchor("stress-list");
        const bool program_scroll = frame % 9 == 0;
        const int stage = frame % 3;
        const auto bits = next();
        float width = 130 + static_cast<float>(next() % 171);
        const float font = .4f + static_cast<float>(next() % 5) * .1f;
        const float viewport = stage == 1 ? 240.0f : 320.0f;
        camera.SetViewport(viewport, 240);
        ui.SetScale(320, 240,
                    frame % 4 == 0   ? "width"
                    : frame % 4 == 1 ? "height"
                    : frame % 4 == 2 ? "contain"
                                     : "expand");
        const float visible_width = viewport / ui.Scale();
        width = std::min(width, visible_width - 20);
        const float root_x = std::max(10.0f, (320 - visible_width) * .5f + 10);
        ui.SetThemeMetric("font_scale", font);
        std::string text =
            stage == 1 ? ((bits & 1) ? chinese + chinese + chinese : latin + latin) : "田 short";
        if (frame % 7 == 0 || ids.empty()) {
            ids.clear();
            const int count = frame % 14 == 0 ? 3 : 1200;
            for (int i = 0; i < count; ++i)
                ids.push_back("stress/" + std::to_string(i));
            ui.ConfigureVariableList("stress-list", ids, 24, std::to_string(frame / 21));
        }
        input.InjectFrame({310, 235},
                          frame % 5 == 0   ? std::vector<std::string_view>{"tab"}
                          : frame % 5 == 2 ? std::vector<std::string_view>{"down"}
                                           : std::vector<std::string_view>{},
                          {});
        ui.NewFrame();
        ui.CloseModal();
        if (frame % 11 == 0)
            ui.OpenModal("stress-modal");
        renderer.BeginFrame(0, 0, 0, 1);
        renderer.BeginRenderTarget(target);
        renderer.ClearActiveTarget({0, 0, 0, 1});
        const bool started = ui.BeginFrameUI();
        ui.SetScrollOffset("outer-stress", stage == 1 ? static_cast<float>(next() % 2000) : 0);
        ui.SetScrollOffset("inner-stress", static_cast<float>(next() % 1000));
        ui.Push(root_x, 10, width, 220);
        ui.ScrollBegin("outer-stress", 180);
        ui.Push(0, 0, width - 12, 0, 3);
        ui.ThemedPanel();
        const float measured = ui.Text(text);
        ui.ScrollBegin("inner-stress", 36);
        ui.Text(text + text);
        const float inner = ui.ScrollEnd();
        ui.Pop();
        const float sibling_y = ui.LastRect().y + ui.LastRect().height;
        ui.DisabledBegin((bits & 4) != 0);
        ui.Button("stress-follow", 20, "following sibling");
        ui.DisabledEnd();
        const auto sibling = ui.LastRect();
        if (program_scroll)
            ui.SetScrollOffset("stress-list",
                               frame % 18 == 0 ? 100000.0f : static_cast<float>(next() % 500));
        ui.ListView("stress-list", 65);
        const float outer = ui.ScrollEnd();
        ui.Pop();
        if (ui.ModalBegin("stress-modal", 150, 80)) {
            ui.Button("stress-dialog", 24, "dialog");
            ui.ModalEnd();
        }
        const bool laid_out = ui.EndLayout();
        int submitted = 0;
        if (ui.ListViewBegin("stress-list")) {
            ui.VariableListBegin("stress-list", 65);
            for (int index = ui.VariableListNext(); index >= 0; index = ui.VariableListNext()) {
                ui.Text(index % 3 == 0   ? chinese + chinese
                        : index % 3 == 1 ? latin
                                         : "short",
                        font);
                ui.Button(ids[index], 18, "row");
                ui.VariableListRowEnd();
                ++submitted;
                if (submitted > 100)
                    break;
            }
            ui.VariableListEnd();
            ui.ListViewEnd();
        }
        const bool resolved = ui.EndFrameUI();
        renderer.EndRenderTarget();
        renderer.EndFrame();
        auto verify = [&](bool condition, const char *message) {
            if (!condition)
                std::printf("UI_STRESS_FAILURE seed=%u frame=%d state=%u width=%.0f font=%.2f\n",
                            seed, frame, bits, width, font);
            check(condition, message);
        };
        verify(started && laid_out && resolved, "stress: one balanced non-replayed declaration");
        verify(std::abs(sibling.y - sibling_y) < .02f && measured > 0 && inner > 0 &&
                   outer >= sibling.height,
               "stress: seeded grow-shrink sibling reserve is same-frame");
        verify(ui.OverflowCount() == 0,
               "stress: randomized wrapping/scales/nested clips introduce no unintended overflow");
        verify(submitted > 0 && submitted < 15,
               "stress: randomized list count/width still submits only a window");
        verify(ui.ScrollOffset("stress-list") >= 0 &&
                   ui.ScrollOffset("stress-list") <=
                       std::max(0.0f, ui.VariableListTotal("stress-list") - 65) + .02f,
               "stress: variable scroll offset is always legal after shrink");
        const float list_limit = std::max(0.0f, ui.VariableListTotal("stress-list") - 65);
        const bool surviving = std::find(ids.begin(), ids.end(), previous_anchor) != ids.end();
        verify(program_scroll || previous_anchor.empty() || !surviving ||
                   ui.ScrollOffset("stress-list") <= .02f ||
                   ui.ScrollOffset("stress-list") >= list_limit - .02f ||
                   ui.VariableListAnchor("stress-list") == previous_anchor,
               "stress: surviving interior anchor has no discontinuity through randomized "
               "remeasurement");
        const auto focused = ui.FocusedWidget();
        const auto focus_rect = ui.WidgetRect(focused);
        verify(focused.empty() ||
                   (focus_rect.width > 0 && (frame % 11 != 0 || focused == "stress-dialog")),
               "stress: no removed/disabled/under-modal stale focus");
        // Sparse border probes are practical GPU readback containment checks, not screenshots.
        if (frame % 30 == 0) {
            bool contained = true;
            auto *rt = renderer.GetRenderTargets().Get(target);
            fake2d::Color pixel;
            for (int x = 0; x < 320; x += 9)
                if (rt && rt->ReadPixel(x, 0, pixel))
                    contained = contained && pixel.r < .01f && pixel.g < .01f;
            verify(contained, "stress: all deferred draws remain inside the top clip boundary");
        }
    }
    ui.CloseModal();
    renderer.GetRenderTargets().Destroy(target);
}

int RunUIStress(fake2d::Engine &engine, std::uint32_t seed, int frames) {
    int checks = 0, failures = 0;
    engine.GetRenderer().AddFallbackFont("assets/fonts/ui_layout_test.otf", 28);
    RunUIStressChecks(engine, seed, frames, [&](bool ok, const char *what) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[ui-stress] FAIL %s\n", what);
        }
    });
    std::printf("[ui-stress] %s: %d checks, %d failures seed=%u frames=%d\n",
                failures ? "FAIL" : "PASS", checks, failures, seed, frames);
    return failures ? 1 : 0;
}

int RunUIBenchmark(fake2d::Engine &engine) {
    auto &renderer = engine.GetRenderer();
    renderer.AddFallbackFont("assets/fonts/ui_layout_test.otf", 28);
    if (!renderer.IsReady())
        return 1;
    const auto gl_string = [](unsigned int name) {
        const auto *value = glGetString(name);
        return value ? reinterpret_cast<const char *>(value) : "unavailable";
    };
#ifdef NDEBUG
    const char *build_mode = "NDEBUG";
#else
    const char *build_mode = "Debug/assertions";
#endif
#if defined(__VERSION__)
    const char *compiler = __VERSION__;
#elif defined(_MSC_VER)
    const char *compiler = "MSVC";
#else
    const char *compiler = "unavailable";
#endif
    std::printf("UI_BENCH_CONTEXT renderer=\"%s\" vendor=\"%s\" gl_version=\"%s\" compiler=\"%s\" "
                "build=\"%s\"\n",
                gl_string(GL_RENDERER), gl_string(GL_VENDOR), gl_string(GL_VERSION), compiler,
                build_mode);
    constexpr int warmup = 8, samples = 30;
    std::printf("UI_BENCH_CONFIG version=%s target=640x480 warmup=%d samples=%d "
                "timer=steady_clock_cpu gpu_time=not_measured allocations=not_instrumented\n",
                FAKE2D_VERSION_STRING, warmup, samples);
    int failures = 0;
    for (const std::string scenario :
         {"fixed_1000", "variable_1200", "rich_64_links", "nested_scroll_64"}) {
        fake2d::Input input;
        fake2d::Camera2D camera;
        camera.SetViewport(640, 480);
        fake2d::UISystem ui;
        ui.Bind(&input, &camera, &renderer);
        ui.SetAudit(true);
        const int target = renderer.GetRenderTargets().Create(640, 480);
        std::vector<std::string> ids;
        for (int i = 0; i < 1200; ++i)
            ids.push_back("bench/" + std::to_string(i));
        ui.ConfigureVariableList("rows", ids, 32, "bench");
        std::vector<double> layout, interaction, total, draw_calls, quads;
        std::size_t max_nodes = 0, max_visible = 0, max_commands = 0, max_steps = 0;
        std::size_t max_rich_batches = 0, max_rich_glyphs = 0;
        int min_rows = 100000, max_rows = 0;
        for (int frame = -warmup; frame < samples; ++frame) {
            const auto start = std::chrono::steady_clock::now();
            input.InjectFrame({630, 470}, {}, {});
            ui.NewFrame();
            renderer.BeginFrame(0, 0, 0, 1);
            renderer.BeginRenderTarget(target);
            ui.BeginFrameUI();
            ui.Push(10, 10, 620, 460);
            int rows = 0;
            if (scenario == "fixed_1000" || scenario == "variable_1200") {
                ui.ListView("rows", 360);
            } else if (scenario == "rich_64_links") {
                ui.ScrollBegin("rich", 360);
                for (int i = 0; i < 64; ++i) {
                    ui.RichText("[color=#ffbb55][b]Rich 田亩[/b][/color] [link=link" +
                                    std::to_string(i) + "]Read[/link] price " +
                                    std::to_string(i * 12345),
                                .6f);
                    ++rows;
                }
                ui.ScrollEnd();
            } else {
                ui.ScrollBegin("outer", 360);
                ui.SetScrollOffset("inner", static_cast<float>((frame + warmup) * 7));
                ui.Push(0, 0, 596, 0, 4);
                ui.ThemedPanel();
                ui.Text("Measured nested auto panel", .6f);
                ui.ScrollBegin("inner", 200);
                for (int i = 0; i < 64; ++i) {
                    ui.Text("田亩 English population " + std::to_string(i) +
                                " LongUnbreakableLatinPopulation12345678901234567890",
                            .6f);
                    ++rows;
                }
                ui.ScrollEnd();
                ui.Pop();
                ui.ScrollEnd();
            }
            ui.Pop();
            ui.EndLayout();
            if (ui.ListViewBegin("rows")) {
                if (scenario == "fixed_1000") {
                    ui.SetScrollOffset("rows", static_cast<float>((frame + warmup) * 17));
                    auto range = ui.ListBegin("rows", 360, 1000, 24);
                    for (int i = 0; i < range.count; ++i) {
                        const int row = range.first + i;
                        ui.Button(ids[row], 24, "田亩 population " + std::to_string(row));
                        ++rows;
                    }
                    ui.ListEnd();
                } else {
                    ui.SetScrollOffset("rows", static_cast<float>((frame + warmup) * 31));
                    ui.VariableListBegin("rows", 360);
                    for (int row = ui.VariableListNext(); row >= 0; row = ui.VariableListNext()) {
                        ui.Text(
                            row % 3 == 0
                                ? "能吏抚恤百姓修复水利田亩营制长句自动换行国朝纪事能吏抚恤百姓修复"
                                  "水利田亩营制长句自动换行"
                                : "LongUnbreakableLatinPopulation123456789012345678901234567890",
                            .55f);
                        ui.Button(ids[row], 20, "row");
                        ui.VariableListRowEnd();
                        ++rows;
                    }
                    ui.VariableListEnd();
                }
                ui.ListViewEnd();
            }
            const bool balanced = ui.EndFrameUI();
            renderer.EndRenderTarget();
            renderer.EndFrame();
            const auto &stats = ui.Stats();
            const bool workload =
                balanced && ui.OverflowCount() == 0 &&
                (scenario == "fixed_1000" ? rows >= 15 && rows <= 16
                 : scenario == "variable_1200"
                     ? rows > 0 && rows < 20 &&
                           stats.measured_rows == static_cast<std::size_t>(rows) &&
                           stats.tree_steps < 2000
                     : scenario == "rich_64_links"
                         ? rows == 64 && stats.rich_batches <= 320 &&
                               renderer.DrawCallCount() <= 400 && stats.nodes == 64
                         : rows == 64);
            if (!workload) {
                ++failures;
                std::printf(
                    "UI_BENCH_FAILURE scenario=%s frame=%d rows=%d nodes=%zu steps=%zu audit=%d\n",
                    scenario.c_str(), frame, rows, stats.nodes, stats.tree_steps,
                    ui.OverflowCount());
            }
            if (frame < 0)
                continue;
            layout.push_back(stats.layout_ms);
            interaction.push_back(stats.interaction_ms);
            total.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count());
            draw_calls.push_back(static_cast<double>(renderer.DrawCallCount()));
            quads.push_back(static_cast<double>(renderer.QuadCount()));
            min_rows = std::min(min_rows, rows);
            max_rows = std::max(max_rows, rows);
            max_nodes = std::max(max_nodes, stats.nodes);
            max_visible = std::max(max_visible, stats.visible);
            max_commands = std::max(max_commands, stats.draw_commands);
            max_steps = std::max(max_steps, stats.tree_steps);
            max_rich_batches = std::max(max_rich_batches, stats.rich_batches);
            max_rich_glyphs = std::max(max_rich_glyphs, stats.rich_glyphs);
        }
        auto mean = [](const std::vector<double> &values) {
            double sum = 0;
            for (double value : values)
                sum += value;
            return sum / values.size();
        };
        auto percentile = [](std::vector<double> values, double q) {
            std::sort(values.begin(), values.end());
            return values[static_cast<std::size_t>(q * (values.size() - 1))];
        };
        std::printf(
            "UI_BENCH scenario=%s layout_ms_mean=%.6f layout_ms_p50=%.6f layout_ms_p95=%.6f "
            "interaction_ms_mean=%.6f total_cpu_ms_mean=%.6f submitted_rows_min=%d "
            "submitted_rows_max=%d nodes_max=%zu visible_max=%zu draw_commands_max=%zu "
            "draw_calls_mean=%.1f quads_mean=%.1f tree_steps_max=%zu rich_batches_max=%zu rich_glyphs_max=%zu\n",
            scenario.c_str(), mean(layout), percentile(layout, .5), percentile(layout, .95),
            mean(interaction), mean(total), min_rows, max_rows, max_nodes, max_visible,
            max_commands, mean(draw_calls), mean(quads), max_steps, max_rich_batches, max_rich_glyphs);
        renderer.GetRenderTargets().Destroy(target);
    }
    std::printf("[ui-bench] %s: 4 workloads, %d counter failures\n", failures ? "FAIL" : "PASS",
                failures);
    return failures ? 1 : 0;
}
