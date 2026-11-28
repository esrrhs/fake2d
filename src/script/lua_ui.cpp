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

inline bool ArgBool(LS *s, CVar *a, int n, int i) {
    return fakelua::inter::FakeluaToNativeBool(s, fakelua::inter::GetNativeArg(s, a, n, i));
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

inline bool IsStringArg(LS *s, CVar *a, int n, int i) {
    if (i >= n) return false;
    const CVar v = fakelua::inter::GetNativeArg(s, a, n, i);
    return v.type_ == 4 || v.type_ == 5; // VarType::String or VarType::StringId
}

UISize ParseSizeSpec(LS *s, CVar *a, int n, int i, UISize fallback = UISize::Auto()) {
    if (i >= n) return fallback;
    if (IsStringArg(s, a, n, i)) {
        const std::string str = ArgStr(s, a, n, i);
        if (str.empty() || str == "auto" || str == "hug") return UISize::Auto();
        if (str == "grow" || str == "fill") return UISize::Grow(1.0f);
        if (str.rfind("grow:", 0) == 0) {
            const float w = static_cast<float>(std::atof(str.substr(5).c_str()));
            return UISize::Grow(w > 0.0f ? w : 1.0f);
        }
        if (str.back() == '%') {
            const float p = static_cast<float>(std::atof(str.substr(0, str.size() - 1).c_str()));
            return UISize::Percent(p * 0.01f);
        }
        const float px = static_cast<float>(std::atof(str.c_str()));
        return px > 0.0f ? UISize::Fixed(px) : UISize::Auto();
    }
    const double v = ArgNum(s, a, n, i);
    if (v < 0.0) return UISize::Grow(static_cast<float>(-v));
    if (v == 0.0) return UISize::Auto();
    return UISize::Fixed(static_cast<float>(v));
}

UIAxisAlign ParseAxisAlign(const std::string &str, UIAxisAlign fallback) {
    if (str == "start" || str == "left" || str == "top") return UIAxisAlign::Start;
    if (str == "center") return UIAxisAlign::Center;
    if (str == "end" || str == "right" || str == "bottom") return UIAxisAlign::End;
    if (str == "stretch" || str == "fill") return UIAxisAlign::Stretch;
    if (str == "baseline") return UIAxisAlign::Baseline;
    if (str == "space_between" || str == "between") return UIAxisAlign::SpaceBetween;
    if (str == "space_around" || str == "around") return UIAxisAlign::SpaceAround;
    if (str == "space_evenly" || str == "evenly") return UIAxisAlign::SpaceEvenly;
    return fallback;
}

UIScrollDir ParseScrollDir(const std::string &str) {
    if (str == "h" || str == "horizontal") return UIScrollDir::Horizontal;
    if (str == "both" || str == "hv" || str == "xy") return UIScrollDir::Both;
    return UIScrollDir::Vertical;
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
// Color, string formatting, and UTF-8 utilities (u_*)
// ---------------------------------------------------------------------------

void RegisterCommonUILuaApi(LS *s, LuaUIHost *host) {
    if (!s || !host) {
        return;
    }
    auto col_of = [host](std::int64_t id) -> Color {
        return host->colors ? host->colors->Get(id, host->default_text_color) : host->default_text_color;
    };
    auto col_id = [host](const Color &c) -> std::int64_t {
        return host->colors ? host->colors->Id(c) : 0;
    };

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
}

// ---------------------------------------------------------------------------
// Unified UISystem Lua bindings (ui_*)
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
    auto rc = [](double x, double y, double w, double h) -> Rect {
        return Rect{static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)};
    };

    // Input blocking, modal/suppress, hit-testing, and screen-space draw on UISystem
    Reg<void(LS *, double, double, double, double)>(
        s, host, "ui_block", [host, ui_sys, rc](LS *, double x, double y, double w, double h) {
            auto *u = ui_sys();
            if (!u) return;
            const Rect r = rc(x, y, w, h);
            u->BlockInput(r);
            if (!u->IsSuppressed() && host->tips) {
                host->tips->RecordOccluder(r);
            }
        });
    Reg<void(LS *, std::int64_t)>(s, host, "ui_set_modal", [host, ui_sys](LS *, std::int64_t m) {
        auto *u = ui_sys();
        if (!u) return;
        u->SetModal(m != 0);
        if (m != 0 && host->tips) {
            host->tips->RecordOccluder(Rect{0.0f, 0.0f, u->LogicalWidth(), u->LogicalHeight()});
        }
    });
    Reg<void(LS *, std::int64_t)>(s, host, "ui_set_suppress", [ui_sys](LS *, std::int64_t v) {
        if (auto *u = ui_sys()) u->SetSuppress(v != 0);
    });
    Reg<bool(LS *, double, double, double, double)>(
        s, host, "ui_hovered_rect", [ui_sys, rc](LS *, double x, double y, double w, double h) -> bool {
            auto *u = ui_sys();
            return u && u->HoveredRect(rc(x, y, w, h));
        });
    RegVar(s, host, "ui_invisible", 5, [host, ui_sys, rc](LS *st, CVar *a, int n) -> bool {
        auto *u = ui_sys();
        if (!u) return false;
        const std::string key = ArgStr(st, a, n, 0);
        const Rect r = rc(ArgNum(st, a, n, 1), ArgNum(st, a, n, 2), ArgNum(st, a, n, 3), ArgNum(st, a, n, 4));
        const std::string tip = n > 5 ? ArgStr(st, a, n, 5) : std::string{};
        const bool clk = u->InvisibleButton(key, r);
        if (!tip.empty() && host->tips && u->HoveredRect(r)) {
            host->tips->RecordEntry(r, tip);
        }
        return clk;
    });
    Reg<void(LS *, double, double, double, double, std::int64_t)>(
        s, host, "ui_quad", [ui_sys, rc, col_of](LS *, double x, double y, double w, double h, std::int64_t c) {
            if (auto *u = ui_sys()) u->DrawQuadAt(rc(x, y, w, h), col_of(c));
        });
    RegVar(s, host, "ui_frame_screen", 5, [ui_sys, rc, col_of](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            const float th = n > 5 ? static_cast<float>(ArgNum(st, a, n, 5)) : 1.0f;
            u->DrawFrameAt(rc(ArgNum(st, a, n, 0), ArgNum(st, a, n, 1),
                              ArgNum(st, a, n, 2), ArgNum(st, a, n, 3)),
                           col_of(static_cast<std::int64_t>(ArgNum(st, a, n, 4))), th);
        }
        return 0.0;
    });
    RegVar(s, host, "ui_rquad", 6, [ui_sys, rc, col_of](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            const float rad = static_cast<float>(ArgNum(st, a, n, 4));
            const Color fill = col_of(static_cast<std::int64_t>(ArgNum(st, a, n, 5)));
            const Color brd = n > 6 && ArgNum(st, a, n, 6) > 0
                                  ? col_of(static_cast<std::int64_t>(ArgNum(st, a, n, 6)))
                                  : Color::Clear();
            const float bw = n > 7 ? static_cast<float>(ArgNum(st, a, n, 7)) : (brd.a > 0.0f ? 1.0f : 0.0f);
            u->DrawRoundedQuadAt(rc(ArgNum(st, a, n, 0), ArgNum(st, a, n, 1),
                                    ArgNum(st, a, n, 2), ArgNum(st, a, n, 3)),
                                 rad, fill, brd, bw);
        }
        return 0.0;
    });
    RegVar(s, host, "ui_rgrad", 7, [ui_sys, rc, col_of](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            u->DrawRoundedGradientAt(
                rc(ArgNum(st, a, n, 0), ArgNum(st, a, n, 1), ArgNum(st, a, n, 2), ArgNum(st, a, n, 3)),
                static_cast<float>(ArgNum(st, a, n, 4)),
                col_of(static_cast<std::int64_t>(ArgNum(st, a, n, 5))),
                col_of(static_cast<std::int64_t>(ArgNum(st, a, n, 6))),
                n > 7 && ArgBool(st, a, n, 7));
        }
        return 0.0;
    });
    Reg<void(LS *, double, double, double, double, double, double, std::int64_t)>(
        s, host, "ui_rframe",
        [ui_sys, rc, col_of](LS *, double x, double y, double w, double h, double r, double th, std::int64_t cid) {
            if (auto *u = ui_sys()) {
                u->DrawRoundedFrameAt(rc(x, y, w, h), static_cast<float>(r), static_cast<float>(th), col_of(cid));
            }
        });
    RegVar(s, host, "ui_rshadow", 5, [ui_sys, rc](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            const float r = static_cast<float>(ArgNum(st, a, n, 4));
            const float oy = n > 5 ? static_cast<float>(ArgNum(st, a, n, 5)) : 4.0f;
            const float sp = n > 6 ? static_cast<float>(ArgNum(st, a, n, 6)) : 8.0f;
            const float al = n > 7 ? static_cast<float>(ArgNum(st, a, n, 7)) : 0.55f;
            u->DrawDropShadowAt(
                rc(ArgNum(st, a, n, 0), ArgNum(st, a, n, 1), ArgNum(st, a, n, 2), ArgNum(st, a, n, 3)),
                r, oy, sp, al);
        }
        return 0.0;
    });
    Reg<double(LS *, double, double, std::string, double, std::int64_t, std::int64_t, std::int64_t)>(
        s, host, "ui_text_at",
        [ui_sys, col_of](LS *, double x, double y, std::string t, double px, std::int64_t c,
                         std::int64_t align, std::int64_t face) -> double {
            auto *u = ui_sys();
            return u ? u->DrawTextPx(static_cast<float>(x), static_cast<float>(y), t,
                                     static_cast<float>(px), col_of(c),
                                     static_cast<int>(align), static_cast<int>(face))
                     : 0.0;
        });
    Reg<double(LS *, double, double, double, std::string, double, std::int64_t, std::int64_t)>(
        s, host, "ui_wrapped_at",
        [ui_sys, col_of](LS *, double x, double y, double w, std::string t, double px,
                         std::int64_t c, std::int64_t face) -> double {
            auto *u = ui_sys();
            return u ? u->DrawWrappedTextPx(static_cast<float>(x), static_cast<float>(y),
                                            static_cast<float>(w), t, static_cast<float>(px),
                                            col_of(c), static_cast<int>(face))
                     : 0.0;
        });
    Reg<double(LS *, std::string, double, std::int64_t)>(
        s, host, "ui_measure", [ui_sys](LS *, std::string t, double px, std::int64_t face) -> double {
            auto *u = ui_sys();
            return u ? u->MeasureTextPx(t, static_cast<float>(px), static_cast<int>(face)) : 0.0;
        });
    Reg<double(LS *, std::string, double, double, std::int64_t)>(
        s, host, "ui_measure_wrapped",
        [ui_sys](LS *, std::string t, double px, double w, std::int64_t face) -> double {
            auto *u = ui_sys();
            return u ? u->MeasureWrappedTextPx(t, static_cast<float>(px),
                                               static_cast<float>(w), static_cast<int>(face))
                     : 0.0;
        });
    Reg<void(LS *, std::string)>(
        s, host, "ui_tooltip_set", [host](LS *, std::string text) {
            if (host->tips) host->tips->SetFallbackTip(std::move(text));
        });

    // Two-Pass layout & UISystem widgets
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
    RegVar(s, host, "ui_constrain", 2, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            const float min_w = static_cast<float>(ArgNum(st, a, n, 0));
            const float max_w = static_cast<float>(ArgNum(st, a, n, 1));
            const float min_h = n > 2 ? static_cast<float>(ArgNum(st, a, n, 2)) : 0.0f;
            const float max_h = n > 3 ? static_cast<float>(ArgNum(st, a, n, 3)) : 1e9f;
            u->Constrain(min_w, max_w, min_h, max_h);
        }
        return 0.0;
    });
    RegVar(s, host, "ui_align_begin", 1, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            const std::string key = ArgStr(st, a, n, 0);
            const std::string rules = n > 1 ? ArgStr(st, a, n, 1) : std::string("top|height|no_overlap");
            u->BeginAlignGroup(key, rules);
        }
        return 0.0;
    });
    Reg<void(LS *)>(s, host, "ui_align_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->EndAlignGroup();
    });
    auto ensure_tip_recorder = [host](UISystem *u) {
        if (!u || !host->tips) return;
        u->SetTipRecorder([host](const Rect &clipped_rect, std::string_view text, bool is_occluder) {
            if (!host->tips) return;
            if (is_occluder) {
                host->tips->RecordOccluder(clipped_rect);
            } else if (!text.empty()) {
                host->tips->RecordEntry(clipped_rect, std::string(text));
            }
        });
    };

    Reg<double(LS *)>(s, host, "ui_scroll_end", [ui_sys](LS *) -> double {
        if (auto *u = ui_sys()) {
            u->ScrollBoxEnd();
        }
        return 0.0;
    });
    Reg<double(LS *, std::string)>(s, host, "ui_token", [ui_sys](LS *, std::string name) -> double {
        auto *u = ui_sys();
        return u ? u->LayoutToken(name) : 0.0;
    });
    Reg<bool(LS *, std::string, double)>(
        s, host, "ui_set_token", [ui_sys](LS *, std::string name, double value) -> bool {
            auto *u = ui_sys();
            return u && u->SetLayoutToken(name, static_cast<float>(value));
        });
    Reg<void(LS *, std::string)>(s, host, "ui_tip", [ui_sys, ensure_tip_recorder](LS *, std::string t) {
        auto *u = ui_sys();
        if (!u || t.empty() || u->IsSuppressed() || !u->InAutoLayout()) return;
        ensure_tip_recorder(u);
        u->Tip(t);
    });
    Reg<void(LS *, double, double, double, double, std::string)>(
        s, host, "ui_tip_rect", [host, ui_sys](LS *, double x, double y, double w, double h, std::string t) {
            auto *u = ui_sys();
            if (t.empty() || !host->tips || (u && u->IsSuppressed())) return;
            host->tips->RecordEntry(
                Rect{static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)},
                std::move(t));
        });
    Reg<void(LS *, double, double, double, double)>(
        s, host, "ui_tip_occlude", [host, ui_sys](LS *, double x, double y, double w, double h) {
            auto *u = ui_sys();
            if (!host->tips || (u && u->IsSuppressed())) return;
            host->tips->RecordOccluder(
                Rect{static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)});
        });
    Reg<std::string(LS *)>(s, host, "ui_tip_poll", [host, ui_sys](LS *) -> std::string {
        if (!host->tips) return {};
        auto *u = ui_sys();
        const Vec2 m = u ? u->LogicalMouse() : Vec2{-10000.0f, -10000.0f};
        return host->tips->Poll(m);
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
    RegVar(s, host, "ui_img_box", 1, [ui_sys, col_of, load_tex](LS *st, CVar *a, int n) -> double {
        auto *u = ui_sys();
        if (!u || !u->InAutoLayout()) return 0.0;
        const std::string name = ArgStr(st, a, n, 0);
        const float h = n > 1 ? static_cast<float>(ArgNum(st, a, n, 1)) : 0.0f;
        const auto tint = n > 2 && ArgNum(st, a, n, 2) > 0
                              ? col_of(static_cast<std::int64_t>(ArgNum(st, a, n, 2)))
                              : Color::White();
        if (const auto *tex = load_tex(name)) {
            const Rect src{0.0f, 0.0f, static_cast<float>(tex->Width()), static_cast<float>(tex->Height())};
            if (h > 0.0f) {
                u->NextHeight(UISize::Fixed(h));
            }
            u->Image(tex, src, tint);
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
    Reg<void(LS *, std::string)>(s, host, "ui_remember", [ui_sys](LS *, std::string name) {
        if (auto *u = ui_sys()) u->Remember(name);
    });
    Reg<void(LS *, std::string)>(s, host, "ui_remember_box", [ui_sys](LS *, std::string name) {
        if (auto *u = ui_sys()) u->RememberBox(name);
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
    Reg<double(LS *, std::string, std::string)>(s, host, "ui_widget_rect", [ui_sys](LS *, std::string key, std::string field) -> double {
        auto *u = ui_sys();
        if (!u) return 0.0;
        const auto r = u->WidgetRect(key);
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
    auto run_styled_button = [host](UISystem *u, const std::string &key, const std::string &text,
                                    float scale, bool active, const std::string &style) -> bool {
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
        const bool clicked = u->Button(key, text, scale);
        u->SetThemeColor("button", prev_btn);
        u->SetThemeColor("hover", prev_hov);
        u->SetThemeColor("pressed", prev_prs);
        u->SetThemeColor("border", prev_brd);
        u->SetThemeColor("text", prev_txt);
        return clicked;
    };

    RegVar(s, host, "ui_button", 2, [ui_sys](LS *st, CVar *a, int n) -> bool {
        auto *u = ui_sys();
        if (!u) return false;
        const std::string key = ArgStr(st, a, n, 0);
        const std::string text = ArgStr(st, a, n, 1);
        const float scale = n > 2 ? static_cast<float>(ArgNum(st, a, n, 2)) : 0.0f;
        return u->Button(key, text, scale);
    });
    Reg<bool(LS *, std::string)>(s, host, "ui_hovered", [ui_sys](LS *, std::string key) -> bool {
        auto *u = ui_sys();
        return u && u->IsHovered(key);
    });
    RegVar(s, host, "ui_button_ex", 2, [ui_sys, run_styled_button](LS *st, CVar *a, int n) -> bool {
        auto *u = ui_sys();
        if (!u) return false;
        const std::string key = ArgStr(st, a, n, 0);
        const std::string text = ArgStr(st, a, n, 1);
        const float scale = n > 2 ? static_cast<float>(ArgNum(st, a, n, 2)) : 0.0f;
        const bool active = n > 3 ? ArgBool(st, a, n, 3) : false;
        const std::string style = n > 4 ? ArgStr(st, a, n, 4) : std::string{};
        return run_styled_button(u, key, text, scale, active, style);
    });
    RegVar(s, host, "ui_bar", 1, [ui_sys, col_of](LS *st, CVar *a, int n) -> double {
        auto *u = ui_sys();
        if (!u) return 0.0;
        const Color def_col{0.80f, 0.66f, 0.38f, 1.0f};
        const float v = static_cast<float>(ArgNum(st, a, n, 0));
        const std::int64_t cid = n > 1 ? static_cast<std::int64_t>(ArgNum(st, a, n, 1)) : 0;
        u->ProgressBar(v, cid > 0 ? col_of(cid) : def_col);
        return 0.0;
    });
    Reg<double(LS *, std::string, double, double, double)>(
        s, host, "ui_slider", [ui_sys](LS *, std::string key, double v, double min_v, double max_v) -> double {
            auto *u = ui_sys();
            return u ? u->Slider(key, static_cast<float>(v), static_cast<float>(min_v), static_cast<float>(max_v))
                     : v;
        });
    RegVar(s, host, "ui_checkbox", 2, [ui_sys](LS *st, CVar *a, int n) -> bool {
        auto *u = ui_sys();
        const bool v = ArgBool(st, a, n, 1);
        if (!u) return v;
        const std::string key = ArgStr(st, a, n, 0);
        const std::string lbl = n > 2 ? ArgStr(st, a, n, 2) : std::string{};
        const float sc = n > 3 ? static_cast<float>(ArgNum(st, a, n, 3)) : 0.0f;
        return u->Checkbox(key, v, lbl, sc);
    });
    RegVar(s, host, "ui_text_input", 1, [ui_sys](LS *st, CVar *a, int n) -> bool {
        auto *u = ui_sys();
        if (!u) return false;
        const std::string key = ArgStr(st, a, n, 0);
        const std::size_t mc = (n > 1 && ArgNum(st, a, n, 1) > 0)
                                   ? static_cast<std::size_t>(ArgNum(st, a, n, 1))
                                   : std::size_t{24};
        return u->TextInput(key, mc);
    });
    Reg<std::string(LS *, std::string)>(s, host, "ui_text_get", [ui_sys](LS *, std::string key) -> std::string {
        auto *u = ui_sys();
        return u ? std::string(u->TextValue(key)) : std::string{};
    });
    Reg<void(LS *, std::string, std::string)>(
        s, host, "ui_text_set", [ui_sys](LS *, std::string key, std::string text) {
            if (auto *u = ui_sys()) u->SetTextValue(key, text);
        });

    // Two-Pass Auto-Layout API (zero hardcoded widget/column sizes)
    RegVar(s, host, "ui_root_begin", 4, [ui_sys, ensure_tip_recorder](LS *st, CVar *a, int n) -> double {
        auto *u = ui_sys();
        if (!u) return 0.0;
        ensure_tip_recorder(u);
        const float x = static_cast<float>(ArgNum(st, a, n, 0));
        const float y = static_cast<float>(ArgNum(st, a, n, 1));
        const UISize w = ParseSizeSpec(st, a, n, 2, UISize::Auto());
        const UISize h = ParseSizeSpec(st, a, n, 3, UISize::Auto());
        const float pad = n > 4 ? static_cast<float>(ArgNum(st, a, n, 4)) : 0.0f;
        const bool block = n > 5 ? ArgBool(st, a, n, 5) : true;
        u->RootBegin(x, y, w, h, pad, block);
        return 0.0;
    });
    RegVar(s, host, "ui_root_anchor", 4, [ui_sys, ensure_tip_recorder](LS *st, CVar *a, int n) -> double {
        auto *u = ui_sys();
        if (!u) return 0.0;
        ensure_tip_recorder(u);
        const std::string ha = ArgStr(st, a, n, 0);
        const std::string va = ArgStr(st, a, n, 1);
        const UISize w = ParseSizeSpec(st, a, n, 2, UISize::Auto());
        const UISize h = ParseSizeSpec(st, a, n, 3, UISize::Auto());
        const float ix = n > 4 ? static_cast<float>(ArgNum(st, a, n, 4)) : 0.0f;
        const float iy = n > 5 ? static_cast<float>(ArgNum(st, a, n, 5)) : 0.0f;
        const float pad = n > 6 ? static_cast<float>(ArgNum(st, a, n, 6)) : 0.0f;
        const bool ov = n > 7 ? ArgBool(st, a, n, 7) : true;
        const bool blk = n > 8 ? ArgBool(st, a, n, 8) : true;
        u->RootAnchorBegin(ha, va, w, h, ix, iy, pad, ov, blk);
        return 0.0;
    });
    Reg<void(LS *)>(s, host, "ui_root_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->RootEnd();
    });
    RegVar(s, host, "ui_vstack", 0, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            const float gap = n > 0 ? static_cast<float>(ArgNum(st, a, n, 0)) : -1.0f;
            const float pad = n > 1 ? static_cast<float>(ArgNum(st, a, n, 1)) : 0.0f;
            const UIAxisAlign ca = n > 2 ? ParseAxisAlign(ArgStr(st, a, n, 2), UIAxisAlign::Stretch) : UIAxisAlign::Stretch;
            u->VStackBegin(gap, pad, ca);
        }
        return 0.0;
    });
    Reg<void(LS *)>(s, host, "ui_vstack_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->StackEnd();
    });
    RegVar(s, host, "ui_hstack", 0, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            const float gap = n > 0 ? static_cast<float>(ArgNum(st, a, n, 0)) : -1.0f;
            const float pad = n > 1 ? static_cast<float>(ArgNum(st, a, n, 1)) : 0.0f;
            const UIAxisAlign ca = n > 2 ? ParseAxisAlign(ArgStr(st, a, n, 2), UIAxisAlign::Center) : UIAxisAlign::Center;
            u->HStackBegin(gap, pad, ca);
        }
        return 0.0;
    });
    Reg<void(LS *)>(s, host, "ui_hstack_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->StackEnd();
    });
    RegVar(s, host, "ui_card", 0, [ui_sys, col_of](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            const float pad = n > 0 ? static_cast<float>(ArgNum(st, a, n, 0)) : -1.0f;
            const float gap = n > 1 ? static_cast<float>(ArgNum(st, a, n, 1)) : -1.0f;
            if (n > 2 && ArgNum(st, a, n, 2) > 0) {
                const Color fill = col_of(static_cast<std::int64_t>(ArgNum(st, a, n, 2)));
                const Color brd = (n > 3 && ArgNum(st, a, n, 3) > 0)
                                      ? col_of(static_cast<std::int64_t>(ArgNum(st, a, n, 3)))
                                      : u->ThemeColor("border");
                u->CardBegin(fill, brd, pad, gap);
            } else {
                u->CardBegin(pad, gap);
            }
        }
        return 0.0;
    });
    Reg<void(LS *)>(s, host, "ui_card_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->CardEnd();
    });
    RegVar(s, host, "ui_table_begin", 3, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            const std::string key = ArgStr(st, a, n, 0);
            const float cg = static_cast<float>(ArgNum(st, a, n, 1));
            const float rg = static_cast<float>(ArgNum(st, a, n, 2));
            std::vector<UISize> cols;
            for (int i = 3; i < n; ++i) {
                cols.push_back(ParseSizeSpec(st, a, n, i, UISize::Auto()));
            }
            u->TableBegin(key, cols, cg, rg);
        }
        return 0.0;
    });
    Reg<void(LS *, double, double, double)>(
        s, host, "ui_table_col_limits", [ui_sys](LS *, double col, double min_w, double max_w) {
            if (auto *u = ui_sys(); u && col >= 0) {
                u->TableColLimits(static_cast<std::size_t>(col), static_cast<float>(min_w),
                                  static_cast<float>(max_w));
            }
        });
    RegVar(s, host, "ui_table_row", 0, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            const UIAxisAlign ca = n > 0 ? ParseAxisAlign(ArgStr(st, a, n, 0), UIAxisAlign::Center) : UIAxisAlign::Center;
            u->TableRowBegin(ca);
        }
        return 0.0;
    });
    Reg<void(LS *)>(s, host, "ui_table_row_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->TableRowEnd();
    });
    RegVar(s, host, "ui_cell", 0, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            const float pad = n > 0 ? static_cast<float>(ArgNum(st, a, n, 0)) : 0.0f;
            u->TableCellBegin(pad);
        }
        return 0.0;
    });
    Reg<void(LS *)>(s, host, "ui_cell_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->TableCellEnd();
    });
    Reg<void(LS *)>(s, host, "ui_table_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->TableEnd();
    });
    RegVar(s, host, "ui_scroll", 1, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            const std::string key = ArgStr(st, a, n, 0);
            const UISize vh = n > 1 ? ParseSizeSpec(st, a, n, 1, UISize::Grow()) : UISize::Grow();
            u->ScrollBoxBegin(key, vh);
        }
        return 0.0;
    });
    RegVar(s, host, "ui_flow", 0, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            const float cg = n > 0 ? static_cast<float>(ArgNum(st, a, n, 0)) : -1.0f;
            const float rg = n > 1 ? static_cast<float>(ArgNum(st, a, n, 1)) : -1.0f;
            u->FlowBoxBegin(cg, rg);
        }
        return 0.0;
    });
    Reg<void(LS *)>(s, host, "ui_flow_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->FlowBoxEnd();
    });
    RegVar(s, host, "ui_canvas", 0, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            const UISize w = n > 0 ? ParseSizeSpec(st, a, n, 0, UISize::Auto()) : UISize::Auto();
            const UISize h = n > 1 ? ParseSizeSpec(st, a, n, 1, UISize::Auto()) : UISize::Auto();
            const float pad = n > 2 ? static_cast<float>(ArgNum(st, a, n, 2)) : 0.0f;
            u->CanvasBegin(w, h, pad);
        }
        return 0.0;
    });
    Reg<void(LS *)>(s, host, "ui_canvas_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->CanvasEnd();
    });
    RegVar(s, host, "ui_divider", 0, [ui_sys, col_of](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            const Color c = (n > 0 && ArgNum(st, a, n, 0) > 0)
                                ? col_of(static_cast<std::int64_t>(ArgNum(st, a, n, 0)))
                                : u->ThemeColor("border");
            const float th = n > 1 ? static_cast<float>(ArgNum(st, a, n, 1)) : 1.0f;
            u->Divider(c, th);
        }
        return 0.0;
    });
    RegVar(s, host, "ui_spacer", 0, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            u->Spacer(n > 0 ? static_cast<float>(ArgNum(st, a, n, 0)) : -1.0f);
        }
        return 0.0;
    });
    RegVar(s, host, "ui_next_size", 2, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            u->NextSize(ParseSizeSpec(st, a, n, 0, UISize::Auto()),
                        ParseSizeSpec(st, a, n, 1, UISize::Auto()));
        }
        return 0.0;
    });
    RegVar(s, host, "ui_next_width", 1, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) u->NextWidth(ParseSizeSpec(st, a, n, 0, UISize::Auto()));
        return 0.0;
    });
    RegVar(s, host, "ui_next_height", 1, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) u->NextHeight(ParseSizeSpec(st, a, n, 0, UISize::Auto()));
        return 0.0;
    });
    RegVar(s, host, "ui_next_grow", 0, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) u->NextGrow(n > 0 ? static_cast<float>(ArgNum(st, a, n, 0)) : 1.0f);
        return 0.0;
    });
    Reg<void(LS *, std::string)>(s, host, "ui_next_align", [ui_sys](LS *, std::string a) {
        if (auto *u = ui_sys()) u->NextAlign(ParseAxisAlign(a, UIAxisAlign::Center));
    });
    Reg<void(LS *, std::string)>(s, host, "ui_next_main_align", [ui_sys](LS *, std::string a) {
        if (auto *u = ui_sys()) u->NextMainAlign(ParseAxisAlign(a, UIAxisAlign::Start));
    });
    Reg<void(LS *, std::string)>(s, host, "ui_modal_open", [ui_sys](LS *, std::string k) {
        if (auto *u = ui_sys()) u->OpenModal(k);
    });
    Reg<void(LS *)>(s, host, "ui_modal_close", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->CloseModal();
    });
    RegVar(s, host, "ui_modal_begin", 1, [ui_sys, ensure_tip_recorder](LS *st, CVar *a, int n) -> bool {
        auto *u = ui_sys();
        if (!u) return false;
        ensure_tip_recorder(u);
        const std::string key = ArgStr(st, a, n, 0);
        const UISize w = n > 1 ? ParseSizeSpec(st, a, n, 1, UISize::Auto()) : UISize::Auto();
        const UISize h = n > 2 ? ParseSizeSpec(st, a, n, 2, UISize::Auto()) : UISize::Auto();
        return u->ModalBegin(key, w, h);
    });
    Reg<void(LS *)>(s, host, "ui_modal_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->ModalEnd();
    });
    Reg<void(LS *, double, double, double, double)>(
        s, host, "ui_next_pad", [ui_sys](LS *, double l, double t, double r, double b) {
            if (auto *u = ui_sys()) {
                u->NextPad(static_cast<float>(l), static_cast<float>(t),
                           static_cast<float>(r), static_cast<float>(b));
            }
        });
    RegVar(s, host, "ui_next_shrink", 0, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) u->NextShrink(n > 0 ? static_cast<float>(ArgNum(st, a, n, 0)) : 1.0f);
        return 0.0;
    });
    Reg<void(LS *, double)>(s, host, "ui_next_aspect", [ui_sys](LS *, double r) {
        if (auto *u = ui_sys()) u->NextAspect(static_cast<float>(r));
    });
    Reg<void(LS *, std::int64_t)>(s, host, "ui_next_span", [ui_sys](LS *, std::int64_t cols) {
        if (auto *u = ui_sys()) u->NextSpan(static_cast<int>(cols));
    });
    RegVar(s, host, "ui_next_margin", 1, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            const float l = static_cast<float>(ArgNum(st, a, n, 0));
            const float t = n > 1 ? static_cast<float>(ArgNum(st, a, n, 1)) : l;
            const float r = n > 2 ? static_cast<float>(ArgNum(st, a, n, 2)) : l;
            const float b = n > 3 ? static_cast<float>(ArgNum(st, a, n, 3)) : t;
            u->NextMargin(l, t, r, b);
        }
        return 0.0;
    });
    RegVar(s, host, "ui_next_bg", 1, [ui_sys, col_of](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            const Color c = col_of(static_cast<std::int64_t>(ArgNum(st, a, n, 0)));
            const float rad = n > 1 ? static_cast<float>(ArgNum(st, a, n, 1)) : 0.0f;
            u->NextBg(c, rad);
        }
        return 0.0;
    });
    RegVar(s, host, "ui_next_border", 1, [ui_sys, col_of](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            const Color c = col_of(static_cast<std::int64_t>(ArgNum(st, a, n, 0)));
            const float w = n > 1 ? static_cast<float>(ArgNum(st, a, n, 1)) : 1.0f;
            u->NextBorder(c, w);
        }
        return 0.0;
    });
    Reg<void(LS *, double)>(s, host, "ui_next_radius", [ui_sys](LS *, double r) {
        if (auto *u = ui_sys()) u->NextRadius(static_cast<float>(r));
    });
    RegVar(s, host, "ui_next_shadow", 0, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            const float oy = n > 0 ? static_cast<float>(ArgNum(st, a, n, 0)) : 4.0f;
            const float sp = n > 1 ? static_cast<float>(ArgNum(st, a, n, 1)) : 6.0f;
            const float al = n > 2 ? static_cast<float>(ArgNum(st, a, n, 2)) : 0.35f;
            u->NextShadow(oy, sp, al);
        }
        return 0.0;
    });
    Reg<void(LS *, double)>(s, host, "ui_next_alpha", [ui_sys](LS *, double a) {
        if (auto *u = ui_sys()) u->NextAlpha(static_cast<float>(a));
    });
    RegVar(s, host, "ui_next_anim", 1, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            u->NextTransition(ArgStr(st, a, n, 0),
                              n > 1 ? static_cast<float>(ArgNum(st, a, n, 1)) : 0.18f);
        }
        return 0.0;
    });
    Reg<void(LS *, std::int64_t)>(s, host, "ui_next_font", [ui_sys](LS *, std::int64_t face) {
        if (auto *u = ui_sys()) u->NextFontFace(static_cast<int>(face));
    });
    Reg<void(LS *, std::int64_t)>(s, host, "ui_next_text_align", [ui_sys](LS *, std::int64_t align) {
        if (auto *u = ui_sys()) u->NextTextAlign(static_cast<int>(align));
    });
    Reg<void(LS *, std::int64_t)>(s, host, "ui_next_bevel", [ui_sys, col_of](LS *, std::int64_t cid) {
        if (auto *u = ui_sys()) u->NextInnerHighlight(col_of(cid));
    });
    RegVar(s, host, "ui_next_inner_frame", 1, [ui_sys, col_of](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            const Color c = col_of(static_cast<std::int64_t>(ArgNum(st, a, n, 0)));
            const float ins = n > 1 ? static_cast<float>(ArgNum(st, a, n, 1)) : 3.0f;
            const float w = n > 2 ? static_cast<float>(ArgNum(st, a, n, 2)) : 1.0f;
            u->NextInnerFrame(c, ins, w);
        }
        return 0.0;
    });
    RegVar(s, host, "ui_next_corners", 1, [ui_sys, col_of](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            const Color c = col_of(static_cast<std::int64_t>(ArgNum(st, a, n, 0)));
            const float sz = n > 1 ? static_cast<float>(ArgNum(st, a, n, 1)) : 14.0f;
            u->NextCornerOrnaments(c, sz);
        }
        return 0.0;
    });
    RegVar(s, host, "ui_next_left_accent", 1, [ui_sys, col_of](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            const Color c = col_of(static_cast<std::int64_t>(ArgNum(st, a, n, 0)));
            const float w = n > 1 ? static_cast<float>(ArgNum(st, a, n, 1)) : 3.0f;
            u->NextLeftAccent(c, w);
        }
        return 0.0;
    });
    RegVar(s, host, "ui_zstack", 0, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) u->ZStackBegin(n > 0 ? static_cast<float>(ArgNum(st, a, n, 0)) : 0.0f);
        return 0.0;
    });
    Reg<void(LS *)>(s, host, "ui_zstack_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->ZStackEnd();
    });
    RegVar(s, host, "ui_overlay", 0, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            u->OverlayBegin(n > 0 ? ArgStr(st, a, n, 0) : "right",
                            n > 1 ? ArgStr(st, a, n, 1) : "top",
                            n > 2 ? static_cast<float>(ArgNum(st, a, n, 2)) : 0.0f,
                            n > 3 ? static_cast<float>(ArgNum(st, a, n, 3)) : 0.0f);
        }
        return 0.0;
    });
    Reg<void(LS *)>(s, host, "ui_overlay_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->OverlayEnd();
    });
    RegVar(s, host, "ui_anchor_to", 1, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            u->AnchorToBegin(ArgStr(st, a, n, 0),
                             n > 1 ? ArgStr(st, a, n, 1) : "bottom_start",
                             n > 2 ? static_cast<float>(ArgNum(st, a, n, 2)) : 4.0f,
                             n > 3 ? ParseSizeSpec(st, a, n, 3, UISize::Auto()) : UISize::Auto(),
                             n > 4 ? ParseSizeSpec(st, a, n, 4, UISize::Auto()) : UISize::Auto());
        }
        return 0.0;
    });
    Reg<void(LS *)>(s, host, "ui_anchor_to_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->AnchorToEnd();
    });
    RegVar(s, host, "ui_grid_begin", 1, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            u->GridBegin(static_cast<float>(ArgNum(st, a, n, 0)),
                         n > 1 ? static_cast<float>(ArgNum(st, a, n, 1)) : -1.0f,
                         n > 2 ? static_cast<float>(ArgNum(st, a, n, 2)) : -1.0f,
                         n > 3 ? static_cast<float>(ArgNum(st, a, n, 3)) : 0.0f);
        }
        return 0.0;
    });
    Reg<void(LS *)>(s, host, "ui_grid_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->GridEnd();
    });
    RegVar(s, host, "ui_scroll_xy", 1, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            u->ScrollBoxBegin(ArgStr(st, a, n, 0),
                              n > 1 ? ParseSizeSpec(st, a, n, 1, UISize::Grow()) : UISize::Grow(),
                              n > 2 ? ParseSizeSpec(st, a, n, 2, UISize::Grow()) : UISize::Grow(),
                              n > 3 ? ParseScrollDir(ArgStr(st, a, n, 3)) : UIScrollDir::Both);
        }
        return 0.0;
    });
    Reg<double(LS *, std::string)>(s, host, "ui_scroll_x", [ui_sys](LS *, std::string key) -> double {
        auto *u = ui_sys();
        return u ? u->ScrollOffsetX(key) : 0.0;
    });
    Reg<double(LS *, std::string)>(s, host, "ui_scroll_content_w", [ui_sys](LS *, std::string key) -> double {
        auto *u = ui_sys();
        return u ? u->ScrollContentWidth(key) : 0.0;
    });
    Reg<void(LS *, std::string, double)>(s, host, "ui_set_scroll_x", [ui_sys](LS *, std::string key, double off) {
        if (auto *u = ui_sys()) u->SetScrollOffsetX(key, static_cast<float>(off));
    });
    RegVar(s, host, "ui_virtual_begin", 3, [ui_sys](LS *st, CVar *a, int n) -> double {
        auto *u = ui_sys();
        if (!u) return 0.0;
        const auto vr = u->VirtualBegin(ArgStr(st, a, n, 0),
                                        static_cast<int>(ArgNum(st, a, n, 1)),
                                        static_cast<float>(ArgNum(st, a, n, 2)),
                                        n > 3 ? static_cast<int>(ArgNum(st, a, n, 3)) : 1);
        return static_cast<double>(vr.count);
    });
    Reg<double(LS *)>(s, host, "ui_virtual_first", [ui_sys](LS *) -> double {
        auto *u = ui_sys();
        return u ? static_cast<double>(u->LastVirtualRange().first) : 0.0;
    });
    Reg<void(LS *)>(s, host, "ui_virtual_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->VirtualEnd();
    });
    Reg<bool(LS *, std::string, std::int64_t)>(
        s, host, "ui_cache_begin", [ui_sys](LS *, std::string key, std::int64_t hash) -> bool {
            auto *u = ui_sys();
            return u && u->CachedSubtreeBegin(key, static_cast<std::uint64_t>(hash));
        });
    Reg<void(LS *)>(s, host, "ui_cache_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->CachedSubtreeEnd();
    });
    Reg<void(LS *, std::string)>(s, host, "ui_push_id", [ui_sys](LS *, std::string id) {
        if (auto *u = ui_sys()) u->PushID(id);
    });
    Reg<void(LS *)>(s, host, "ui_pop_id", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->PopID();
    });
    RegVar(s, host, "ui_focus_scope_begin", 1, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            u->FocusScopeBegin(ArgStr(st, a, n, 0), n <= 1 || ArgBool(st, a, n, 1));
        }
        return 0.0;
    });
    Reg<void(LS *)>(s, host, "ui_focus_scope_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->FocusScopeEnd();
    });
    RegVar(s, host, "ui_inspector", 0, [ui_sys](LS *st, CVar *a, int n) -> bool {
        auto *u = ui_sys();
        if (!u) return false;
        if (n > 0) u->SetInspector(ArgBool(st, a, n, 0));
        else u->ToggleInspector();
        return u->IsInspectorEnabled();
    });
    Reg<double(LS *, std::string)>(s, host, "ui_ime_rect", [ui_sys](LS *, std::string f) -> double {
        auto *u = ui_sys();
        if (!u) return 0.0;
        const Rect r = u->ImeCursorRect();
        return f == "x" ? r.x : f == "y" ? r.y : f == "w" ? r.width : f == "h" ? r.height : 0.0;
    });

    // Dedicated heightless auto-sized widget aliases
    RegVar(s, host, "ui_btn", 2, [ui_sys](LS *st, CVar *a, int n) -> bool {
        auto *u = ui_sys();
        if (!u) return false;
        const std::string key = ArgStr(st, a, n, 0);
        const std::string text = ArgStr(st, a, n, 1);
        const float scale = n > 2 ? static_cast<float>(ArgNum(st, a, n, 2)) : 0.0f;
        return u->Button(key, text, scale);
    });
    RegVar(s, host, "ui_btn_ex", 2, [ui_sys, run_styled_button](LS *st, CVar *a, int n) -> bool {
        auto *u = ui_sys();
        if (!u) return false;
        const std::string key = ArgStr(st, a, n, 0);
        const std::string text = ArgStr(st, a, n, 1);
        const float scale = n > 2 ? static_cast<float>(ArgNum(st, a, n, 2)) : 0.0f;
        const bool active = n > 3 ? ArgBool(st, a, n, 3) : false;
        const std::string style = n > 4 ? ArgStr(st, a, n, 4) : std::string{};
        return run_styled_button(u, key, text, scale, active, style);
    });
    RegVar(s, host, "ui_pbar", 1, [ui_sys, col_of](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            const float v = static_cast<float>(ArgNum(st, a, n, 0));
            const std::int64_t cid = n > 1 ? static_cast<std::int64_t>(ArgNum(st, a, n, 1)) : 0;
            u->ProgressBar(v, cid > 0 ? col_of(cid) : Color{0.80f, 0.66f, 0.38f, 1.0f});
        }
        return 0.0;
    });
    Reg<bool(LS *, std::string, std::string, bool, double)>(
        s, host, "ui_tab_btn", [ui_sys](LS *, std::string key, std::string lbl, bool active, double scale) -> bool {
            auto *u = ui_sys();
            return u && u->Tab(key, lbl, active, static_cast<float>(scale));
        });
    Reg<bool(LS *, std::string, double)>(
        s, host, "ui_set_metric", [ui_sys](LS *, std::string name, double val) -> bool {
            auto *u = ui_sys();
            return u && u->SetThemeMetric(name, static_cast<float>(val));
        });
    Reg<bool(LS *)>(s, host, "ui_in_auto", [ui_sys](LS *) -> bool {
        auto *u = ui_sys();
        return u && u->InAutoLayout();
    });
    Reg<double(LS *, std::string)>(
        s, host, "ui_get_metric", [ui_sys](LS *, std::string name) -> double {
            auto *u = ui_sys();
            return u ? u->ThemeMetric(name) : 0.0;
        });
    RegVar(s, host, "ui_grid_cols_begin", 1, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            u->GridColumnsBegin(static_cast<int>(ArgNum(st, a, n, 0)),
                                n > 1 ? static_cast<float>(ArgNum(st, a, n, 1)) : -1.0f,
                                n > 2 ? static_cast<float>(ArgNum(st, a, n, 2)) : -1.0f,
                                n > 3 ? static_cast<float>(ArgNum(st, a, n, 3)) : 0.0f);
        }
        return 0.0;
    });
    Reg<void(LS *, std::int64_t)>(s, host, "ui_next_row_span", [ui_sys](LS *, std::int64_t rows) {
        if (auto *u = ui_sys()) u->NextRowSpan(static_cast<int>(rows));
    });
    RegVar(s, host, "ui_next_gradient", 2, [ui_sys, col_of](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            const Color s_col = col_of(static_cast<std::int64_t>(ArgNum(st, a, n, 0)));
            const Color e_col = col_of(static_cast<std::int64_t>(ArgNum(st, a, n, 1)));
            u->NextGradient(s_col, e_col, n > 2 && ArgBool(st, a, n, 2));
        }
        return 0.0;
    });
    RegVar(s, host, "ui_next_blur", 0, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) u->NextBackdropBlur(n > 0 ? static_cast<float>(ArgNum(st, a, n, 0)) : 4.0f);
        return 0.0;
    });
    RegVar(s, host, "ui_next_clip", 0, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) u->NextClip(n <= 0 || ArgBool(st, a, n, 0));
        return 0.0;
    });
    Reg<void(LS *, std::string)>(s, host, "ui_next_dir", [ui_sys](LS *, std::string d) {
        if (auto *u = ui_sys()) {
            u->NextDirection(d == "rtl" ? UILayoutDirection::RTL : UILayoutDirection::LTR);
        }
    });
    Reg<void(LS *, std::string)>(s, host, "ui_dir_begin", [ui_sys](LS *, std::string d) {
        if (auto *u = ui_sys()) {
            u->LayoutDirectionBegin(d == "rtl" ? UILayoutDirection::RTL : UILayoutDirection::LTR);
        }
    });
    Reg<void(LS *)>(s, host, "ui_dir_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->LayoutDirectionEnd();
    });
    RegVar(s, host, "ui_safe_area_begin", 0, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) u->SafeAreaBegin(n > 0 ? static_cast<float>(ArgNum(st, a, n, 0)) : 0.0f);
        return 0.0;
    });
    Reg<void(LS *)>(s, host, "ui_safe_area_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->SafeAreaEnd();
    });
    Reg<void(LS *, double, double, double, double)>(
        s, host, "ui_set_safe_area", [ui_sys](LS *, double l, double t, double r, double b) {
            if (auto *u = ui_sys()) {
                u->SetSafeAreaInsets(static_cast<float>(l), static_cast<float>(t),
                                     static_cast<float>(r), static_cast<float>(b));
            }
        });
    Reg<void(LS *, std::string)>(s, host, "ui_next_placeholder", [ui_sys](LS *, std::string ph) {
        if (auto *u = ui_sys()) u->NextPlaceholder(ph);
    });
    Reg<void(LS *, std::string)>(s, host, "ui_next_tooltip", [ui_sys](LS *, std::string tip) {
        if (auto *u = ui_sys()) u->NextTooltip(tip);
    });
    RegVar(s, host, "ui_next_semantics", 1, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) {
            u->NextSemantics(ArgStr(st, a, n, 0),
                             n > 1 ? ArgStr(st, a, n, 1) : "",
                             n > 2 ? ArgStr(st, a, n, 2) : "",
                             n > 3 ? ArgStr(st, a, n, 3) : "");
        }
        return 0.0;
    });
    Reg<void(LS *, bool)>(s, host, "ui_next_visible", [ui_sys](LS *, bool v) {
        if (auto *u = ui_sys()) u->NextVisible(v);
    });
    Reg<void(LS *, bool)>(s, host, "ui_next_enabled", [ui_sys](LS *, bool en) {
        if (auto *u = ui_sys()) u->NextEnabled(en);
    });
    Reg<void(LS *, std::string)>(s, host, "ui_focus_request", [ui_sys](LS *, std::string key) {
        if (auto *u = ui_sys()) u->RequestFocus(key);
    });
    Reg<void(LS *)>(s, host, "ui_focus_clear", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->ClearFocus();
    });
    RegVar(s, host, "ui_scroll_to", 1, [ui_sys](LS *st, CVar *a, int n) -> double {
        if (auto *u = ui_sys()) u->ScrollToWidget(ArgStr(st, a, n, 0), n <= 1 || ArgBool(st, a, n, 1));
        return 0.0;
    });
    Reg<bool(LS *, std::string, std::string, std::string)>(
        s, host, "ui_drag_source", [ui_sys](LS *, std::string key, std::string tp, std::string pl) -> bool {
            auto *u = ui_sys();
            return u && u->DragSourceBegin(key, tp, pl);
        });
    Reg<void(LS *)>(s, host, "ui_drag_source_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->DragSourceEnd();
    });
    Reg<bool(LS *)>(s, host, "ui_drag_preview", [ui_sys](LS *) -> bool {
        auto *u = ui_sys();
        return u && u->DragPreviewBegin();
    });
    Reg<void(LS *)>(s, host, "ui_drag_preview_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->DragPreviewEnd();
    });
    RegVar(s, host, "ui_drop_target", 1, [ui_sys](LS *st, CVar *a, int n) -> bool {
        auto *u = ui_sys();
        return u && u->DropTargetBegin(ArgStr(st, a, n, 0), n > 1 ? ArgStr(st, a, n, 1) : "");
    });
    Reg<void(LS *)>(s, host, "ui_drop_target_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->DropTargetEnd();
    });
    Reg<bool(LS *)>(s, host, "ui_is_dragging", [ui_sys](LS *) -> bool {
        auto *u = ui_sys();
        return u && u->IsDragging();
    });
    Reg<std::string(LS *)>(s, host, "ui_drag_type", [ui_sys](LS *) -> std::string {
        auto *u = ui_sys();
        return u ? std::string(u->ActiveDragType()) : std::string{};
    });
    Reg<std::string(LS *)>(s, host, "ui_drag_payload", [ui_sys](LS *) -> std::string {
        auto *u = ui_sys();
        return u ? std::string(u->ActiveDragPayload()) : std::string{};
    });
    Reg<bool(LS *, std::string)>(s, host, "ui_is_drop_hovered", [ui_sys](LS *, std::string key) -> bool {
        auto *u = ui_sys();
        return u && u->IsDropHovered(key);
    });
    Reg<std::string(LS *, std::string)>(s, host, "ui_drop_payload", [ui_sys](LS *, std::string key) -> std::string {
        auto *u = ui_sys();
        return u ? u->DroppedPayload(key) : std::string{};
    });
    Reg<void(LS *)>(s, host, "ui_menubar_begin", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->MenuBarBegin();
    });
    Reg<void(LS *)>(s, host, "ui_menubar_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->MenuBarEnd();
    });
    Reg<bool(LS *, std::string, std::string)>(
        s, host, "ui_menu_begin", [ui_sys](LS *, std::string key, std::string lbl) -> bool {
            auto *u = ui_sys();
            return u && u->MenuBegin(key, lbl);
        });
    Reg<void(LS *)>(s, host, "ui_menu_end", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->MenuEnd();
    });
    RegVar(s, host, "ui_menu_item", 2, [ui_sys](LS *st, CVar *a, int n) -> bool {
        auto *u = ui_sys();
        return u && u->MenuItem(ArgStr(st, a, n, 0), ArgStr(st, a, n, 1),
                                n > 2 ? ArgStr(st, a, n, 2) : "",
                                n > 3 && ArgBool(st, a, n, 3));
    });
    Reg<void(LS *)>(s, host, "ui_menu_separator", [ui_sys](LS *) {
        if (auto *u = ui_sys()) u->MenuSeparator();
    });
    Reg<double(LS *)>(s, host, "ui_cursor_shape", [ui_sys](LS *) -> double {
        auto *u = ui_sys();
        return u ? static_cast<double>(u->DesiredCursorShape()) : 0.0;
    });
    Reg<double(LS *)>(s, host, "ui_pinch_delta", [ui_sys](LS *) -> double {
        auto *u = ui_sys();
        return u ? static_cast<double>(u->PinchDelta()) : 0.0;
    });
    Reg<std::string(LS *)>(s, host, "ui_semantic_json", [ui_sys](LS *) -> std::string {
        auto *u = ui_sys();
        return u ? u->SemanticJson() : std::string{};
    });
}

} // namespace fake2d
