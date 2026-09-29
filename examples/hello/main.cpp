#include "fake2d/atlas.h"
#include "fake2d/engine.h"
#include "fake2d/node.h"
#include "fake2d/scene.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace {

// ---------------------------------------------------------------------------
// Procedural asset generation for the breakout sample: three small white
// textures tinted from Lua. Generated on demand so the repo stays asset-free.
// ---------------------------------------------------------------------------

constexpr int kSheetSize = 128;
constexpr int kTile = 64;

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

/// TexturePacker hash-format atlas describing PaintDemoSheet's 2x2 tiles.
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

// --- breakout shapes (white on transparent; tinted per brick row from Lua) ---

void PaintRoundedRect(std::uint8_t *pixels, int w, int h, int radius) {
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t *px = pixels + (static_cast<size_t>(y) * w + x) * 4;
            const float cx = static_cast<float>(std::clamp(x, radius, w - 1 - radius));
            const float cy = static_cast<float>(std::clamp(y, radius, h - 1 - radius));
            const float dx = static_cast<float>(x) - cx;
            const float dy = static_cast<float>(y) - cy;
            const float d = std::sqrt(dx * dx + dy * dy);
            const float a = std::clamp(static_cast<float>(radius) - d + 0.5f, 0.0f, 1.0f);
            px[0] = px[1] = px[2] = 255;
            px[3] = static_cast<std::uint8_t>(a * 255.0f);
        }
    }
}

void PaintCircle(std::uint8_t *pixels, int w, int h) {
    const float r = w * 0.5f - 0.5f;
    const float c = w * 0.5f - 0.5f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t *px = pixels + (static_cast<size_t>(y) * w + x) * 4;
            const float dx = static_cast<float>(x) - c;
            const float dy = static_cast<float>(y) - c;
            const float d = std::sqrt(dx * dx + dy * dy);
            const float a = std::clamp(r - d + 0.5f, 0.0f, 1.0f);
            px[0] = px[1] = px[2] = 255;
            px[3] = static_cast<std::uint8_t>(a * 255.0f);
        }
    }
}

/// Write the sample's PNG assets unless they already exist next to the binary.
void EnsureGameAssets() {
    const std::filesystem::path dir = "assets";
    std::error_code ec;
    std::filesystem::create_directory(dir, ec);

    const auto write = [&dir](const char *name, void (*paint)(std::uint8_t *, int, int), int w, int h) {
        const std::filesystem::path path = dir / name;
        if (std::filesystem::exists(path)) {
            return;
        }
        std::vector<std::uint8_t> pixels(static_cast<size_t>(w) * h * 4);
        paint(pixels.data(), w, h);
        if (!stbi_write_png(path.string().c_str(), w, h, 4, pixels.data(), w * 4)) {
            std::fprintf(stderr, "fake2d_hello: failed to write %s\n", path.string().c_str());
        }
    };

    write("brick.png", [](std::uint8_t *p, int w, int h) { PaintRoundedRect(p, w, h, 7); }, 86, 26);
    write("paddle.png", [](std::uint8_t *p, int w, int h) { PaintRoundedRect(p, w, h, 8); }, 120, 18);
    write("ball.png", PaintCircle, 18, 18);
}

// ---------------------------------------------------------------------------
// Phase 2 demo: C++ scene graph with a procedural atlas (kept behind
// --scene-demo). The default mode is the script-driven breakout game.
// ---------------------------------------------------------------------------

int RunSceneDemo(fake2d::Engine &engine, const fake2d::TextureHandle sheet, int max_frames) {
    fake2d::Atlas demo_atlas;
    if (!demo_atlas.LoadFromString(kAtlasJson, *engine.GetResources().GetTexture(sheet))) {
        std::fprintf(stderr, "fake2d_hello: failed to parse demo atlas\n");
        return 1;
    }

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
    engine.SetFrameCallback([&](fake2d::Engine &e) {
        elapsed += e.DeltaTime();
        orbit->GetTransform().SetRotation(static_cast<float>(elapsed) * 0.8f);
        const float s = 1.0f + 0.08f * std::sin(elapsed * 2.0f);
        orbit->GetTransform().SetScale(s);
        scene.Draw(e.GetRenderer());
    });

    return engine.Run(max_frames);
}

} // namespace

int main(int argc, char **argv) {
    fake2d::EngineConfig cfg;
    cfg.title = "fake2d";
    cfg.width = 960;
    cfg.height = 540;
    cfg.script_entry = "scripts/game.lua";

    bool scene_demo = false;
    int max_frames = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--headless") {
            cfg.headless = true;
        } else if (arg == "--frames" && i + 1 < argc) {
            max_frames = std::atoi(argv[++i]);
        } else if (arg == "--hot-reload") {
            cfg.hot_reload = true;
        } else if (arg == "--scene-demo") {
            scene_demo = true;
            cfg.script_entry = "scripts/main.lua";
        }
    }

    fake2d::Engine engine;
    if (!engine.Init(cfg)) {
        std::fprintf(stderr, "fake2d_hello: engine init failed\n");
        return 1;
    }

    if (scene_demo) {
        std::vector<std::uint8_t> pixels(static_cast<size_t>(kSheetSize) * kSheetSize * 4);
        PaintDemoSheet(pixels.data());
        const fake2d::TextureHandle sheet =
            engine.GetResources().CreateTexture("demo_sheet", kSheetSize, kSheetSize, pixels.data());
        if (sheet == fake2d::kInvalidTextureHandle) {
            std::fprintf(stderr, "fake2d_hello: failed to create demo sheet\n");
            return 1;
        }
        return RunSceneDemo(engine, sheet, max_frames);
    }

    EnsureGameAssets();
    return engine.Run(max_frames);
}
