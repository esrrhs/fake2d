#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fake2d/math.h"
#include "fake2d/ui.h"

namespace fake2d {

class Engine;

struct UITestTarget {
    std::string name;
    /// Called lazily before the first audit frame of this target (e.g. deferred scene boot).
    std::function<void()> prepare;
    /// Reset screen/state to the target's initial layout before each audit or simulated click.
    std::function<void()> reset_state;
    /// Execute a full frame render (batch Begin -> UISystem layout/draw -> batch End).
    std::function<void()> render_frame;
    /// Optional post-click cleanup (e.g. suppress quit / confirmation dialogs triggered by button tests).
    std::function<void()> after_click;
};

struct UITestCustomCheck {
    std::string name;
    std::function<void()> prepare;
    /// Return 0 on pass or >0 error count on fail (and log diagnostic line).
    std::function<std::size_t()> run;
};

struct UITestConfig {
    std::string filter_pattern;
    std::function<std::size_t()> get_error_count;
    /// Run `UISystem::AuditLayout()` on each target (overflow, viewport, overlap, alignment, centering).
    bool audit_layout = true;
    /// Exercise mouse press/release on every visible interactive widget.
    bool test_clicks = true;
    /// Verify enabled visible widgets are unoccluded, clipped to >1px, and register click/focus response.
    bool verify_clickable = true;
    /// Optional predicate returning true if a specific widget intentionally opts out of ClickCount verification.
    std::function<bool(std::string_view target, std::string_view widget, std::string_view kind)> allow_no_click;
};

struct UITestReport {
    std::size_t targets_tested = 0;
    std::size_t buttons_tested = 0;
    std::size_t violations = 0;
    std::size_t script_errors = 0;
    std::size_t click_failures = 0;
    std::size_t skipped_offscreen = 0;
    std::size_t disabled_tested = 0;
    double elapsed_seconds = 0.0;

    [[nodiscard]] bool Passed() const {
        return violations == 0 && script_errors == 0 && click_failures == 0;
    }
};

/// Unified automated UI layout auditor & clickable widget stresser driven directly
/// by `fake2d::UISystem` (`AuditLayout()`, `AuditRects()`, `ClickCount()`, and `LogicalToWindow()`).
UITestReport RunUITests(Engine &engine,
                        const std::vector<UITestTarget> &targets,
                        const std::vector<UITestCustomCheck> &custom_checks,
                        const UITestConfig &config);

/// High-level semantic UI test driver built on Two-Pass `AuditRects()` & `AuditLayout()`.
/// Lets functional and layout tests interact by widget key or text instead of hardcoded pixel math.
class UITestDriver {
public:
    using FrameFn = std::function<void()>;

    UITestDriver(Engine &engine, FrameFn render_frame);

    std::vector<UISystem::LayoutIssue> StepFrame(float dt = 0.0f);
    [[nodiscard]] std::vector<UISystem::LayoutIssue> Audit() const;

    [[nodiscard]] std::optional<UISystem::AuditRect> Find(std::string_view key) const;
    [[nodiscard]] std::optional<UISystem::AuditRect> FindByText(std::string_view substring,
                                                                std::string_view kind = "") const;
    [[nodiscard]] std::optional<Vec2> ProbePoint(std::string_view key) const;
    [[nodiscard]] bool IsVisible(std::string_view key) const;
    [[nodiscard]] Rect RectOf(std::string_view key) const;

    bool Hover(std::string_view key, float dt = 0.0f);
    bool HoverText(std::string_view substring, std::string_view kind = "", float dt = 0.0f);
    bool HoverAt(const Vec2 &logical_pos, float dt = 0.0f);
    bool Click(std::string_view key, float dt = 0.0f);
    bool ClickText(std::string_view substring, std::string_view kind = "", float dt = 0.0f);
    bool TypeText(std::string_view key, std::string_view utf8_text);
    void PressKey(std::string_view key_name, float dt = 0.0f);
    bool Scroll(std::string_view key, float wheel_delta);
    bool ScrollHorizontal(std::string_view key, float wheel_x);
    bool ScrollTo(std::string_view key, bool smooth = false);
    bool DragAndDrop(std::string_view src_key, std::string_view dst_key, float dt = 0.0f);
    void Pinch(float delta, float dt = 0.0f);
    void MoveMouseAway(float dt = 0.0f);

    [[nodiscard]] std::vector<UISemanticNode> SemanticNodes() const;
    [[nodiscard]] std::string SemanticJson() const;
    [[nodiscard]] std::optional<UISemanticNode> FindSemantic(std::string_view role,
                                                             std::string_view label = "") const;

private:
    Engine &engine_;
    FrameFn render_frame_;
    Vec2 mouse_logical_{-10000.0f, -10000.0f};

    bool ClickAuditIdx(std::size_t idx, float dt);
};

} // namespace fake2d
