#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "fake2d/input.h"
#include "fake2d/math.h"
#include "fake2d/sprite_batch.h"
#include "fake2d/texture.h"
#include "fake2d/ui.h"

namespace fake2d {

struct ImmediateViolation {
    std::string type;
    std::string label;
    Rect elem{};
    Rect container{};
    std::string reason;
};

struct ImmediateClickable {
    int id = 0;
    std::string type;
    std::string label;
    std::string tip;
    Rect rect{};
    bool enabled = true;
    bool active = false;
};

struct ImmediateTheme {
    Color panel{0.13f, 0.10f, 0.075f, 0.94f};
    Color panel2{0.17f, 0.135f, 0.095f, 0.96f};
    Color header{0.16f, 0.13f, 0.09f, 1.0f};
    Color accent{0.80f, 0.66f, 0.38f, 1.0f};
    Color accent_dim{0.52f, 0.43f, 0.26f, 1.0f};
    Color text{0.92f, 0.88f, 0.78f, 1.0f};
    Color text_dim{0.74f, 0.70f, 0.62f, 1.0f};
    Color paper_tint{0.17f, 0.135f, 0.095f, 0.96f};
    Color title_grad_top{0.42f, 0.13f, 0.09f, 1.0f};
    Color title_grad_bot{0.24f, 0.07f, 0.05f, 1.0f};
    Color header_paper_tint{0.55f, 0.25f, 0.2f, 0.25f};
    Color button_top{0.25f, 0.22f, 0.18f, 1.0f};
    Color button_bot{0.15f, 0.13f, 0.11f, 1.0f};
    Color button_hot_top{0.38f, 0.31f, 0.20f, 1.0f};
    Color button_hot_bot{0.24f, 0.19f, 0.12f, 1.0f};
    Color button_active_top{0.55f, 0.41f, 0.18f, 1.0f};
    Color button_active_bot{0.36f, 0.26f, 0.11f, 1.0f};
    Color button_dis_top{0.17f, 0.17f, 0.17f, 1.0f};
    Color button_dis_bot{0.12f, 0.12f, 0.12f, 1.0f};
    Color button_hot_border{0.75f, 0.62f, 0.38f, 1.0f};
    Color button_border{0.42f, 0.35f, 0.24f, 1.0f};
    Color slider_knob_top{0.85f, 0.72f, 0.45f, 1.0f};
    Color slider_knob_bot{0.55f, 0.43f, 0.24f, 1.0f};
    Color close_bg{0.25f, 0.12f, 0.10f, 1.0f};
    Color close_hot{0.55f, 0.20f, 0.15f, 1.0f};
    Color badge_dark_text{0.18f, 0.12f, 0.06f, 1.0f};
    Color badge_light_text{1.0f, 0.97f, 0.90f, 1.0f};
    int default_face = 0;
    int title_face = 2;
};

struct ImmediateTextCallbacks {
    std::function<float(SpriteBatch &, std::string_view, float x, float y, float px, const Color &, int align, int face)> draw;
    std::function<float(SpriteBatch &, std::string_view, float x, float y, float w, float px, const Color &, int face)> draw_wrapped;
    std::function<float(std::string_view, float px, int face)> measure;
    std::function<float(std::string_view, float px, float w, int face)> measure_wrapped;
    std::function<void(float render_scale)> set_render_scale;
};

/// Immediate-mode 2D GUI toolkit with deterministic layout auditing, clickable
/// widget collection, clipped nested scroll viewports, and textured/beveled chrome.
class ImmediateGui {
  public:
    int click_count = 0;
    bool audit_bounds = false;
    bool collect_clickables = false;
    std::vector<ImmediateViolation> violations;
    std::vector<ImmediateClickable> clickables;
    std::vector<ImmediateClickable> prev_clickables;
    Rect active_panel_rect{0.0f, 0.0f, 0.0f, 0.0f};
    std::string active_panel_title;

    Vec2 mouse{};
    Vec2 mouse_override{-1.0f, -1.0f};
    bool mouse_locked = false;
    bool click_override = false;
    bool down_override = false;
    bool clicked = false;
    bool down = false;
    bool rclicked = false;
    double wheel = 0.0;
    float sw = 1600.0f;
    float sh = 900.0f;
    float ui_scale = 1.0f;
    float draw_scale = 1.0f;
    float clock_s = 0.0f;
    bool suppress = false;
    std::string tooltip;
    SpriteBatch *b = nullptr;

    void SetTheme(const ImmediateTheme &theme) { theme_ = theme; }
    [[nodiscard]] const ImmediateTheme &Theme() const { return theme_; }
    void SetTextCallbacks(ImmediateTextCallbacks cbs) { text_cbs_ = std::move(cbs); }
    [[nodiscard]] const ImmediateTextCallbacks &TextCallbacks() const { return text_cbs_; }

    void CheckBounds(const Rect &r, const std::string &type, const std::string &label);
    void RecordClickable(const std::string &type, const std::string &label, const std::string &tip,
                         const Rect &r, bool enabled, bool active);

    void Begin(SpriteBatch *batch, const Input *in, float screen_w, float screen_h, float content_scale);
    void End();
    [[nodiscard]] bool MouseOverUI() const { return over_prev_; }
    [[nodiscard]] bool WantsMouse() const { return over_prev_ || modal_; }

    void Fill(const Rect &r, const Color &c);
    void Frame(const Rect &r, const Color &c, float thickness = 1.0f);
    void Branch(float x0, float y0, float cx0, float cy0, float cx1, float cy1, float x1, float y1,
                float w0, float w1, const Color &c);
    void Paper(const Rect &r, const Color &tint);
    void Badge(float x, float y, float size, const Color &c, const std::string &glyph);
    void GradientV(const Rect &r, const Color &top, const Color &bottom, int steps = 8);

    float Text(float x, float y, std::string_view s, float px, const Color &c, int align, int face);
    float Text(float x, float y, std::string_view s, float px = 14.0f);
    float Wrapped(float x, float y, float w, std::string_view s, float px, const Color &c, int face);
    float Wrapped(float x, float y, float w, std::string_view s, float px = 14.0f);
    [[nodiscard]] float Measure(std::string_view s, float px, int face) const;
    [[nodiscard]] float Measure(std::string_view s, float px = 14.0f) const;
    [[nodiscard]] float MeasureWrapped(std::string_view s, float px, float width, int face) const;
    float TextFit(float x, float y, float max_w, std::string_view s, float px, const Color &c,
                  int face, float min_px = 9.5f);

    void Panel(const Rect &r, const std::string &title = "", bool block_input = true);
    bool Button(const Rect &r, const std::string &label, bool enabled = true,
                const std::string &tip = "", bool active = false, float px = 14.0f);
    bool CloseButton(const Rect &r);
    bool Slider(const Rect &r, float &v, float lo, float hi, const std::string &tip = "");
    bool Checkbox(const Rect &r, bool &v, const std::string &label, const std::string &tip = "");
    bool InvisibleButton(const Rect &r, const std::string &tip = "");
    void Bar(const Rect &r, float frac, const Color &c, const Color &bg = {0.0f, 0.0f, 0.0f, 0.45f});
    void Tip(const Rect &r, const std::string &t);
    void Section(float x, float y, float w, const std::string &title);
    [[nodiscard]] bool Hovered(const Rect &r) const;
    void Block(const Rect &r);
    void SetModal(bool m) { modal_ = m; }

    float BeginScroll(const std::string &id, const Rect &r, float content_h);
    void EndScroll();

    void ReleaseProceduralTextures();

  private:
    void Clip(const Rect *r);
    const Texture2D &PaperTex();
    const Texture2D &DiscTex();
    [[nodiscard]] float Sx(float v) const { return v * draw_scale; }
    [[nodiscard]] Rect Sr(const Rect &r) const {
        return {r.x * draw_scale, r.y * draw_scale, r.width * draw_scale, r.height * draw_scale};
    }

    ImmediateTheme theme_;
    ImmediateTextCallbacks text_cbs_;
    Texture2D paper_tex_;
    Texture2D disc_tex_;
    std::string drag_id_;
    float cs_ = 1.0f;
    bool over_ = false;
    bool over_prev_ = false;
    bool modal_ = false;
    std::unordered_map<std::string, float> scroll_;
    std::vector<Rect> clip_stack_;
    bool wheel_used_ = false;
};

} // namespace fake2d
