#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace fake2d {

/// Optional Steamworks adapter (achievements, rich presence, language, cloud
/// save toggle). The engine never bundles the proprietary SDK: builds with
/// `-DFAKE2D_WITH_STEAM=ON -DFAKE2D_STEAM_SDK=/path/to/sdk` use the real
/// backend, every other build links the no-op stub below. Games call Init()
/// once at startup and Tick() once per frame.
namespace steam {

/// `app_id` 0 means "use steam_appid.txt / the shipped app id".
bool Init(std::uint32_t app_id = 0);
[[nodiscard]] bool Available();
void Shutdown();
void Tick();

void UnlockAchievement(std::string_view id);
void SetRichPresence(std::string_view key, std::string_view value);
/// Steam UI language when available, otherwise "english".
[[nodiscard]] std::string Language();

/// Cloud saves are a game-side decision; this only remembers the intent for
/// the save layer / UI. No-op backends keep the flag.
void SetCloudEnabled(bool enabled);
[[nodiscard]] bool CloudEnabled();

} // namespace steam
} // namespace fake2d
