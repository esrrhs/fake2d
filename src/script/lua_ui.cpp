#include "fake2d/lua_ui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#include "fake2d/engine.h"
#include "fake2d/input.h"
#include "fake2d/text_utils.h"
#include "fake2d/ui.h"
#include "fakelua.h"

namespace fake2d {

// ---------------------------------------------------------------------------
// ColorPool
// ---------------------------------------------------------------------------

std::int64_t ColorPool::Id(const Color &c) {
    static_assert(sizeof(Color) == 16);
    char key[16];
    std::memcpy(key, &c, sizeof c);
    std::string k(key, sizeof c);
    const auto it = key_to_id_.find(k);
    if (it != key_to_id_.end()) {
        return it->second;
    }
    colors_.push_back(c);
    const std::int64_t id = static_cast<std::int64_t>(colors_.size());
    key_to_id_.emplace(std::move(k), id);
    return id;
}

Color ColorPool::Get(std::int64_t id, const Color &fallback) const {
    if (id > 0 && id <= static_cast<std::int64_t>(colors_.size())) {
        return colors_[static_cast<std::size_t>(id - 1)];
    }
    return fallback;
}

std::int64_t ColorPool::Pack(double r, double g, double b, double a) {
    return Id(Color{static_cast<float>(r), static_cast<float>(g), static_cast<float>(b), static_cast<float>(a)});
}

void ColorPool::SetThemeColor(std::string name, const Color &c) {
    theme_ids_[std::move(name)] = Id(c);
}

std::int64_t ColorPool::ThemeId(const std::string &name, const Color &fallback) const {
    const auto it = theme_ids_.find(name);
    if (it != theme_ids_.end()) {
        return it->second;
    }
    std::fprintf(stderr, "ui script: unknown theme color '%s'\n", name.c_str());
    return const_cast<ColorPool *>(this)->Id(fallback);
}

void ColorPool::Clear() {
    colors_.clear();
    key_to_id_.clear();
    theme_ids_.clear();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

using LS = fakelua::State;
using CVar = fakelua::CVar;

inline std::string ArgStr(LS *s, CVar *a, int n, int i) {
    return fakelua::inter::FakeluaToNativeString(s, fakelua::inter::GetNativeArg(s, a, n, i));
}

inline double ArgNum(LS *s, CVar *a, int n, int i) {
    return fakelua::inter::FakeluaToNativeDouble(s, fakelua::inter::GetNativeArg(s, a, n, i));
}

template <typename Sig, typename F>
void Reg(LS *s, LuaUIHost *host, const char *name, F fn) {
    if (host && host->note_native) {
        host->note_native(name);
    }
    fakelua::RegisterNativeFunction(s, name, false, std::function<Sig>(std::move(fn)));
}

template <typename F>
void RegVar(LS *s, LuaUIHost *host, const char *name, int fixed, F fn) {
    if (host && host->note_native) {
        host->note_native(name);
    }
    fakelua::RegisterNativeFunction(
        s, name, fixed, true,
        [fn = std::move(fn)](LS *st, CVar *args, int n) -> CVar {
            return fakelua::inter::NativeToFakelua(st, fn(st, args, n));
        });
}

UITextOverflow ParseOverflow(const std::string &mode) {
    if (mode == "ellipsis") return UITextOverflow::Ellipsis;
    if (mode == "shrink") return UITextOverflow::Shrink;
    if (mode == "clip") return UITextOverflow::Clip;
    return UITextOverflow::Wrap;
}

Color MixColor(const Color &a, const Color &b, float t) {
    return {
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a + (b.a - a.a) * t,
    };
}

} // namespace

std::string FormatFromLuaArgs(LS *s, CVar *args, int n) {
    const std::string fmt = ArgStr(s, args, n, 0);
    std::string out;
    int ai = 1;
    char buf[512];
    for (std::size_t i = 0; i < fmt.size();) {
        if (fmt[i] != '%') {
            out += fmt[i++];
            continue;
        }
        if (i + 1 < fmt.size() && fmt[i + 1] == '%') {
            out += '%';
            i += 2;
            continue;
        }
        std::size_t j = i + 1;
        while (j < fmt.size() && std::strchr("-+ #0123456789.", fmt[j])) {
            ++j;
        }
        if (j >= fmt.size()) {
            out += fmt.substr(i);
            break;
        }
        const char conv = fmt[j];
        const std::string spec = fmt.substr(i, j - i + 1);
        if (conv == 's') {
            const std::string v = ai < n ? ArgStr(s, args, n, ai++) : std::string();
            std::snprintf(buf, sizeof buf, spec.c_str(), v.c_str());
            out += buf;
        } else if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'x' || conv == 'X' || conv == 'o') {
            const long long v = ai < n ? static_cast<long long>(ArgNum(s, args, n, ai++)) : 0;
            std::snprintf(buf, sizeof buf, spec.c_str(), v);
            out += buf;
        } else if (conv == 'f' || conv == 'F' || conv == 'e' || conv == 'E' ||
                   conv == 'g' || conv == 'G' || conv == 'a' || conv == 'A') {
            const double v = ai < n ? ArgNum(s, args, n, ai++) : 0.0;
            std::snprintf(buf, sizeof buf, spec.c_str(), v);
            out += buf;
        } else {
            out += spec;
            ++ai;
        }
        i = j + 1;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Immediate-mode GUI Lua bindings (u_*)
// ---------------------------------------------------------------------------

void RegisterImmediateGuiLuaApi(LS *s, LuaUIHost *host) {
    if (!s || !host) {
        return;
    }
    auto col_of = [host](std::int64_t id) -> Color {
        return host->colors ? host->colors->Get(id, host->default_text_color) : host->default_text_color;
    };
    auto col_id = [host](const Color &c) -> std::int64_t {
        return host->colors ? host->colors->Id(c) : 0;
    };
    auto rc = [](double x, double y, double w, double h) -> Rect {
        return Rect{static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)};
    };

    // Colors
    Reg<std::int64_t(LS *, double, double, double, double)>(
        s, host, "u_col", [host](LS *, double r, double g, double b, double a) -> std::int64_t {
            return host->colors ? host->colors->Pack(r, g, b, a) : 0;
        });
    Reg<std::int64_t(LS *, std::int64_t, double)>(
        s, host, "u_hex", [col_id](LS *, std::int64_t c, double a) -> std::int64_t {
            const unsigned u = static_cast<unsigned>(c);
            return col_id(Color{
                static_cast<float>((u >> 16) & 255u) / 255.0f,
                static_cast<float>((u >> 8) & 255u) / 255.0f,
                static_cast<float>(u & 255u) / 255.0f,
                static_cast<float>(a),
            });
        });
    Reg<std::int64_t(LS *, std::int64_t, std::int64_t, double)>(
        s, host, "u_mix", [col_id, col_of](LS *, std::int64_t a, std::int64_t b, double t) -> std::int64_t {
            return col_id(MixColor(col_of(a), col_of(b), static_cast<float>(t)));
        });
    Reg<std::int64_t(LS *, std::string)>(
        s, host, "u_theme", [host](LS *, std::string name) -> std::int64_t {
            return host->colors ? host->colors->ThemeId(name, host->default_text_color) : 0;
        });
    Reg<std::int64_t(LS *, std::int64_t, double, double, double)>(
        s, host, "u_lighten", [col_id, col_of](LS *, std::int64_t c, double dr, double dg, double db) -> std::int64_t {
            Color x = col_of(c);
            x.r += static_cast<float>(dr);
            x.g += static_cast<float>(dg);
            x.b += static_cast<float>(db);
            x.a = 1.0f;
            return col_id(x);
        });
    Reg<std::int64_t(LS *, std::int64_t, double)>(
        s, host, "u_dim", [col_id, col_of](LS *, std::int64_t c, double k) -> std::int64_t {
            const Color x = col_of(c);
            const float kk = static_cast<float>(k);
            return col_id(Color{x.r * kk, x.g * kk, x.b * kk, 1.0f});
        });
    Reg<double(LS *, std::int64_t, std::int64_t)>(
        s, host, "u_col_get", [col_of](LS *, std::int64_t id, std::int64_t ch) -> double {
            const Color c = col_of(id);
            if (ch == 0) return c.r;
            if (ch == 1) return c.g;
            if (ch == 2) return c.b;
            return c.a;
        });
    Reg<std::int64_t(LS *, std::int64_t, double)>(
        s, host, "u_col_a", [col_id, col_of](LS *, std::int64_t id, double a) -> std::int64_t {
            Color c = col_of(id);
            c.a = static_cast<float>(a);
            return col_id(c);
        });
    Reg<std::int64_t(LS *, std::int64_t, std::int64_t, double)>(
        s, host, "u_lerp_hex", [col_id](LS *, std::int64_t p, std::int64_t q, double td) -> std::int64_t {
            const float t = static_cast<float>(td);
            auto ch = [p, q, t](int sh) {
                return static_cast<float>(
                    ((static_cast<double>((p >> sh) & 255) * (1.0f - t) +
                      static_cast<double>((q >> sh) & 255) * t) / 255.0f));
            };
            return col_id(Color{ch(16), ch(8), ch(0), 1.0f});
        });

    // Text & UTF-8
    Reg<double(LS *, double, double, std::string, double, std::int64_t, std::int64_t, std::int64_t)>(
        s, host, "u_text",
        [host, col_of](LS *, double x, double y, std::string t, double px, std::int64_t c,
                       std::int64_t align, std::int64_t face) -> double {
            return host->gui ? host->gui->Text(static_cast<float>(x), static_cast<float>(y), t,
                                               static_cast<float>(px), col_of(c),
                                               static_cast<int>(align), static_cast<int>(face))
                             : 0.0;
        });
    Reg<double(LS *, double, double, double, std::string, double, std::int64_t, std::int64_t)>(
        s, host, "u_wrapped",
        [host, col_of](LS *, double x, double y, double w, std::string t, double px,
                       std::int64_t c, std::int64_t face) -> double {
            return host->gui ? host->gui->Wrapped(static_cast<float>(x), static_cast<float>(y),
                                                  static_cast<float>(w), t, static_cast<float>(px),
                                                  col_of(c), static_cast<int>(face))
                             : 0.0;
        });
    Reg<double(LS *, std::string, double, std::int64_t)>(
        s, host, "u_measure", [host](LS *, std::string t, double px, std::int64_t face) -> double {
            return host->gui ? host->gui->Measure(t, static_cast<float>(px), static_cast<int>(face)) : 0.0;
        });
    Reg<double(LS *, std::string, double, double, std::int64_t)>(
        s, host, "u_measure_wrapped",
        [host](LS *, std::string t, double px, double w, std::int64_t face) -> double {
            return host->gui ? host->gui->MeasureWrapped(t, static_cast<float>(px),
                                                         static_cast<float>(w), static_cast<int>(face))
                             : 0.0;
        });
    Reg<double(LS *, double, double, double, std::string, double, std::int64_t, std::int64_t, double)>(
        s, host, "u_text_fit",
        [host, col_of](LS *, double x, double y, double mw, std::string t, double px,
                       std::int64_t c, std::int64_t face, double min_px) -> double {
            return host->gui ? host->gui->TextFit(static_cast<float>(x), static_cast<float>(y),
                                                  static_cast<float>(mw), t, static_cast<float>(px),
                                                  col_of(c), static_cast<int>(face),
                                                  static_cast<float>(min_px))
                             : 0.0;
        });
    Reg<std::int64_t(LS *, std::string)>(
        s, host, "u_utf8_len", [](LS *, std::string t) -> std::int64_t {
            return static_cast<std::int64_t>(utf8::Count(t));
        });
    Reg<std::string(LS *, std::string)>(
        s, host, "u_utf8_first", [](LS *, std::string t) -> std::string { return utf8::First(t); });
    RegVar(s, host, "u_strf", 1, [](LS *st, CVar *a, int n) -> std::string {
        return FormatFromLuaArgs(st, a, n);
    });
    Reg<std::string(LS *, std::string, std::int64_t)>(
        s, host, "u_utf8_cut", [](LS *, std::string t, std::int64_t n) -> std::string {
            return utf8::Cut(t, static_cast<std::size_t>(n < 0 ? 0 : n));
        });

    // Primitives
    Reg<void(LS *, double, double, double, double, std::int64_t)>(
        s, host, "u_fill", [host, rc, col_of](LS *, double x, double y, double w, double h, std::int64_t c) {
            if (host->gui) host->gui->Fill(rc(x, y, w, h), col_of(c));
        });
    Reg<void(LS *, double, double, double, double, double, double, double, double, double, double, std::int64_t)>(
        s, host, "u_branch",
        [host, col_of](LS *, double x0, double y0, double cx0, double cy0, double cx1, double cy1,
                       double x1, double y1, double w0, double w1, std::int64_t c) {
            if (host->gui) {
                host->gui->Branch(static_cast<float>(x0), static_cast<float>(y0),
                                  static_cast<float>(cx0), static_cast<float>(cy0),
                                  static_cast<float>(cx1), static_cast<float>(cy1),
                                  static_cast<float>(x1), static_cast<float>(y1),
                                  static_cast<float>(w0), static_cast<float>(w1), col_of(c));
            }
        });
    Reg<void(LS *, double, double, double, double, std::int64_t, double)>(
        s, host, "u_frame",
        [host, rc, col_of](LS *, double x, double y, double w, double h, std::int64_t c, double t) {
            if (host->gui) host->gui->Frame(rc(x, y, w, h), col_of(c), static_cast<float>(t));
        });
    Reg<void(LS *, double, double, double, double, std::int64_t)>(
        s, host, "u_paper", [host, rc, col_of](LS *, double x, double y, double w, double h, std::int64_t c) {
            if (host->gui) host->gui->Paper(rc(x, y, w, h), col_of(c));
        });
    Reg<void(LS *, double, double, double, double, std::int64_t, std::int64_t, std::int64_t)>(
        s, host, "u_gradient",
        [host, rc, col_of](LS *, double x, double y, double w, double h, std::int64_t a,
                           std::int64_t b, std::int64_t steps) {
            if (host->gui) host->gui->GradientV(rc(x, y, w, h), col_of(a), col_of(b), static_cast<int>(steps));
        });
    Reg<void(LS *, double, double, double, std::int64_t, std::string)>(
        s, host, "u_badge",
        [host, col_of](LS *, double x, double y, double size, std::int64_t c, std::string glyph) {
            if (host->gui) {
                host->gui->Badge(static_cast<float>(x), static_cast<float>(y),
                                 static_cast<float>(size), col_of(c), glyph);
            }
        });
    Reg<void(LS *, double, double, double, double, std::string, std::int64_t)>(
        s, host, "u_panel",
        [host, rc](LS *, double x, double y, double w, double h, std::string title, std::int64_t blk) {
            if (host->gui) host->gui->Panel(rc(x, y, w, h), title, blk != 0);
        });
    Reg<void(LS *, double, double, double, double)>(
        s, host, "u_block", [host, rc](LS *, double x, double y, double w, double h) {
            if (!host->gui) return;
            const Rect r = rc(x, y, w, h);
            host->gui->Block(r);
            if (!host->gui->suppress && host->tips) {
                host->tips->RecordOccluder(r);
            }
        });
    Reg<void(LS *, double, double, double, std::string)>(
        s, host, "u_section", [host](LS *, double x, double y, double w, std::string t) {
            if (host->gui) host->gui->Section(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), t);
        });

    // Interactive widgets
    Reg<bool(LS *, double, double, double, double, std::string, std::int64_t, std::string, std::int64_t, double)>(
        s, host, "u_button",
        [host, rc](LS *, double x, double y, double w, double h, std::string label, std::int64_t en,
                   std::string tip, std::int64_t active, double px) -> bool {
            if (!host->gui) return false;
            const Rect r = rc(x, y, w, h);
            const bool clk = host->gui->Button(r, label, en != 0, tip, active != 0, static_cast<float>(px));
            if (!tip.empty() && host->tips && host->gui->Hovered(r)) {
                host->tips->RecordEntry(r, tip);
            }
            return clk;
        });
    Reg<bool(LS *, double, double)>(
        s, host, "u_close", [host](LS *, double x, double y) -> bool {
            return host->gui && host->gui->CloseButton(Rect{static_cast<float>(x), static_cast<float>(y), 22.0f, 22.0f});
        });
    Reg<bool(LS *, double, double, double, double, std::string)>(
        s, host, "u_invisible",
        [host, rc](LS *, double x, double y, double w, double h, std::string tip) -> bool {
            if (!host->gui) return false;
            const Rect r = rc(x, y, w, h);
            const bool clk = host->gui->InvisibleButton(r, tip);
            if (!tip.empty() && host->tips && host->gui->Hovered(r)) {
                host->tips->RecordEntry(r, tip);
            }
            return clk;
        });
    Reg<bool(LS *, double, double, double, double, std::string)>(
        s, host, "u_tip", [host, rc](LS *, double x, double y, double w, double h, std::string t) -> bool {
            if (!host->gui) return true;
            const Rect r = rc(x, y, w, h);
            host->gui->Tip(r, t);
            if (!t.empty() && host->tips && host->gui->Hovered(r)) {
                host->tips->RecordEntry(r, t);
            }
            return true;
        });
    Reg<bool(LS *, double, double, double, double)>(
        s, host, "u_hovered", [host, rc](LS *, double x, double y, double w, double h) -> bool {
            return host->gui && host->gui->Hovered(rc(x, y, w, h));
        });
    Reg<double(LS *, double, double, double, double, double, double, double, std::string)>(
        s, host, "u_slider",
        [host, rc](LS *, double x, double y, double w, double h, double lo, double hi,
                   double cur, std::string tip) -> double {
            if (!host->gui) return cur;
            const Rect r = rc(x, y, w, h);
            float v = static_cast<float>(cur);
            host->gui->Slider(r, v, static_cast<float>(lo), static_cast<float>(hi), tip);
            if (!tip.empty() && host->tips && host->gui->Hovered(r)) {
                host->tips->RecordEntry(r, tip);
            }
            return v;
        });
    Reg<std::int64_t(LS *, double, double, double, double, std::int64_t, std::string, std::string)>(
        s, host, "u_checkbox",
        [host, rc](LS *, double x, double y, double w, double h, std::int64_t v,
                   std::string label, std::string tip) -> std::int64_t {
            if (!host->gui) return v;
            const Rect r = rc(x, y, w, h);
            bool on = v != 0;
            host->gui->Checkbox(r, on, label, tip);
            if (!tip.empty() && host->tips && host->gui->Hovered(r)) {
                host->tips->RecordEntry(r, tip);
            }
            return on ? 1 : 0;
        });
    Reg<void(LS *, double, double, double, double, double, std::int64_t, std::int64_t)>(
        s, host, "u_bar",
        [host, rc, col_of](LS *, double x, double y, double w, double h, double frac,
                           std::int64_t c, std::int64_t bg) {
            if (host->gui) {
                host->gui->Bar(rc(x, y, w, h), static_cast<float>(frac), col_of(c), col_of(bg));
            }
        });
    Reg<double(LS *, std::string, double, double, double, double, double)>(
        s, host, "u_scroll_begin",
        [host, rc](LS *, std::string id, double x, double y, double w, double h, double content) -> double {
            return host->gui ? host->gui->BeginScroll(id, rc(x, y, w, h), static_cast<float>(content)) : 0.0;
        });
    Reg<void(LS *)>(s, host, "u_scroll_end", [host](LS *) {
        if (host->gui) host->gui->EndScroll();
    });
    Reg<void(LS *, std::int64_t)>(s, host, "u_set_modal", [host](LS *, std::int64_t m) {
        if (!host->gui) return;
        host->gui->SetModal(m != 0);
        if (m != 0 && host->tips) {
            host->tips->RecordOccluder(Rect{0.0f, 0.0f, host->gui->sw, host->gui->sh});
        }
    });
    Reg<void(LS *, std::int64_t)>(s, host, "u_set_suppress", [host](LS *, std::int64_t v) {
        if (host->gui) host->gui->suppress = (v != 0);
    });
}

// ---------------------------------------------------------------------------
// Box-layout UISystem Lua bindings (ui_*)
// ---------------------------------------------------------------------------

void RegisterLayoutUILuaApi(LS *s, LuaUIHost *host) {
    if (!s || !host) {
        return;
    }
    auto ui_sys = [host]() -> UISystem * {
        return host->engine ? &host->engine->GetUI() : nullptr;
    };
    auto col_of = [host](std::int64_t id) -> Color {
        return host->colors ? host->colors->Get(id, host->default_text_color) : host->default_text_color;
    };
    auto load_tex = [host](const std::string &name) -> const Texture2D * {
        if (!host->textures) return nullptr;
        return host->textures->GetOrLoad(name, host->root_dir, host->texture_subdir);
    };

    Reg<void(LS *, double, double, double, double, double)>(
        s, host, "ui_push", [ui_sys](LS *, double x, double y, double w, double h, double pad) {
            if (auto *u = ui_sys()) {
                u->Push(static_cast<float>(x), static_cast<float>(y),
                        static_cast<float>(w), static_cast<float>(h), static_cast<float>(pad));
            }
        });
    Reg<void(LS *)>(s, host, "ui_pop", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->Pop();
    });
    RegVar(s, host, "ui_text", 1, [ui_sys, col_of](LS *st, CVar *a, int n) -> double {
        auto *u = ui_sys();
        if (!u) return 0.0;
        const std::string text = ArgStr(st, a, n, 0);
        const float scale = n > 1 ? static_cast<float>(ArgNum(st, a, n, 1)) : 0.0f;
        const std::string mode = n > 2 ? ArgStr(st, a, n, 2) : std::string("wrap");
        const int align = n > 3 ? static_cast<int>(ArgNum(st, a, n, 3)) : 0;
        const auto col = n > 4 && ArgNum(st, a, n, 4) > 0
                             ? col_of(static_cast<std::int64_t>(ArgNum(st, a, n, 4)))
                             : Color{1.0f, 1.0f, 1.0f, 1.0f};
        const float min_s = n > 5 ? static_cast<float>(ArgNum(st, a, n, 5)) : 9.5f / 32.0f;
        return u->Text(text, scale, ParseOverflow(mode), align, col, min_s);
    });
    Reg<double(LS *)>(s, host, "ui_avail_w", [ui_sys](LS *) -> double {
        auto *u = ui_sys();
        return u ? u->AvailWidth() : 0.0;
    });
    Reg<double(LS *)>(s, host, "ui_avail_h", [ui_sys](LS *) -> double {
        auto *u = ui_sys();
        return u ? u->AvailHeight() : 0.0;
    });
    Reg<double(LS *, std::string, double)>(s, host, "ui_auto_h", [ui_sys](LS *, std::string key, double fb) -> double {
        auto *u = ui_sys();
        return u ? u->AutoHeight(key, static_cast<float>(fb)) : fb;
    });
    Reg<void(LS *, std::string)>(s, host, "ui_record_auto_h", [ui_sys](LS *, std::string key) {
        if (auto *u = ui_sys()) u->RecordAutoHeight(key);
    });
    Reg<double(LS *, std::string)>(s, host, "ui_last_rect", [ui_sys](LS *, std::string field) -> double {
        auto *u = ui_sys();
        if (!u) return 0.0;
        const auto r = u->LastRect();
        if (field == "x") return r.x;
        if (field == "y") return r.y;
        if (field == "w") return r.width;
        if (field == "h") return r.height;
        return 0.0;
    });
    RegVar(s, host, "ui_row", 0, [ui_sys](LS *st, CVar *a, int n) -> double {
        auto *u = ui_sys();
        if (!u) return 0.0;
        std::vector<float> cols;
        for (int i = 1; i < n; ++i) {
            cols.push_back(static_cast<float>(ArgNum(st, a, n, i)));
        }
        u->Row(n > 0 ? static_cast<float>(ArgNum(st, a, n, 0)) : 0.0f, cols);
        return 0.0;
    });
    Reg<void(LS *, double)>(s, host, "ui_col", [ui_sys](LS *, double w) {
        if (auto *u = ui_sys()) u->Col(static_cast<float>(w));
    });
    Reg<void(LS *)>(s, host, "ui_col_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->ColEnd();
    });
    Reg<void(LS *)>(s, host, "ui_row_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->RowEnd();
    });
    Reg<void(LS *, std::string, double)>(
        s, host, "ui_scroll_begin", [host, ui_sys](LS *, std::string key, double h) {
            auto *u = ui_sys();
            if (!u) return;
            const float vw = u->AvailWidth();
            const float vh = h > 0.0 ? static_cast<float>(h) : u->AvailHeight();
            const float outer_off = host->tips ? host->tips->CurrentScrollOffset() : 0.0f;
            u->ScrollBegin(key, static_cast<float>(h));
            u->Push(0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
            u->Pop();
            const auto pos = u->LastRect();
            const Rect vp{pos.x, pos.y - outer_off, vw, std::max(0.0f, vh)};
            if (host->tips) {
                host->tips->PushScroll(std::move(key), vp, u->ScrollOffset(key));
            }
        });
    Reg<double(LS *)>(s, host, "ui_scroll_end", [host, ui_sys](LS *) -> double {
        if (host->tips) host->tips->PopScroll();
        auto *u = ui_sys();
        return u ? u->ScrollEnd() : 0.0;
    });
    Reg<double(LS *, std::string)>(s, host, "ui_token", [ui_sys](LS *, std::string name) -> double {
        auto *u = ui_sys();
        return u ? u->LayoutToken(name) : 0.0;
    });
    Reg<void(LS *, std::string)>(s, host, "ui_tip", [host, ui_sys](LS *, std::string t) {
        auto *u = ui_sys();
        if (!u || t.empty() || !host->tips) return;
        if (host->gui && host->gui->suppress) return;
        const auto lr = u->LastRect();
        host->tips->RecordClippedEntry(Rect{lr.x, lr.y, lr.width, lr.height}, std::move(t));
    });
    Reg<void(LS *, double, double, double, double, std::string)>(
        s, host, "ui_tip_rect", [host](LS *, double x, double y, double w, double h, std::string t) {
            if (t.empty() || !host->tips || (host->gui && host->gui->suppress)) return;
            host->tips->RecordEntry(
                Rect{static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)},
                std::move(t));
        });
    Reg<void(LS *, double, double, double, double)>(
        s, host, "ui_tip_occlude", [host](LS *, double x, double y, double w, double h) {
            if (!host->tips || (host->gui && host->gui->suppress)) return;
            host->tips->RecordOccluder(
                Rect{static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)});
        });
    Reg<std::string(LS *)>(s, host, "ui_tip_poll", [host](LS *) -> std::string {
        if (!host->tips) return {};
        const Vec2 m = host->gui ? host->gui->mouse : Vec2{-10000.0f, -10000.0f};
        const std::string_view fb = host->gui ? std::string_view(host->gui->tooltip) : std::string_view{};
        return host->tips->Poll(m, fb);
    });
    Reg<double(LS *, std::string)>(s, host, "ui_tip_poll_rect", [host](LS *, std::string field) -> double {
        if (!host->tips) return 0.0;
        const Rect &r = host->tips->PolledRect();
        if (field == "x") return r.x;
        if (field == "y") return r.y;
        if (field == "w") return r.width;
        if (field == "h") return r.height;
        return 0.0;
    });
    Reg<bool(LS *)>(s, host, "ui_tip_mid_click", [host](LS *) -> bool {
        return host->engine && host->engine->GetInput().MousePressed(2);
    });
    Reg<bool(LS *)>(s, host, "ui_tip_key_lock", [host](LS *) -> bool {
        return host->engine && host->engine->GetInput().KeyPressed("l");
    });
    Reg<bool(LS *)>(s, host, "ui_tip_key_esc", [host](LS *) -> bool {
        return host->engine && host->engine->GetInput().KeyPressed("escape");
    });

    RegVar(s, host, "ui_panel", 0, [ui_sys, col_of](LS *st, CVar *a, int n) -> double {
        auto *u = ui_sys();
        if (!u) return 0.0;
        if (n <= 0) {
            u->ThemedPanel();
        } else if (n > 1 && ArgNum(st, a, n, 1) > 0) {
            u->Panel(col_of(static_cast<std::int64_t>(ArgNum(st, a, n, 0))),
                     col_of(static_cast<std::int64_t>(ArgNum(st, a, n, 1))));
        } else {
            u->Panel(col_of(static_cast<std::int64_t>(ArgNum(st, a, n, 0))));
        }
        return 0.0;
    });
    RegVar(s, host, "ui_panel_skin", 1, [](LS *, CVar *, int) -> double { return 0.0; });
    RegVar(s, host, "ui_sprite", 5, [host, ui_sys, col_of, load_tex](LS *st, CVar *a, int n) -> double {
        auto *u = ui_sys();
        if (!u) return 0.0;
        const std::string name = ArgStr(st, a, n, 0);
        const float x = static_cast<float>(ArgNum(st, a, n, 1));
        const float y = static_cast<float>(ArgNum(st, a, n, 2));
        const float w = static_cast<float>(ArgNum(st, a, n, 3));
        const float h = static_cast<float>(ArgNum(st, a, n, 4));
        Color tint = Color::White();
        if (n > 5 && ArgNum(st, a, n, 5) > 0) {
            tint = col_of(static_cast<std::int64_t>(ArgNum(st, a, n, 5)));
        }
        if (const auto *tex = load_tex(name)) {
            if (host->before_sprite_draw) {
                host->before_sprite_draw();
            }
            u->DrawSpriteAt(tex, x, y, w, h, tint);
            return 1.0;
        }
        return 0.0;
    });
    RegVar(s, host, "ui_img_box", 2, [ui_sys, col_of, load_tex](LS *st, CVar *a, int n) -> double {
        auto *u = ui_sys();
        if (!u) return 0.0;
        const std::string name = ArgStr(st, a, n, 0);
        const float h = static_cast<float>(ArgNum(st, a, n, 1));
        const auto tint = n > 2 && ArgNum(st, a, n, 2) > 0
                              ? col_of(static_cast<std::int64_t>(ArgNum(st, a, n, 2)))
                              : Color::White();
        if (const auto *tex = load_tex(name)) {
            const Rect src{0.0f, 0.0f, static_cast<float>(tex->Width()), static_cast<float>(tex->Height())};
            u->Image(tex, src, h, tint);
            return 1.0;
        }
        return 0.0;
    });
    Reg<double(LS *, std::string)>(s, host, "ui_tex_ok", [load_tex](LS *, std::string name) -> double {
        return load_tex(name) ? 1.0 : 0.0;
    });
    RegVar(s, host, "ui_fill_rect", 5, [ui_sys, col_of](LS *st, CVar *a, int n) -> double {
        auto *u = ui_sys();
        if (!u) return 0.0;
        const float x = static_cast<float>(ArgNum(st, a, n, 0));
        const float y = static_cast<float>(ArgNum(st, a, n, 1));
        const float w = static_cast<float>(ArgNum(st, a, n, 2));
        const float h = static_cast<float>(ArgNum(st, a, n, 3));
        const auto fill = col_of(static_cast<std::int64_t>(ArgNum(st, a, n, 4)));
        const auto brd = n > 5 && ArgNum(st, a, n, 5) > 0
                             ? col_of(static_cast<std::int64_t>(ArgNum(st, a, n, 5)))
                             : Color::Clear();
        u->FillRect(x, y, w, h, fill, brd);
        return 0.0;
    });
    Reg<bool(LS *, std::string, std::string, double, double, double, double, double)>(
        s, host, "ui_push_related",
        [ui_sys](LS *, std::string target, std::string edge, double off, double w, double h,
                 double pad, double inset) -> bool {
            auto *u = ui_sys();
            return u && u->PushRelated(target, edge, static_cast<float>(off), static_cast<float>(w),
                                       static_cast<float>(h), static_cast<float>(pad), static_cast<float>(inset));
        });
    Reg<void(LS *, std::string)>(s, host, "ui_remember", [ui_sys](LS *, std::string name) {
        if (auto *u = ui_sys()) u->Remember(name);
    });
    Reg<double(LS *, std::string, std::string)>(s, host, "ui_named", [ui_sys](LS *, std::string name, std::string field) -> double {
        auto *u = ui_sys();
        if (!u) return 0.0;
        const auto r = u->NamedRect(name);
        if (field == "x") return r.x;
        if (field == "y") return r.y;
        if (field == "w") return r.width;
        if (field == "h") return r.height;
        return 0.0;
    });
    Reg<void(LS *, double)>(s, host, "ui_disabled_begin", [ui_sys](LS *, double on) {
        if (auto *u = ui_sys()) u->DisabledBegin(on != 0.0);
    });
    Reg<void(LS *)>(s, host, "ui_disabled_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->DisabledEnd();
    });
    Reg<bool(LS *, std::string, double, std::string, double)>(
        s, host, "ui_button", [ui_sys](LS *, std::string key, double h, std::string text, double scale) -> bool {
            auto *u = ui_sys();
            return u && u->Button(key, static_cast<float>(h), text, static_cast<float>(scale));
        });
    Reg<bool(LS *, std::string)>(s, host, "ui_hovered", [ui_sys](LS *, std::string key) -> bool {
        auto *u = ui_sys();
        return u && u->IsHovered(key);
    });
    Reg<bool(LS *, std::string, double, std::string, double, bool, std::string)>(
        s, host, "ui_button_ex",
        [host, ui_sys](LS *, std::string key, double h, std::string text, double scale,
                       bool active, std::string style) -> bool {
            auto *u = ui_sys();
            if (!u) return false;
            const auto prev_btn = u->ThemeColor("button");
            const auto prev_hov = u->ThemeColor("hover");
            const auto prev_prs = u->ThemeColor("pressed");
            const auto prev_brd = u->ThemeColor("border");
            const auto prev_txt = u->ThemeColor("text");

            const auto it = host->button_styles.find(style);
            if (it != host->button_styles.end()) {
                const auto &preset = it->second;
                const auto &st = (active && preset.has_active_variant) ? preset.active : preset.normal;
                u->SetThemeColor("button", st.button);
                u->SetThemeColor("hover", st.hover);
                u->SetThemeColor("pressed", st.pressed);
                u->SetThemeColor("border", st.border);
                u->SetThemeColor("text", st.text);
            }
            const bool clicked = u->Button(key, static_cast<float>(h), text, static_cast<float>(scale));
            u->SetThemeColor("button", prev_btn);
            u->SetThemeColor("hover", prev_hov);
            u->SetThemeColor("pressed", prev_prs);
            u->SetThemeColor("border", prev_brd);
            u->SetThemeColor("text", prev_txt);
            return clicked;
        });
    Reg<void(LS *, double, double, std::int64_t)>(
        s, host, "ui_bar", [ui_sys, col_of](LS *, double h, double v, std::int64_t c) {
            if (auto *u = ui_sys()) {
                u->ProgressBar(static_cast<float>(h), static_cast<float>(v),
                               c > 0 ? col_of(c) : Color{0.80f, 0.66f, 0.38f, 1.0f});
            }
        });
    Reg<double(LS *, std::string, double, double, double, double)>(
        s, host, "ui_slider", [ui_sys](LS *, std::string key, double h, double v, double lo, double hi) -> double {
            auto *u = ui_sys();
            return u ? u->Slider(key, static_cast<float>(h), static_cast<float>(v),
                                 static_cast<float>(lo), static_cast<float>(hi))
                     : v;
        });
    Reg<bool(LS *, std::string, double, bool, std::string, double)>(
        s, host, "ui_checkbox",
        [ui_sys](LS *, std::string key, double h, bool v, std::string label, double scale) -> bool {
            auto *u = ui_sys();
            return u ? u->Checkbox(key, static_cast<float>(h), v, label, static_cast<float>(scale)) : v;
        });
    Reg<bool(LS *, std::string, double, double)>(
        s, host, "ui_text_input", [ui_sys](LS *, std::string key, double h, double max_c) -> bool {
            auto *u = ui_sys();
            const std::size_t n = max_c > 0 ? static_cast<std::size_t>(max_c) : std::size_t{24};
            return u && u->TextInput(key, static_cast<float>(h), n);
        });
    Reg<std::string(LS *, std::string)>(s, host, "ui_text_get", [ui_sys](LS *, std::string key) -> std::string {
        auto *u = ui_sys();
        return u ? std::string(u->TextValue(key)) : std::string{};
    });
    Reg<void(LS *, std::string, std::string)>(
        s, host, "ui_text_set", [ui_sys](LS *, std::string key, std::string text) {
            if (auto *u = ui_sys()) u->SetTextValue(key, text);
        });
}

} // namespace fake2d
