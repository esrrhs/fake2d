#pragma once

#include <cstddef>

namespace fake2d {

/// One entry of the demo catalogue.
///
/// The command line accepts every entry as `--<flag>-demo`, and the launcher
/// lists the same table, so the menu can never offer something the host would
/// not know how to start. Presentation (titles, blurbs, cover art) stays on the
/// script side, keyed by `flag`.
struct DemoInfo {
    const char *flag;
    const char *script;
    /// Coarse grouping used by the launcher tabs: "game" or "engine".
    const char *category;
};

inline constexpr DemoInfo kDemos[] = {
    // --- playable demos --------------------------------------------------
    {"breakout", "scripts/game.lua", "game"},
    {"angry-birds", "scripts/angry_birds_demo.lua", "game"},
    {"limbo", "scripts/limbo_demo.lua", "game"},
    {"platformer", "scripts/platformer_demo.lua", "game"},
    {"mario", "scripts/mario.lua", "game"},
    {"beat-em-up", "scripts/beat_em_up_demo.lua", "game"},
    {"shmup", "scripts/shmup_demo.lua", "game"},
    {"snooker", "scripts/snooker_demo.lua", "game"},
    {"contra", "scripts/contra_demo.lua", "game"},
    {"city", "scripts/city_demo.lua", "game"},
    {"rts", "scripts/rts_demo.lua", "game"},
    {"vn", "scripts/vn_demo.lua", "game"},
    // --- engine feature showcases ----------------------------------------
    {"scene", "scripts/scene_demo.lua", "engine"},
    {"slope", "scripts/slope_demo.lua", "engine"},
    {"path", "scripts/path_demo.lua", "engine"},
    {"map", "scripts/map_demo.lua", "engine"},
    {"tile", "scripts/tile_demo.lua", "engine"},
    {"rt", "scripts/rt_demo.lua", "engine"},
    {"ui", "scripts/ui_demo.lua", "engine"},
    {"ui2", "scripts/ui2_demo.lua", "engine"},
    {"ui3", "scripts/ui3_demo.lua", "engine"},
    {"fog", "scripts/fog_demo.lua", "engine"},
    {"post", "scripts/post_demo.lua", "engine"},
    {"fx", "scripts/fx_demo.lua", "engine"},
    {"audio", "scripts/audio_demo.lua", "engine"},
    {"spatial", "scripts/spatial_demo.lua", "engine"},
    {"text", "scripts/text_demo.lua", "engine"},
};

inline constexpr std::size_t kDemoCount = sizeof(kDemos) / sizeof(kDemos[0]);

}  // namespace fake2d
