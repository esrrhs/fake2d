#include "fake2d/ui_immediate.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

#include "fake2d/procedural_tex.h"
#include "fake2d/renderer.h"

namespace fake2d {

namespace {

Rect RectIntersect(const Rect &a, const Rect &b) {
    const float x0 = std::max(a.x, b.x);
    const float y0 = std::max(a.y, b.y);
    const float x1 = std::min(a.x + a.width, b.x + b.width);
    const float y1 = std::min(a.y + a.height, b.y + b.height);
    return Rect{x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
}

bool RectContains(const Rect &r, float x, float y) {
    return r.width > 0.0f && r.height > 0.0f &&
           x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height;
}

bool TryLoadFontPath(Renderer &r, const std::string &path, float px) {
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec)) {
        return false;
    }
    return r.LoadFont(path, px);
}

} // namespace

// ---------------------------------------------------------------------------
// UITextureCache
// ---------------------------------------------------------------------------

const Texture2D *UITextureCache::GetOrLoad(const std::string &name,
                                           const std::string &root_dir,
                                           const std::string &default_subdir) {
    if (name.empty()) {
        return nullptr;
    }
    if (const auto it = cache_.find(name); it != cache_.end()) {
        return it->second.get();
    }
    if (failed_.count(name)) {
        return nullptr;
    }
    std::string rel = (name.find('/') != std::string::npos) ? name : (default_subdir + name);
    std::string path = root_dir.empty() ? rel : (root_dir + "/" + rel);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        failed_[name] = true;
        return nullptr;
    }
    auto tex = std::make_unique<Texture2D>();
    if (!tex->LoadFromFile(path, TextureFilter::Linear, TextureWrap::Clamp)) {
        failed_[name] = true;
        return nullptr;
    }
    const Texture2D *ptr = tex.get();
    cache_.emplace(name, std::move(tex));
    return ptr;
}

void UITextureCache::Clear() {
    cache_.clear();
    failed_.clear();
}

// ---------------------------------------------------------------------------
// UITipTracker
// ---------------------------------------------------------------------------

void UITipTracker::BeginFrame() {
    scroll_stack_.clear();
    entries_.clear();
    polled_rect_ = {};
}

void UITipTracker::PushScroll(std::string key, const Rect &viewport, float offset) {
    Rect vp = viewport;
    for (const auto &sc : scroll_stack_) {
        vp = RectIntersect(vp, sc.viewport);
    }
    scroll_stack_.push_back({std::move(key), vp, offset});
}

void UITipTracker::PopScroll() {
    if (!scroll_stack_.empty()) {
        scroll_stack_.pop_back();
    }
}

float UITipTracker::CurrentScrollOffset() const {
    float off = 0.0f;
    for (const auto &s : scroll_stack_) {
        off += s.offset;
    }
    return off;
}

Rect UITipTracker::ToScreenRect(Rect r) const {
    r.y -= CurrentScrollOffset();
    for (const auto &s : scroll_stack_) {
        r = RectIntersect(r, s.viewport);
    }
    return r;
}

void UITipTracker::RecordEntry(const Rect &r, std::string text) {
    if (text.empty() || r.width <= 0.0f || r.height <= 0.0f) {
        return;
    }
    entries_.push_back({r, std::move(text)});
}

void UITipTracker::RecordClippedEntry(const Rect &r, std::string text) {
    if (text.empty()) {
        return;
    }
    RecordEntry(ToScreenRect(r), std::move(text));
}

void UITipTracker::RecordOccluder(const Rect &r) {
    if (r.width <= 0.0f || r.height <= 0.0f) {
        return;
    }
    entries_.push_back({r, std::string{}});
}

std::string UITipTracker::Poll(Vec2 mouse, std::string_view fallback_tip) {
    if (mouse.x < -1000.0f || mouse.y < -1000.0f) {
        polled_rect_ = {};
        return {};
    }
    for (int i = static_cast<int>(entries_.size()) - 1; i >= 0; --i) {
        const auto &e = entries_[static_cast<std::size_t>(i)];
        if (RectContains(e.rect, mouse.x, mouse.y)) {
            if (e.text.empty()) {
                polled_rect_ = {};
                return {};
            }
            polled_rect_ = e.rect;
            return e.text;
        }
    }
    if (!fallback_tip.empty()) {
        polled_rect_ = {mouse.x - 10.0f, mouse.y - 10.0f, 20.0f, 20.0f};
        return std::string(fallback_tip);
    }
    polled_rect_ = {};
    return {};
}

// ---------------------------------------------------------------------------
// EnsureSystemCjkFonts
// ---------------------------------------------------------------------------

bool EnsureSystemCjkFonts(Renderer &renderer, const std::string &primary_font_path, float pixel_height) {
    static const char *const kSysFonts[] = {
        "/System/Library/Fonts/Hiragino Sans GB.ttc",
        "/System/Library/Fonts/STHeiti Medium.ttc",
        "/System/Library/Fonts/Supplemental/Songti.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "C:/Windows/Fonts/msyh.ttc",
    };
    bool ok = TryLoadFontPath(renderer, primary_font_path, pixel_height);
    if (!ok) {
        for (const char *p : kSysFonts) {
            if (TryLoadFontPath(renderer, p, pixel_height)) {
                ok = true;
                break;
            }
        }
    }
    if (!ok) {
        return false;
    }
    for (const char *p : kSysFonts) {
        renderer.AddFallbackFont(p, pixel_height);
    }
    return true;
}

// ---------------------------------------------------------------------------
// ImmediateGui
// ---------------------------------------------------------------------------

const Texture2D &ImmediateGui::PaperTex() {
    if (!paper_tex_.IsValid()) {
        paper_tex_ = procedural_tex::CreatePaperGrain();
    }
    return paper_tex_;
}

const Texture2D &ImmediateGui::DiscTex() {
    if (!disc_tex_.IsValid()) {
        disc_tex_ = procedural_tex::CreateBeveledDisc();
    }
    return disc_tex_;
}

void ImmediateGui::ReleaseProceduralTextures() {
    paper_tex_.Destroy();
    disc_tex_.Destroy();
}

void ImmediateGui::RecordClickable(const std::string &type, const std::string &label,
                                     const std::string &tip, const Rect &r, bool enabled, bool active) {
    if (!collect_clickables) {
        return;
    }
    clickables.push_back({static_cast<int>(clickables.size()), type, label, tip, r, enabled, active});
}

void ImmediateGui::CheckBounds(const Rect &r, const std::string &type, const std::string &label) {
    if (!audit_bounds || r.width <= 0.0f || r.height <= 0.0f) {
        return;
    }
    const float tol = 3.0f;
    if (!clip_stack_.empty()) {
        const Rect &c = clip_stack_.back();
        if (r.x < c.x - tol || (r.x + r.width) > c.x + c.width + tol) {
            std::string reason = "Element horizontally exceeds scroll container: elem_x=" +
                                 std::to_string(static_cast<int>(r.x)) +
                                 " elem_w=" + std::to_string(static_cast<int>(r.width)) +
                                 " vs scroll_x=" + std::to_string(static_cast<int>(c.x)) +
                                 " scroll_w=" + std::to_string(static_cast<int>(c.width));
            violations.push_back({type, label, r, c, reason});
            std::fprintf(stderr, "[UI_OVERFLOW] %s: %s (label='%s')\n",
                         type.c_str(), reason.c_str(), label.c_str());
        }
        return;
    }

    if (r.x < -tol || r.y < -tol || (r.x + r.width) > sw + tol || (r.y + r.height) > sh + tol) {
        std::string reason = "Element exceeds screen bounds: elem=(" +
                             std::to_string(static_cast<int>(r.x)) + "," +
                             std::to_string(static_cast<int>(r.y)) + "," +
                             std::to_string(static_cast<int>(r.width)) + "x" +
                             std::to_string(static_cast<int>(r.height)) + ") screen=" +
                             std::to_string(static_cast<int>(sw)) + "x" +
                             std::to_string(static_cast<int>(sh));
        violations.push_back({type, label, r, Rect{0.0f, 0.0f, sw, sh}, reason});
        std::fprintf(stderr, "[UI_OVERFLOW] %s: %s (label='%s')\n",
                     type.c_str(), reason.c_str(), label.c_str());
        return;
    }

    if (active_panel_rect.width > 0.0f) {
        if (r.x >= active_panel_rect.x - tol && r.x < active_panel_rect.x + active_panel_rect.width) {
            if ((r.x + r.width) > active_panel_rect.x + active_panel_rect.width + tol) {
                std::string reason = "Element right edge exceeds panel width: elem_x=" +
                                     std::to_string(static_cast<int>(r.x)) +
                                     " elem_w=" + std::to_string(static_cast<int>(r.width)) +
                                     " vs panel_x=" + std::to_string(static_cast<int>(active_panel_rect.x)) +
                                     " panel_w=" + std::to_string(static_cast<int>(active_panel_rect.width));
                violations.push_back({type, label, r, active_panel_rect, reason});
                std::fprintf(stderr, "[UI_OVERFLOW] %s: %s (label='%s')\n",
                             type.c_str(), reason.c_str(), label.c_str());
            }
            if (r.y >= active_panel_rect.y - tol &&
                (r.y + r.height) > active_panel_rect.y + active_panel_rect.height + tol) {
                std::string reason = "Element bottom exceeds panel height: elem_y=" +
                                     std::to_string(static_cast<int>(r.y)) +
                                     " elem_h=" + std::to_string(static_cast<int>(r.height)) +
                                     " vs panel_y=" + std::to_string(static_cast<int>(active_panel_rect.y)) +
                                     " panel_h=" + std::to_string(static_cast<int>(active_panel_rect.height));
                violations.push_back({type, label, r, active_panel_rect, reason});
                std::fprintf(stderr, "[UI_OVERFLOW] %s: %s (label='%s')\n",
                             type.c_str(), reason.c_str(), label.c_str());
            }
        }
    }
}

void ImmediateGui::Begin(SpriteBatch *batch, const Input *in, float screen_w, float screen_h, float content_scale) {
    b = batch;
    sw = screen_w;
    sh = screen_h;
    cs_ = content_scale;
    violations.clear();
    prev_clickables = std::move(clickables);
    clickables.clear();
    active_panel_rect = {0.0f, 0.0f, 0.0f, 0.0f};
    active_panel_title.clear();
    if (text_cbs_.set_render_scale) {
        text_cbs_.set_render_scale(content_scale);
    }
    mouse = in ? in->MousePosition() : Vec2{0.0f, 0.0f};
    mouse.x /= ui_scale;
    mouse.y /= ui_scale;
    if (mouse_locked || mouse_override.x >= 0.0f) {
        mouse = mouse_override;
    }
    clicked = click_override || (in && in->MousePressed(0));
    rclicked = in && in->MousePressed(1);
    down = down_override || (in && in->MouseDown(0));
    wheel = in ? in->MouseWheel() : 0.0;
    over_prev_ = over_;
    over_ = false;
    tooltip.clear();
    wheel_used_ = false;
}

void ImmediateGui::End() {}

bool ImmediateGui::Hovered(const Rect &r) const {
    if (suppress) {
        return false;
    }
    if (!clip_stack_.empty()) {
        const Rect &c = clip_stack_.back();
        if (mouse.x < c.x || mouse.y < c.y || mouse.x >= c.x + c.width || mouse.y >= c.y + c.height) {
            return false;
        }
    }
    return mouse.x >= r.x && mouse.y >= r.y && mouse.x < r.x + r.width && mouse.y < r.y + r.height;
}

void ImmediateGui::Block(const Rect &r) {
    if (Hovered(r)) {
        over_ = true;
    }
}

void ImmediateGui::Fill(const Rect &r, const Color &c) {
    if (b) {
        b->DrawQuad(Sr(r), c);
    }
}

void ImmediateGui::Frame(const Rect &r0, const Color &c, float thickness) {
    if (b) {
        b->DrawFrame(Sr(r0), c, thickness * draw_scale);
    }
}

void ImmediateGui::Branch(float x0, float y0, float cx0, float cy0, float cx1, float cy1,
                          float x1, float y1, float w0, float w1, const Color &c) {
    if (b) {
        b->DrawBezierTapered({Sx(x0), Sx(y0)}, {Sx(cx0), Sx(cy0)}, {Sx(cx1), Sx(cy1)}, {Sx(x1), Sx(y1)},
                             w0 * draw_scale, w1 * draw_scale, c);
    }
}

void ImmediateGui::Paper(const Rect &r, const Color &tint) {
    if (b) {
        b->DrawSprite(PaperTex(), Rect{r.x * 0.73f, r.y * 0.61f, r.width, r.height}, Sr(r), tint);
    }
}

void ImmediateGui::Badge(float x, float y, float size, const Color &c, const std::string &glyph) {
    if (!b) {
        return;
    }
    b->DrawSprite(DiscTex(), Sr(Rect{x, y, size, size}), c);
    if (glyph.empty()) {
        return;
    }
    const float luma = 0.3f * c.r + 0.59f * c.g + 0.11f * c.b;
    const Color tc = luma > 0.62f ? theme_.badge_dark_text : theme_.badge_light_text;
    const float px = size * 0.66f;
    Text(x + size * 0.5f, y + (size - px * 1.25f) * 0.5f + size * 0.02f,
         glyph, px, tc, 1, theme_.title_face);
}

void ImmediateGui::GradientV(const Rect &r0, const Color &top, const Color &bottom, int steps) {
    if (b) {
        b->DrawGradientV(Sr(r0), top, bottom, steps);
    }
}

float ImmediateGui::Text(float x, float y, std::string_view s, float px, const Color &c, int align, int face) {
    if (!b || !text_cbs_.draw) {
        return 0.0f;
    }
    const float w = text_cbs_.draw(*b, s, Sx(x), Sx(y), px * draw_scale, c, align, face);
    return draw_scale > 0.0f ? w / draw_scale : w;
}

float ImmediateGui::Text(float x, float y, std::string_view s, float px) {
    return Text(x, y, s, px, theme_.text, 0, theme_.default_face);
}

float ImmediateGui::Wrapped(float x, float y, float w, std::string_view s, float px, const Color &c, int face) {
    if (!b || !text_cbs_.draw_wrapped) {
        return 0.0f;
    }
    const float h = text_cbs_.draw_wrapped(*b, s, Sx(x), Sx(y), px * draw_scale, w * draw_scale, c, face);
    return draw_scale > 0.0f ? h / draw_scale : h;
}

float ImmediateGui::Wrapped(float x, float y, float w, std::string_view s, float px) {
    return Wrapped(x, y, w, s, px, theme_.text, theme_.default_face);
}

float ImmediateGui::Measure(std::string_view s, float px, int face) const {
    return text_cbs_.measure ? text_cbs_.measure(s, px, face) : 0.0f;
}

float ImmediateGui::Measure(std::string_view s, float px) const {
    return Measure(s, px, theme_.default_face);
}

float ImmediateGui::MeasureWrapped(std::string_view s, float px, float width, int face) const {
    return text_cbs_.measure_wrapped ? text_cbs_.measure_wrapped(s, px, width, face) : 0.0f;
}

float ImmediateGui::TextFit(float x, float y, float max_w, std::string_view s, float px, const Color &c,
                            int face, float min_px) {
    float p = px;
    while (p > min_px && Measure(s, p, face) > max_w) {
        p -= 0.5f;
    }
    if (Measure(s, p, face) <= max_w) {
        return Text(x, y + (px - p) * 0.5f, s, p, c, 0, face);
    }
    return Wrapped(x, y - p * 0.45f, max_w, s, p, c, face);
}

void ImmediateGui::Panel(const Rect &r, const std::string &title, bool block_input) {
    active_panel_rect = r;
    active_panel_title = title;
    CheckBounds(r, "panel", title);
    if (block_input) {
        Block(r);
    }
    Fill(Rect{r.x + 4.0f, r.y + 5.0f, r.width, r.height}, {0.0f, 0.0f, 0.0f, 0.35f});
    Paper(r, theme_.paper_tint);
    for (int i = 0; i < 6; ++i) {
        const float fi = static_cast<float>(i);
        Frame(Rect{r.x + fi, r.y + fi, r.width - 2.0f * fi, r.height - 2.0f * fi},
              {0.08f, 0.05f, 0.02f, 0.16f - fi * 0.025f}, 1.0f);
    }
    Frame(r, theme_.accent_dim);
    Color subtle = theme_.accent;
    subtle.a = 0.28f;
    Frame(Rect{r.x + 3.0f, r.y + 3.0f, r.width - 6.0f, r.height - 6.0f}, subtle);
    if (!title.empty()) {
        const Rect h{r.x + 1.0f, r.y + 1.0f, r.width - 2.0f, 38.0f};
        GradientV(h, theme_.title_grad_top, theme_.title_grad_bot);
        Paper(h, theme_.header_paper_tint);
        Fill(Rect{r.x + 1.0f, r.y + 38.0f, r.width - 2.0f, 2.0f}, theme_.accent);
        Color bar_subtle = theme_.accent;
        bar_subtle.a = 0.35f;
        Fill(Rect{r.x + 1.0f, r.y + 41.0f, r.width - 2.0f, 1.0f}, bar_subtle);
        Text(r.x + r.width * 0.5f, r.y + 5.0f, title, 22.0f, theme_.accent, 1, theme_.title_face);
    }
}

bool ImmediateGui::Button(const Rect &r, const std::string &label, bool enabled,
                          const std::string &tp, bool active, float px) {
    CheckBounds(r, "button", label);
    RecordClickable("button", label, tp, r, enabled, active);
    if (audit_bounds && !label.empty()) {
        const float min_w = Measure(label, 9.5f);
        if (min_w > r.width - 4.0f) {
            std::string reason = "Button text label too wide for button: text_w=" +
                                 std::to_string(static_cast<int>(min_w)) +
                                 " button_w=" + std::to_string(static_cast<int>(r.width));
            violations.push_back({"button_text", label, r, r, reason});
            std::fprintf(stderr, "[UI_OVERFLOW] button_text: %s (label='%s')\n",
                         reason.c_str(), label.c_str());
        }
    }
    const bool hot = Hovered(r);
    Color top = active ? theme_.button_active_top : (hot && enabled ? theme_.button_hot_top : theme_.button_top);
    Color bot = active ? theme_.button_active_bot : (hot && enabled ? theme_.button_hot_bot : theme_.button_bot);
    if (!enabled) {
        top = theme_.button_dis_top;
        bot = theme_.button_dis_bot;
    }
    GradientV(r, top, bot, 4);
    Paper(r, {1.0f, 1.0f, 1.0f, 0.07f});
    Frame(r, active ? theme_.accent : (hot && enabled ? theme_.button_hot_border : theme_.button_border));
    if (!label.empty()) {
        float p2 = px;
        while (p2 > 9.5f && Measure(label, p2) > r.width - 8.0f) {
            p2 -= 0.5f;
        }
        Text(r.x + r.width * 0.5f, r.y + (r.height - p2 * 1.25f) * 0.5f, label, p2,
             enabled ? theme_.text : theme_.text_dim, 1, theme_.default_face);
    }
    if (hot && !tp.empty()) {
        tooltip = tp;
    }
    if (hot) {
        over_ = true;
    }
    if (hot && enabled && clicked) {
        click_count++;
    }
    return hot && enabled && clicked;
}

bool ImmediateGui::Slider(const Rect &r, float &v, float lo, float hi, const std::string &tp) {
    CheckBounds(r, "slider", tp);
    RecordClickable("slider", tp, tp, r, true, false);
    const bool hot = Hovered(r);
    const std::string id = std::to_string(static_cast<int>(r.x)) + "," + std::to_string(static_cast<int>(r.y));
    if (hot && clicked) {
        drag_id_ = id;
    }
    if (!down) {
        drag_id_.clear();
    }
    bool changed = false;
    if (drag_id_ == id) {
        const float t = std::clamp((mouse.x - r.x - 6.0f) / std::max(1.0f, r.width - 12.0f), 0.0f, 1.0f);
        const float nv = lo + (hi - lo) * t;
        if (nv != v) {
            v = nv;
            changed = true;
        }
        over_ = true;
    }
    const float t = (v - lo) / std::max(1e-6f, hi - lo);
    Fill(Rect{r.x + 6.0f, r.y + r.height * 0.5f - 3.0f, r.width - 12.0f, 6.0f}, {0.0f, 0.0f, 0.0f, 0.5f});
    Fill(Rect{r.x + 6.0f, r.y + r.height * 0.5f - 3.0f, (r.width - 12.0f) * t, 6.0f}, theme_.accent_dim);
    const Rect knob{r.x + 6.0f + (r.width - 12.0f) * t - 6.0f, r.y + 2.0f, 12.0f, r.height - 4.0f};
    GradientV(knob, theme_.slider_knob_top, theme_.slider_knob_bot, 3);
    Frame(knob, theme_.accent);
    if (hot) {
        over_ = true;
        if (!tp.empty()) {
            tooltip = tp;
        }
    }
    return changed;
}

bool ImmediateGui::Checkbox(const Rect &r, bool &v, const std::string &label, const std::string &tp) {
    CheckBounds(r, "checkbox", label);
    RecordClickable("checkbox", label, tp, r, true, v);
    const bool hot = Hovered(r);
    const Rect box{r.x, r.y + (r.height - 18.0f) * 0.5f, 18.0f, 18.0f};
    Fill(box, {0.0f, 0.0f, 0.0f, 0.45f});
    Frame(box, hot ? theme_.accent : theme_.accent_dim);
    if (v) {
        Fill(Rect{box.x + 4.0f, box.y + 4.0f, 10.0f, 10.0f}, theme_.accent);
    }
    Text(r.x + 26.0f, r.y + (r.height - 14.0f * 1.25f) * 0.5f, label, 14.0f, theme_.text, 0, theme_.default_face);
    if (hot) {
        over_ = true;
        if (!tp.empty()) {
            tooltip = tp;
        }
    }
    if (hot && clicked) {
        v = !v;
        click_count++;
        return true;
    }
    return false;
}

bool ImmediateGui::CloseButton(const Rect &r) {
    CheckBounds(r, "close_button", "×");
    RecordClickable("close_button", "×", "", r, true, false);
    const bool hot = Hovered(r);
    Fill(r, hot ? theme_.close_hot : theme_.close_bg);
    Frame(r, theme_.accent_dim);
    Text(r.x + r.width * 0.5f, r.y + 1.0f, "×", r.height - 4.0f, theme_.text, 1, theme_.default_face);
    return hot && clicked;
}

bool ImmediateGui::InvisibleButton(const Rect &r, const std::string &tp) {
    CheckBounds(r, "invisible_button", tp);
    RecordClickable("invisible_button", tp, tp, r, true, false);
    const bool hot = Hovered(r);
    if (hot) {
        Fill(r, {1.0f, 0.9f, 0.6f, 0.06f});
        if (!tp.empty()) {
            tooltip = tp;
        }
    }
    return hot && clicked;
}

void ImmediateGui::Bar(const Rect &r, float frac, const Color &c, const Color &bg) {
    CheckBounds(r, "bar", "");
    Fill(r, bg);
    Fill(Rect{r.x, r.y, r.width * std::clamp(frac, 0.0f, 1.0f), r.height}, c);
    Frame(r, {0.0f, 0.0f, 0.0f, 0.5f});
}

void ImmediateGui::Tip(const Rect &r, const std::string &t) {
    if (Hovered(r)) {
        tooltip = t;
    }
}

void ImmediateGui::Section(float x, float y, float w, const std::string &title) {
    CheckBounds(Rect{x, y, w, 22.0f}, "section", title);
    Fill(Rect{x, y + 10.0f, w, 1.0f}, theme_.accent_dim);
    const float tw = Measure(title, 15.0f) + 16.0f;
    Fill(Rect{x + 8.0f, y, tw, 21.0f}, theme_.panel);
    Text(x + 16.0f, y + 1.0f, title, 15.0f, theme_.accent, 0, theme_.default_face);
}

void ImmediateGui::Clip(const Rect *r) {
    if (!b) {
        return;
    }
    b->Flush();
    if (!r) {
        b->ClearClipRect();
        return;
    }
    const Rect p = Sr(*r);
    const float shw = sh * draw_scale;
    b->SetClipRectPhysical(Rect{p.x * cs_, (shw - (p.y + p.height)) * cs_, p.width * cs_, p.height * cs_});
}

float ImmediateGui::BeginScroll(const std::string &id, const Rect &r, float content_h) {
    float &off = scroll_[id];
    Rect view = r;
    if (!clip_stack_.empty()) {
        view = RectIntersect(r, clip_stack_.back());
    }
    if (Hovered(view) && wheel != 0.0 && !wheel_used_) {
        off -= static_cast<float>(wheel) * 50.0f;
        wheel_used_ = true;
    }
    off = std::clamp(off, 0.0f, std::max(0.0f, content_h - r.height));
    clip_stack_.push_back(view);
    Clip(&view);
    if (content_h > r.height) {
        const float th = r.height * r.height / content_h;
        const float ty = r.y + (r.height - th) * (off / std::max(1.0f, content_h - r.height));
        Fill(Rect{r.x + r.width - 5.0f, r.y, 4.0f, r.height}, {0.0f, 0.0f, 0.0f, 0.3f});
        Fill(Rect{r.x + r.width - 5.0f, ty, 4.0f, th}, theme_.accent_dim);
    }
    return off;
}

void ImmediateGui::EndScroll() {
    if (clip_stack_.empty()) {
        return;
    }
    clip_stack_.pop_back();
    if (clip_stack_.empty()) {
        Clip(nullptr);
    } else {
        Clip(&clip_stack_.back());
    }
}

} // namespace fake2d
