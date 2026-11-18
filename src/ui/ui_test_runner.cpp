#include "fake2d/ui_test_runner.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "fake2d/engine.h"
#include "fake2d/input.h"
#include "fake2d/text_utils.h"
#include "fake2d/ui.h"

namespace fake2d {

namespace {

Rect IntersectRect(const Rect &a, const Rect &b) {
    const float x0 = std::max(a.x, b.x);
    const float y0 = std::max(a.y, b.y);
    const float x1 = std::min(a.x + a.width, b.x + b.width);
    const float y1 = std::min(a.y + a.height, b.y + b.height);
    return {x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
}

Rect ViewportRect(const UISystem &uisys) {
    const float vw = uisys.LogicalWidth();
    const float vh = uisys.LogicalHeight();
    return (vw > 1.0f && vh > 1.0f) ? Rect{0.0f, 0.0f, vw, vh} : Rect{-1e6f, -1e6f, 2e6f, 2e6f};
}

std::string DisplayWidgetName(const UISystem::AuditRect &a, std::size_t idx) {
    if (!a.name.empty()) {
        return a.name;
    }
    return a.kind + "#" + std::to_string(idx);
}

bool IsTextEditorKind(std::string_view kind) {
    return kind == "text_input" || kind == "text_area" || kind == "text input" ||
           kind == "text area" || kind == "password";
}

struct ProbeResolution {
    bool visible = false;
    bool found_probe = false;
    bool covered_by_modal_layer = false;
    Rect vis{};
    Vec2 probe{};
};

ProbeResolution ResolveAuditProbe(const std::vector<UISystem::AuditRect> &rects,
                                  std::size_t idx,
                                  const Rect &viewport,
                                  int max_layer) {
    ProbeResolution res;
    const auto &a = rects[idx];
    const Rect clipped = IntersectRect(a.rect, a.clip_rect);
    res.vis = IntersectRect(clipped, viewport);
    if (res.vis.width <= 1.0f || res.vis.height <= 1.0f) {
        return res;
    }
    res.visible = true;

    const float mx = std::min(2.0f, res.vis.width * 0.2f);
    const float my = std::min(2.0f, res.vis.height * 0.2f);
    const float x0 = res.vis.x + mx, x1 = res.vis.x + res.vis.width - mx;
    const float y0 = res.vis.y + my, y1 = res.vis.y + res.vis.height - my;
    std::vector<Vec2> candidates;
    if (a.kind == "checkbox" || a.kind == "radio") {
        const float lead_x = std::clamp(a.rect.x + std::min(a.rect.height, a.rect.width) * 0.45f, x0, x1);
        candidates.push_back({lead_x, (y0 + y1) * 0.5f});
    }
    candidates.push_back({(x0 + x1) * 0.5f, (y0 + y1) * 0.5f});
    candidates.push_back({x0 + (x1 - x0) * 0.25f, (y0 + y1) * 0.5f});
    candidates.push_back({x0 + (x1 - x0) * 0.75f, (y0 + y1) * 0.5f});
    candidates.push_back({(x0 + x1) * 0.5f, y0 + (y1 - y0) * 0.25f});
    candidates.push_back({(x0 + x1) * 0.5f, y0 + (y1 - y0) * 0.75f});
    for (int gy = 0; gy < 5; ++gy) {
        for (int gx = 0; gx < 5; ++gx) {
            candidates.push_back({x0 + (x1 - x0) * (gx / 4.0f), y0 + (y1 - y0) * (gy / 4.0f)});
        }
    }

    auto occluded_by_higher_layer = [&](const Vec2 &pt) -> bool {
        for (std::size_t j = 0; j < rects.size(); ++j) {
            if (j == idx) continue;
            const auto &s = rects[j];
            if (s.suppressed || s.kind == "tooltip" || s.rect.width <= 0.5f || s.rect.height <= 0.5f) {
                continue;
            }
            if (s.layer > a.layer) {
                const Rect sv = IntersectRect(s.rect, s.clip_rect);
                if (sv.Contains(pt)) return true;
            }
        }
        return false;
    };

    auto occluded_on_same_layer = [&](const Vec2 &pt) -> bool {
        for (std::size_t j = idx + 1; j < rects.size(); ++j) {
            const auto &s = rects[j];
            if (s.suppressed || s.kind == "tooltip" || s.layer != a.layer ||
                s.rect.width <= 0.5f || s.rect.height <= 0.5f) {
                continue;
            }
            if (UISystem::IsInteractiveKind(s.kind) ||
                ((s.kind == "panel" || s.kind == "window") && s.root_box_id != a.root_box_id)) {
                const Rect sv = IntersectRect(s.rect, s.clip_rect);
                if (sv.Contains(pt)) return true;
            }
        }
        return false;
    };

    res.probe = candidates.front();
    for (const Vec2 &c : candidates) {
        if (occluded_by_higher_layer(c)) {
            res.covered_by_modal_layer = true;
            continue;
        }
        if (!occluded_on_same_layer(c)) {
            res.probe = c;
            res.found_probe = true;
            return res;
        }
    }
    if (a.layer >= max_layer) {
        res.covered_by_modal_layer = false;
    }
    return res;
}

int MaxAuditLayer(const std::vector<UISystem::AuditRect> &rects) {
    int max_layer = 0;
    for (const auto &r : rects) {
        if (!r.suppressed && r.kind != "tooltip") {
            max_layer = std::max(max_layer, r.layer);
        }
    }
    return max_layer;
}

bool MatchesQuery(const UISystem::AuditRect &a,
                  std::string_view query,
                  bool by_text,
                  std::string_view kind,
                  bool require_enabled) {
    if (a.suppressed || (require_enabled && a.disabled) || (!kind.empty() && a.kind != kind)) {
        return false;
    }
    if (!by_text) {
        return a.name == query;
    }
    return (!a.text.empty() && a.text.find(query) != std::string::npos) ||
           (a.text.empty() && a.name.find(query) != std::string::npos);
}

std::optional<std::size_t> FindMatchingAuditIdx(const std::vector<UISystem::AuditRect> &rects,
                                                const Rect &vp,
                                                int max_layer,
                                                std::string_view query,
                                                bool by_text,
                                                std::string_view kind,
                                                bool require_enabled,
                                                bool allow_occluded_fallback) {
    std::optional<std::size_t> fallback;
    for (std::size_t i = 0; i < rects.size(); ++i) {
        const auto &a = rects[i];
        if (!MatchesQuery(a, query, by_text, kind, require_enabled)) {
            continue;
        }
        const auto pr = ResolveAuditProbe(rects, i, vp, max_layer);
        if (pr.visible && pr.found_probe) {
            return i;
        }
        if (allow_occluded_fallback && !fallback.has_value()) {
            fallback = i;
        }
    }
    return fallback;
}

} // namespace

UITestReport RunUITests(Engine &engine,
                        const std::vector<UITestTarget> &targets,
                        const std::vector<UITestCustomCheck> &custom_checks,
                        const UITestConfig &config) {
    UITestReport report;
    auto &in = engine.GetInput();
    auto &uisys = engine.GetUI();

    uisys.SetAudit(true);

    std::fprintf(stderr, "\n======================================================================\n");
    std::fprintf(stderr, "           STARTING COMPREHENSIVE AUTOMATED UI TEST SUITE            \n");
    std::fprintf(stderr, "======================================================================\n");

    const auto t_start = std::chrono::steady_clock::now();

    for (const auto &target : targets) {
        if (!config.filter_pattern.empty() && target.name.find(config.filter_pattern) == std::string::npos) {
            continue;
        }
        if (target.prepare) {
            target.prepare();
        }

        std::fprintf(stderr, "\n--------------------------------------------------\n");
        std::fprintf(stderr, "[UI_TEST] Testing Target: %s\n", target.name.c_str());

        // Warm up 1 frame so auto-height / cached layouts settle before auditing
        in.InjectFrame({-10000.0f, -10000.0f}, {}, {});
        uisys.NewFrame();
        if (target.reset_state) {
            target.reset_state();
        }
        if (target.render_frame) {
            target.render_frame();
        }

        in.InjectFrame({-10000.0f, -10000.0f}, {}, {});
        uisys.NewFrame();

        if (target.reset_state) {
            target.reset_state();
        }
        if (target.render_frame) {
            target.render_frame();
        }

        const auto rects = uisys.AuditRects();
        const Rect viewport = ViewportRect(uisys);
        const int max_layer = MaxAuditLayer(rects);

        std::size_t target_violations = 0;
        if (config.audit_layout) {
            const auto issues = uisys.AuditLayout();
            target_violations = issues.size();
            for (const auto &iss : issues) {
                std::fprintf(stderr, "    - [LAYOUT %s:%s] %s\n",
                             iss.category.c_str(), iss.rule.c_str(), iss.message.c_str());
            }
            report.violations += target_violations;
            if (target_violations > 0) {
                std::fprintf(stderr, "  [LAYOUT FAIL] %zu layout/alignment/overflow violations total.\n",
                             target_violations);
            } else {
                std::fprintf(stderr, "  [LAYOUT PASS] 0 layout/alignment/overflow violations.\n");
            }
        }

        struct ClickItem {
            std::size_t audit_idx;
            std::string kind;
            std::string name;
            Rect rect;
            Rect vis;
            Vec2 probe;
            bool disabled = false;
        };
        std::vector<ClickItem> buttons;
        std::size_t target_click_failures = 0;
        std::size_t target_skipped_offscreen = 0;

        for (std::size_t i = 0; i < rects.size(); ++i) {
            const auto &a = rects[i];
            if (a.suppressed || !UISystem::IsInteractiveKind(a.kind) ||
                a.rect.width <= 0.0f || a.rect.height <= 0.0f) {
                continue;
            }
            const std::string wname = DisplayWidgetName(a, i);
            const ProbeResolution pr = ResolveAuditProbe(rects, i, viewport, max_layer);
            if (!pr.visible) {
                if (a.in_scroll) {
                    ++target_skipped_offscreen;
                    ++report.skipped_offscreen;
                } else if (!a.disabled && config.test_clicks && config.verify_clickable) {
                    ++target_click_failures;
                    std::fprintf(stderr, "  [CLICK FAIL] Widget '%s' (%s) clipped to empty outside scroll!\n",
                                 wname.c_str(), a.kind.c_str());
                }
                continue;
            }
            if (!pr.found_probe) {
                if (pr.covered_by_modal_layer) {
                    // Underlying widget covered by an active modal on a higher layer.
                    continue;
                }
                if (!a.disabled && config.test_clicks && config.verify_clickable) {
                    ++target_click_failures;
                    std::fprintf(stderr,
                                 "  [CLICK FAIL] Widget '%s' (%s at %.1f,%.1f,%.1fx%.1f) is occluded and unclickable!\n",
                                 wname.c_str(), a.kind.c_str(), pr.vis.x, pr.vis.y, pr.vis.width, pr.vis.height);
                }
                continue;
            }
            buttons.push_back({i, a.kind, wname, a.rect, pr.vis, pr.probe, a.disabled});
        }

        std::fprintf(stderr, "  Registered %zu clickable widgets (%zu scrolled-off skipped).\n",
                     buttons.size(), target_skipped_offscreen);

        std::size_t target_btn_errors = 0;
        if (config.test_clicks) {
            for (std::size_t b_idx = 0; b_idx < buttons.size(); ++b_idx) {
                const auto &btn = buttons[b_idx];
                const std::size_t before_errs = config.get_error_count ? config.get_error_count() : 0;

                auto reset_and_draw = [&]() {
                    if (target.reset_state) {
                        target.reset_state();
                    }
                    uisys.NewFrame();
                    if (target.render_frame) {
                        target.render_frame();
                    }
                    if (target.after_click) {
                        target.after_click();
                    }
                };

                bool cur_disabled = btn.disabled;
                Vec2 cur_probe = btn.probe;
                bool still_present = true;

                if (b_idx > 0) {
                    // Settle cached auto-heights and transient layout state cleanly before clicking next widget.
                    in.InjectFrame({-10000.0f, -10000.0f}, {}, {});
                    reset_and_draw();
                    still_present = false;
                    const auto &fresh_rects = uisys.AuditRects();
                    for (std::size_t j = 0; j < fresh_rects.size(); ++j) {
                        const auto &fr = fresh_rects[j];
                        if (fr.suppressed || fr.kind != btn.kind ||
                            DisplayWidgetName(fr, j) != btn.name) {
                            continue;
                        }
                        const Rect fr_vis = IntersectRect(IntersectRect(fr.rect, fr.clip_rect), viewport);
                        if (fr_vis.width > 1.0f && fr_vis.height > 1.0f) {
                            still_present = true;
                            cur_disabled = fr.disabled;
                            if (!fr_vis.Contains(cur_probe)) {
                                cur_probe = {fr_vis.x + fr_vis.width * 0.5f,
                                             fr_vis.y + fr_vis.height * 0.5f};
                            }
                            break;
                        }
                    }
                }

                if (!still_present) {
                    report.buttons_tested++;
                    continue;
                }

                const int before_clicks = uisys.ClickCount();
                const Vec2 win_pt = uisys.LogicalToWindow(cur_probe);

                in.InjectFrame(win_pt, {}, {0});
                reset_and_draw();
                in.InjectFrame(win_pt, {}, {});
                reset_and_draw();
                const int after_clicks = uisys.ClickCount();
                const std::string focused_after(uisys.FocusedTextInput());
                in.InjectFrame({-10000.0f, -10000.0f}, {}, {});

                const std::size_t after_errs = config.get_error_count ? config.get_error_count() : 0;
                if (after_errs > before_errs) {
                    target_btn_errors += (after_errs - before_errs);
                    std::fprintf(stderr,
                                 "  [CLICK ERROR] Button #%zu (%s '%s' at %.1f,%.1f) triggered %zu script error(s)!\n",
                                 b_idx, btn.kind.c_str(), btn.name.c_str(), btn.rect.x, btn.rect.y,
                                 (after_errs - before_errs));
                }

                if (cur_disabled) {
                    ++report.disabled_tested;
                    if (config.verify_clickable && after_clicks != before_clicks) {
                        ++target_click_failures;
                        std::fprintf(stderr,
                                     "  [CLICK FAIL] Disabled widget #%zu (%s '%s') fired a click unexpectedly!\n",
                                     b_idx, btn.kind.c_str(), btn.name.c_str());
                    }
                } else if (config.verify_clickable &&
                           (!config.allow_no_click ||
                            !config.allow_no_click(target.name, btn.name, btn.kind))) {
                    if (IsTextEditorKind(btn.kind)) {
                        const bool name_is_synthetic = btn.name.find('#') != std::string::npos;
                        if (focused_after.empty() || (!name_is_synthetic && focused_after != btn.name)) {
                            ++target_click_failures;
                            std::fprintf(stderr,
                                         "  [CLICK FAIL] Text widget #%zu (%s '%s' at %.1f,%.1f) failed to focus (focused='%s')!\n",
                                         b_idx, btn.kind.c_str(), btn.name.c_str(), cur_probe.x,
                                         cur_probe.y, focused_after.c_str());
                        }
                    } else if (btn.kind != "splitter") {
                        if (after_clicks <= before_clicks) {
                            ++target_click_failures;
                            std::fprintf(stderr,
                                         "  [CLICK FAIL] Widget #%zu (%s '%s' at %.1f,%.1f) did not register click!\n",
                                         b_idx, btn.kind.c_str(), btn.name.c_str(), cur_probe.x,
                                         cur_probe.y);
                        }
                    }
                }
                report.buttons_tested++;
            }
        }

        report.click_failures += target_click_failures;
        report.script_errors += target_btn_errors;
        report.targets_tested++;

        if (target_violations == 0 && target_btn_errors == 0 && target_click_failures == 0) {
            std::fprintf(stderr,
                         "[UI_TEST] Target '%s': PASS (%zu buttons clicked, 0 violations, 0 click failures, 0 script errors)\n",
                         target.name.c_str(), buttons.size());
        } else {
            std::fprintf(stderr,
                         "[UI_TEST] Target '%s': FAIL (%zu buttons clicked, %zu violations, %zu click failures, %zu script errors)\n",
                         target.name.c_str(), buttons.size(), target_violations,
                         target_click_failures, target_btn_errors);
        }
    }

    for (const auto &check : custom_checks) {
        if (!config.filter_pattern.empty() && check.name.find(config.filter_pattern) == std::string::npos) {
            continue;
        }
        if (check.prepare) {
            check.prepare();
        }
        std::fprintf(stderr, "\n--------------------------------------------------\n");
        std::fprintf(stderr, "[UI_TEST] Testing Target: %s\n", check.name.c_str());
        report.targets_tested++;
        if (check.run) {
            report.script_errors += check.run();
        }
    }

    const auto t_end = std::chrono::steady_clock::now();
    report.elapsed_seconds = std::chrono::duration<double>(t_end - t_start).count();

    std::fprintf(stderr, "\n======================================================================\n");
    std::fprintf(stderr, "                   AUTOMATED UI TEST SUITE REPORT                     \n");
    std::fprintf(stderr, "======================================================================\n");
    std::fprintf(stderr, "Total Screens/Sub-tabs Tested: %zu\n", report.targets_tested);
    std::fprintf(stderr, "Total Clickable Buttons Tested: %zu\n", report.buttons_tested);
    std::fprintf(stderr, "Total Disabled Widgets Checked: %zu\n", report.disabled_tested);
    std::fprintf(stderr, "Total Scrolled-Off Skipped:    %zu\n", report.skipped_offscreen);
    std::fprintf(stderr, "Total Layout Violations:       %zu\n", report.violations);
    std::fprintf(stderr, "Total Click/Hit Failures:      %zu\n", report.click_failures);
    std::fprintf(stderr, "Total Script Errors:           %zu\n", report.script_errors);
    std::fprintf(stderr, "Execution Time:                %.2f seconds\n", report.elapsed_seconds);
    std::fprintf(stderr, "======================================================================\n");
    std::fprintf(stderr, report.Passed() ? "FINAL VERDICT: [PASS]\n" : "FINAL VERDICT: [FAIL]\n");
    std::fprintf(stderr, "======================================================================\n\n");

    return report;
}

// ---------------------------------------------------------------------------
// High-Level Semantic UITestDriver
// ---------------------------------------------------------------------------

UITestDriver::UITestDriver(Engine &engine, FrameFn render_frame)
    : engine_(engine), render_frame_(std::move(render_frame)) {
    engine_.GetUI().SetAudit(true);
}

std::vector<UISystem::LayoutIssue> UITestDriver::StepFrame(float dt) {
    auto &in = engine_.GetInput();
    auto &ui = engine_.GetUI();
    if (dt > 0.0f) {
        ui.SetClockSeconds(ui.ClockSeconds() + dt);
    }
    in.InjectFrame(ui.LogicalToWindow(mouse_logical_), {}, {});
    ui.NewFrame();
    if (render_frame_) {
        render_frame_();
    }
    return ui.AuditLayout();
}

std::vector<UISystem::LayoutIssue> UITestDriver::Audit() const {
    return engine_.GetUI().AuditLayout();
}

std::optional<UISystem::AuditRect> UITestDriver::Find(std::string_view key) const {
    const auto &rects = engine_.GetUI().AuditRects();
    const auto idx = FindMatchingAuditIdx(
        rects, ViewportRect(engine_.GetUI()), MaxAuditLayer(rects), key, false, "", false, true);
    return idx.has_value() ? std::optional<UISystem::AuditRect>(rects[*idx]) : std::nullopt;
}

std::optional<UISystem::AuditRect> UITestDriver::FindByText(std::string_view substring,
                                                            std::string_view kind) const {
    const auto &rects = engine_.GetUI().AuditRects();
    const auto idx = FindMatchingAuditIdx(
        rects, ViewportRect(engine_.GetUI()), MaxAuditLayer(rects), substring, true, kind, false, true);
    return idx.has_value() ? std::optional<UISystem::AuditRect>(rects[*idx]) : std::nullopt;
}

std::optional<Vec2> UITestDriver::ProbePoint(std::string_view key) const {
    const auto &rects = engine_.GetUI().AuditRects();
    const Rect vp = ViewportRect(engine_.GetUI());
    const int max_layer = MaxAuditLayer(rects);
    const auto idx = FindMatchingAuditIdx(rects, vp, max_layer, key, false, "", false, false);
    if (!idx.has_value()) {
        return std::nullopt;
    }
    return ResolveAuditProbe(rects, *idx, vp, max_layer).probe;
}

bool UITestDriver::IsVisible(std::string_view key) const {
    const auto found = Find(key);
    if (found.has_value()) {
        const Rect clipped = IntersectRect(found->rect, found->clip_rect);
        const Rect vis = IntersectRect(clipped, ViewportRect(engine_.GetUI()));
        return vis.width > 1.0f && vis.height > 1.0f;
    }
    const Rect nr = engine_.GetUI().NamedRect(key);
    return nr.width > 1.0f && nr.height > 1.0f;
}

Rect UITestDriver::RectOf(std::string_view key) const {
    if (const auto found = Find(key); found.has_value()) {
        return found->rect;
    }
    const Rect wr = engine_.GetUI().WidgetRect(key);
    if (wr.width > 0.0f && wr.height > 0.0f) {
        return wr;
    }
    return engine_.GetUI().NamedRect(key);
}

bool UITestDriver::Hover(std::string_view key, float dt) {
    const auto pt = ProbePoint(key);
    return pt.has_value() && HoverAt(*pt, dt);
}

bool UITestDriver::HoverText(std::string_view substring, std::string_view kind, float dt) {
    const auto &rects = engine_.GetUI().AuditRects();
    const Rect vp = ViewportRect(engine_.GetUI());
    const int max_layer = MaxAuditLayer(rects);
    const auto idx = FindMatchingAuditIdx(rects, vp, max_layer, substring, true, kind, false, false);
    return idx.has_value() && HoverAt(ResolveAuditProbe(rects, *idx, vp, max_layer).probe, dt);
}

bool UITestDriver::HoverAt(const Vec2 &logical_pos, float dt) {
    mouse_logical_ = logical_pos;
    StepFrame(dt);
    // Pump 1 settle frame so hover-opened submenus/popups render into AuditRects().
    StepFrame(0.0f);
    return true;
}

bool UITestDriver::ClickAuditIdx(std::size_t idx, float dt) {
    const auto &rects = engine_.GetUI().AuditRects();
    if (idx >= rects.size() || rects[idx].disabled) {
        return false;
    }
    const Rect vp = ViewportRect(engine_.GetUI());
    const int max_layer = MaxAuditLayer(rects);
    const auto pr = ResolveAuditProbe(rects, idx, vp, max_layer);
    if (!pr.visible || !pr.found_probe) {
        return false;
    }
    auto &in = engine_.GetInput();
    auto &ui = engine_.GetUI();
    mouse_logical_ = pr.probe;
    const Vec2 win_pt = ui.LogicalToWindow(mouse_logical_);
    const float half_dt = dt * 0.5f;
    if (half_dt > 0.0f) ui.SetClockSeconds(ui.ClockSeconds() + half_dt);
    in.InjectFrame(win_pt, {}, {0});
    ui.NewFrame();
    if (render_frame_) render_frame_();
    if (half_dt > 0.0f) ui.SetClockSeconds(ui.ClockSeconds() + half_dt);
    in.InjectFrame(win_pt, {}, {});
    ui.NewFrame();
    if (render_frame_) render_frame_();
    // Pump 1 settle frame so state transitions triggered after EndFrameUI() render and update AuditRects().
    in.InjectFrame(win_pt, {}, {});
    ui.NewFrame();
    if (render_frame_) render_frame_();
    return true;
}

bool UITestDriver::Click(std::string_view key, float dt) {
    const auto &rects = engine_.GetUI().AuditRects();
    const auto idx = FindMatchingAuditIdx(
        rects, ViewportRect(engine_.GetUI()), MaxAuditLayer(rects), key, false, "", true, false);
    return idx.has_value() && ClickAuditIdx(*idx, dt);
}

bool UITestDriver::ClickText(std::string_view substring, std::string_view kind, float dt) {
    const auto &rects = engine_.GetUI().AuditRects();
    const auto idx = FindMatchingAuditIdx(
        rects, ViewportRect(engine_.GetUI()), MaxAuditLayer(rects), substring, true, kind, true, false);
    return idx.has_value() && ClickAuditIdx(*idx, dt);
}

bool UITestDriver::TypeText(std::string_view key, std::string_view utf8_text) {
    if (engine_.GetUI().FocusedTextInput() != key) {
        if (!Click(key)) {
            return false;
        }
    }
    auto &in = engine_.GetInput();
    auto &ui = engine_.GetUI();
    const std::vector<unsigned> codepoints = utf8::Decode(utf8_text);
    in.InjectFrame(ui.LogicalToWindow(mouse_logical_), {}, {}, 0.0, codepoints);
    ui.NewFrame();
    if (render_frame_) {
        render_frame_();
    }
    StepFrame();
    return true;
}

void UITestDriver::PressKey(std::string_view key_name, float dt) {
    auto &in = engine_.GetInput();
    auto &ui = engine_.GetUI();
    const Vec2 win_pt = ui.LogicalToWindow(mouse_logical_);
    const float half_dt = dt * 0.5f;
    if (half_dt > 0.0f) {
        ui.SetClockSeconds(ui.ClockSeconds() + half_dt);
    }
    in.InjectFrame(win_pt, {key_name}, {});
    ui.NewFrame();
    if (render_frame_) {
        render_frame_();
    }
    if (half_dt > 0.0f) {
        ui.SetClockSeconds(ui.ClockSeconds() + half_dt);
    }
    in.InjectFrame(win_pt, {}, {});
    ui.NewFrame();
    if (render_frame_) {
        render_frame_();
    }
}

bool UITestDriver::Scroll(std::string_view key, float wheel_delta) {
    const Rect r = RectOf(key);
    if (r.width <= 0.0f || r.height <= 0.0f) {
        return false;
    }
    auto &in = engine_.GetInput();
    auto &ui = engine_.GetUI();
    mouse_logical_ = {r.x + r.width * 0.5f, r.y + r.height * 0.5f};
    in.InjectFrame(ui.LogicalToWindow(mouse_logical_), {}, {}, static_cast<double>(wheel_delta));
    ui.NewFrame();
    if (render_frame_) {
        render_frame_();
    }
    StepFrame();
    return true;
}

void UITestDriver::MoveMouseAway(float dt) {
    mouse_logical_ = {-10000.0f, -10000.0f};
    StepFrame(dt);
}

bool UITestDriver::ScrollHorizontal(std::string_view key, float wheel_x) {
    const Rect r = RectOf(key);
    if (r.width <= 0.0f || r.height <= 0.0f) {
        return false;
    }
    auto &in = engine_.GetInput();
    auto &ui = engine_.GetUI();
    mouse_logical_ = {r.x + r.width * 0.5f, r.y + r.height * 0.5f};
    in.InjectFrame(ui.LogicalToWindow(mouse_logical_), {}, {}, 0.0);
    in.InjectWheelX(static_cast<double>(wheel_x));
    ui.NewFrame();
    if (render_frame_) {
        render_frame_();
    }
    StepFrame();
    return true;
}

bool UITestDriver::ScrollTo(std::string_view key, bool smooth) {
    engine_.GetUI().ScrollToWidget(key, smooth);
    StepFrame(smooth ? 0.25f : 0.0f);
    return IsVisible(key);
}

bool UITestDriver::DragAndDrop(std::string_view src_key, std::string_view dst_key, float dt) {
    const Rect sr = RectOf(src_key);
    const Rect dr = RectOf(dst_key);
    if (sr.width <= 0.0f || sr.height <= 0.0f || dr.width <= 0.0f || dr.height <= 0.0f) {
        return false;
    }
    auto &in = engine_.GetInput();
    auto &ui = engine_.GetUI();
    const Vec2 start{sr.x + sr.width * 0.5f, sr.y + sr.height * 0.5f};
    const Vec2 end{dr.x + dr.width * 0.5f, dr.y + dr.height * 0.5f};
    const Vec2 mid{(start.x + end.x) * 0.5f, (start.y + end.y) * 0.5f};

    in.InjectFrame(ui.LogicalToWindow(start), {}, {0});
    ui.NewFrame();
    if (render_frame_) render_frame_();

    // Move beyond drag threshold
    in.InjectFrame(ui.LogicalToWindow({start.x + 12.0f, start.y + 12.0f}), {}, {0});
    ui.NewFrame();
    if (render_frame_) render_frame_();

    in.InjectFrame(ui.LogicalToWindow(mid), {}, {0});
    ui.NewFrame();
    if (render_frame_) render_frame_();

    in.InjectFrame(ui.LogicalToWindow(end), {}, {0});
    ui.NewFrame();
    if (render_frame_) render_frame_();

    mouse_logical_ = end;
    in.InjectFrame(ui.LogicalToWindow(end), {}, {});
    ui.NewFrame();
    if (render_frame_) render_frame_();
    StepFrame(dt);
    return true;
}

void UITestDriver::Pinch(float delta, float dt) {
    auto &in = engine_.GetInput();
    auto &ui = engine_.GetUI();
    in.InjectFrame(ui.LogicalToWindow(mouse_logical_), {}, {}, 0.0);
    in.InjectPinch(static_cast<double>(delta));
    ui.NewFrame();
    if (render_frame_) render_frame_();
    StepFrame(dt);
}

std::vector<UISemanticNode> UITestDriver::SemanticNodes() const {
    return engine_.GetUI().SemanticNodes();
}

std::string UITestDriver::SemanticJson() const {
    return engine_.GetUI().SemanticJson();
}

namespace {
std::optional<UISemanticNode> FindSemRec(const std::vector<UISemanticNode> &nodes,
                                         std::string_view role, std::string_view label) {
    for (const auto &n : nodes) {
        const bool role_ok = role.empty() || n.role == role;
        const bool lbl_ok = label.empty() || n.label.find(label) != std::string::npos || n.key == label;
        if (role_ok && lbl_ok) {
            return n;
        }
        if (auto child = FindSemRec(n.children, role, label)) {
            return child;
        }
    }
    return std::nullopt;
}
} // namespace

std::optional<UISemanticNode> UITestDriver::FindSemantic(std::string_view role,
                                                         std::string_view label) const {
    return FindSemRec(SemanticNodes(), role, label);
}

} // namespace fake2d
