#include "fake2d/atlas.h"
#include "fake2d/engine.h"
#include "fake2d/node.h"
#include "fake2d/scene.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <vector>

namespace {

constexpr int kSheetSize = 128;
constexpr int kTile = 64;

// TexturePacker hash-format atlas describing the procedurally painted sheet
// below (2x2 tiles of 64x64: circle, box, triangle, diamond).
constexpr const char *kAtlasJson = R"({
  "frames": {
    "red_circle": {
      "frame": {"x": 0, "y": 0, "w": 64, "h": 64},
      "rotated": false, "trimmed": false,
      "spriteSourceSize": {"x": 0, "y": 0, "w": 64, "h": 64},
      "sourceSize": {"w": 64, "h": 64}, "pivot": {"x": 0.5, "y": 0.5}
    },
    "green_box": {
      "frame": {"x": 64, "y": 0, "w": 64, "h": 64},
      "rotated": false, "trimmed": false,
      "spriteSourceSize": {"x": 0, "y": 0, "w": 64, "h": 64},
      "sourceSize": {"w": 64, "h": 64}, "pivot": {"x": 0.5, "y": 0.5}
    },
    "blue_triangle": {
      "frame": {"x": 0, "y": 64, "w": 64, "h": 64},
      "rotated": false, "trimmed": false,
      "spriteSourceSize": {"x": 0, "y": 0, "w": 64, "h": 64},
      "sourceSize": {"w": 64, "h": 64}, "pivot": {"x": 0.5, "y": 0.5}
    },
    "yellow_diamond": {
      "frame": {"x": 64, "y": 64, "w": 64, "h": 64},
      "rotated": false, "trimmed": false,
      "spriteSourceSize": {"x": 0, "y": 0, "w": 64, "h": 64},
      "sourceSize": {"w": 64, "h": 64}, "pivot": {"x": 0.5, "y": 0.5}
    }
  },
  "meta": { "image": "demo_sheet.png", "size": {"w": 128, "h": 128}, "scale": "1" }
})";

/// Paint a 128x128 RGBA sheet: four 64x64 shape tiles on transparent background.
void PaintDemoSheet(std::uint8_t *pixels) {
    auto coverage = [](int tile, float dx, float dy) -> std::uint8_t {
        switch (tile) {
            case 0:  // red circle
                return (dx * dx + dy * dy) <= 26.0f * 26.0f ? 255 : 0;
            case 1:  // green box
                return (std::fabs(dx) <= 22.0f && std::fabs(dy) <= 22.0f) ? 255 : 0;
            case 2: {  // blue triangle, wide bottom, apex top
                const float t = (dy + 26.0f) / 52.0f;
                if (t < 0.0f || t > 1.0f) return 0;
                return std::fabs(dx) <= 28.0f * (1.0f - t) ? 255 : 0;
            }
            default:  // yellow diamond
                return (std::fabs(dx) + std::fabs(dy) <= 28.0f) ? 255 : 0;
        }
    };

    const fake2d::Color tile_colors[4] = {
        fake2d::Color::FromRGBA8(225, 65, 65),
        fake2d::Color::FromRGBA8(75, 200, 95),
        fake2d::Color::FromRGBA8(75, 125, 230),
        fake2d::Color::FromRGBA8(240, 208, 66),
    };

    for (int y = 0; y < kSheetSize; ++y) {
        for (int x = 0; x < kSheetSize; ++x) {
            std::uint8_t *px = pixels + (static_cast<size_t>(y) * kSheetSize + x) * 4;
            const int tile = (x >= kTile ? 1 : 0) + (y >= kTile ? 2 : 0);
            const float dx = static_cast<float>(x % kTile) - (kTile * 0.5f - 0.5f);
            const float dy = static_cast<float>(y % kTile) - (kTile * 0.5f - 0.5f);
            const std::uint8_t a = coverage(tile, dx, dy);
            px[0] = static_cast<std::uint8_t>(tile_colors[tile].r * 255.0f);
            px[1] = static_cast<std::uint8_t>(tile_colors[tile].g * 255.0f);
            px[2] = static_cast<std::uint8_t>(tile_colors[tile].b * 255.0f);
            px[3] = a;
        }
    }
}

} // namespace

int main(int argc, char **argv) {
    fake2d::EngineConfig cfg;
    cfg.title = "fake2d hello";
    cfg.width = 960;
    cfg.height = 540;
    cfg.script_entry = "scripts/main.lua";

    int max_frames = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--headless") {
            cfg.headless = true;
        } else if (arg == "--frames" && i + 1 < argc) {
            max_frames = std::atoi(argv[++i]);
        } else if (arg == "--hot-reload") {
            cfg.hot_reload = true;
        }
    }

    fake2d::Engine engine;
    if (!engine.Init(cfg)) {
        std::fprintf(stderr, "fake2d_hello: engine init failed\n");
        return 1;
    }

    // --- resources: procedural demo atlas ---------------------------------
    fake2d::ResourceManager &resources = engine.GetResources();

    std::vector<std::uint8_t> pixels(static_cast<size_t>(kSheetSize) * kSheetSize * 4);
    PaintDemoSheet(pixels.data());
    const fake2d::TextureHandle sheet =
        resources.CreateTexture("demo_sheet", kSheetSize, kSheetSize, pixels.data());
    if (sheet == fake2d::kInvalidTextureHandle) {
        std::fprintf(stderr, "fake2d_hello: failed to create demo sheet\n");
        return 1;
    }

    fake2d::Atlas demo_atlas;
    if (!demo_atlas.LoadFromString(kAtlasJson, *resources.GetTexture(sheet))) {
        std::fprintf(stderr, "fake2d_hello: failed to parse demo atlas\n");
        return 1;
    }

    // --- scene: hierarchy + layer/z ordering demo --------------------------
    fake2d::Scene scene;
    scene.Root().GetTransform().SetPosition(480.0f, 270.0f);

    auto orbit_group = std::make_unique<fake2d::Node>("orbit_group");
    fake2d::Node *orbit = orbit_group.get();
    scene.Root().AddChild(std::move(orbit_group));

    const auto add_sprite = [&](const char *name, const char *region,
                                float ox, float oy, int layer, float z, float size) {
        auto node = std::make_unique<fake2d::SpriteNode>(name);
        node->SetAtlasRegion(&demo_atlas, region);
        node->SetSize(size, size);
        node->SetPivot({0.5f, 0.5f});
        node->GetTransform().SetPosition(ox, oy);
        node->SetLayer(layer);
        node->SetZ(z);
        orbit->AddChild(std::move(node));
    };

    // Layer 0: yellow diamond sits behind everything on the orbit group.
    add_sprite("yellow", "yellow_diamond", 0.0f, -130.0f, 0, 0.0f, 72.0f);
    // Layer 1: the three shapes share a layer; z decides who wins overlaps.
    add_sprite("red", "red_circle", -160.0f, 0.0f, 1, 0.0f, 88.0f);
    add_sprite("green", "green_box", 0.0f, 0.0f, 1, 1.0f, 88.0f);
    add_sprite("blue", "blue_triangle", 160.0f, 0.0f, 1, 2.0f, 88.0f);

    // Layer 2: a solid panel above the sprites — layering beats tree insertion
    // order, and it shares position with the green box (z=1, layer=1) below it.
    auto panel = std::make_unique<fake2d::RectNode>("center_panel");
    panel->SetSize(64.0f, 64.0f);
    panel->SetPivot({0.5f, 0.5f});
    panel->SetTint(fake2d::Color::FromRGBA8(255, 214, 102, 235));
    panel->SetLayer(2);
    panel->GetTransform().SetPosition(0.0f, 0.0f);
    scene.Root().AddChild(std::move(panel));

    double elapsed = 0.0;
    engine.SetFrameCallback([&](fake2d::Engine &engine) {
        elapsed += engine.DeltaTime();
        orbit->GetTransform().SetRotation(static_cast<float>(elapsed) * 0.8f);
        const float s = 1.0f + 0.08f * std::sin(elapsed * 2.0f);
        orbit->GetTransform().SetScale(s);
        scene.Draw(engine.GetRenderer());
    });

    return engine.Run(max_frames);
}
