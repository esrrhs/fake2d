#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "fake2d/math.h"
#include "fake2d/ui.h"

namespace fakelua {
class State;
struct CVar;
} // namespace fakelua

namespace fake2d {

class Engine;

/// Lossless float-precision color pool indexed by 1-based integer handles (`0` = unset).
class ColorPool {
  public:
    std::int64_t Id(const Color &c);
    [[nodiscard]] Color Get(std::int64_t id, const Color &fallback = Color::White()) const;
    std::int64_t Pack(double r, double g, double b, double a);

    void SetThemeColor(std::string name, const Color &c);
    std::int64_t ThemeId(const std::string &name, const Color &fallback = Color::White()) const;
    void Clear();

  private:
    std::vector<Color> colors_;
    std::unordered_map<std::string, std::int64_t> key_to_id_;
    std::unordered_map<std::string, std::int64_t> theme_ids_;
};

/// UISystem button style preset (colors applied temporarily for `ui_button_ex`).
struct UIButtonStyle {
    Color button{};
    Color hover{};
    Color pressed{};
    Color border{};
    Color text{};
};

struct UIButtonStylePreset {
    UIButtonStyle normal{};
    UIButtonStyle active{};
    bool has_active_variant = false;
};

/// Host context connecting `Engine` (`UISystem`), `UITipTracker`, `UITextureCache`,
/// and `ColorPool` to the generic UI FakeLua bindings.
struct LuaUIHost {
    Engine *engine = nullptr;
    UITipTracker *tips = nullptr;
    UITextureCache *textures = nullptr;
    ColorPool *colors = nullptr;

    std::string root_dir;
    std::string texture_subdir = "assets/ui/";
    Color default_text_color{0.92f, 0.88f, 0.78f, 1.0f};
    std::unordered_map<std::string, UIButtonStylePreset> button_styles;

    std::function<void(const char *)> note_native;
    std::function<void()> before_sprite_draw;
};

/// `vsnprintf`-compatible formatter reading conversion specifiers from FakeLua variadic arguments.
[[nodiscard]] std::string FormatFromLuaArgs(fakelua::State *s, fakelua::CVar *args, int n);

/// Register pure color pool, `vsnprintf`, and UTF-8 utility helpers (`u_col`, `u_hex`, `u_theme`, `u_strf`, ...).
void RegisterCommonUILuaApi(fakelua::State *s, LuaUIHost *host);

/// Register full `UISystem` layout, screen-space draw, hit-test, input capture, cached sprite,
/// and scroll-aware tooltip hit-testing Lua primitives (`ui_*`).
void RegisterLayoutUILuaApi(fakelua::State *s, LuaUIHost *host);

} // namespace fake2d
