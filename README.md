# Fake2D

[English](README.md) | [中文说明](README_CN.md)

[![Build Status](https://github.com/esrrhs/fake2d/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/esrrhs/fake2d/actions)
[![License](https://img.shields.io/github/license/esrrhs/fake2d)](LICENSE)

**Fake2D** is a lightweight, modern 2D game rendering engine powered by [FakeLua](https://github.com/esrrhs/fakelua): C++ owns the window, GPU resources, and scene graph; FakeLua scripts orchestrate gameplay logic with a per-frame linear arena reset (**zero GC pauses**).

> Status: **1.3 — Phase 8 Mario Demo complete** — everything in 1.2 (rendering, audio, particles, physics, animation, tilemaps, UI, platformer controller), plus slope tiles, Tiled parallax & image layers, SaveStore KV persistence, EntityStore fixed-slot subsystem, per-draw custom shaders, and a complete SMB 1-1 style `--mario-demo` with pixel-art sprites, dual-form player, Goombas, ?/brick bumping, mushroom powerups, and flagpole sequence. The mario demo exercises four of those Phase 8 systems directly: every enemy and effect is an EntityStore slot, the high score and best time persist through SaveStore (`saves/mario.json`, shown as `BEST` in the HUD), the goal pole runs through a per-draw GLSL slot (`assets/flag_shimmer.frag`), and a Tiled-slope hill at cols 19..24 is walked with `map_slope_dir` + `map_ground_y`. See the [Implementation Plan](#implementation-plan), [docs/PLAN.md](docs/PLAN.md), the [Scripting Guide](docs/SCRIPTING.md), and [Packaging Notes](docs/PACKAGING.md).

---

## Modern Design Goals

Fake2D is built from the ground up to follow modern 2D game engine industry standards (aligned with architectures like MonoGame, Raylib, and Defold):

1. **Zero-GC Per-Frame Execution**:
   - Eliminates garbage collection spikes (the primary performance pitfall in traditional Lua/JS/C# game engines).
   - Gameplay scripts can allocate frame-local tables, arrays, and variables freely; all frame memory is reclaimed instantly via `fakelua::inter::Reset(state)` at frame boundary.
2. **Data-Oriented SpriteBatching**:
   - Quad-based dynamic streaming VBO with pre-allocated static index buffer (`0-1-2-2-3-0` quad pattern).
   - **Unified 1x1 White Texture**: Colored geometry (rectangles, progress bars, debug cards) and textured sprites share the exact same GLSL shader and batch buffer, preventing pipeline breaks.
   - Multi-key batch sorting (Layer → Depth/Z → Blend Mode → Texture ID) merges same-texture quads into one draw call; alpha/additive blend modes; measured by the built-in `--bench` suite (~674 → 8 draw calls for 768 sprites / 8 textures).
3. **Decoupled 2D Camera & Coordinates**:
   - Dedicated `Camera2D` supporting viewport scaling, rotation, smooth panning, and zoom.
   - Two-way coordinate transformation (`ScreenToWorld` and `WorldToScreen`) for pixel-perfect HUDs and world interactions.
   - HiDPI/Retina native: the projection works in physical framebuffer pixels while game coordinates stay logical points, and live resize/DPI changes are tracked automatically.
4. **Data-Driven Scene & Assets**:
   - Spatial node hierarchy (`Transform2D`) with local-to-world dirty caching (Phase 2).
   - Texture Atlas / SpriteSheet support for packing entire levels into single draw calls.
   - Script hot-reload via FakeLua's embedded JIT backend for rapid iteration.
5. **Modern CI & Headless Testing**:
   - Native support for headless execution (`--headless`, `--frames N`) allows automated smoke and visual regression testing in virtualized CI runners without a physical display.
   - Built with Modern CMake 3.16+ standards and target-based exports (`fakelua::fakelua`, `fake2d::fake2d`).

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    FakeLua Scripts                      │
│            update(dt) · entities · UI logic             │
└────────────────────────────┬────────────────────────────┘
                             │ Native bindings (draw_quad, camera_*, ...)
┌────────────────────────────▼────────────────────────────┐
│                      Fake2D Engine                      │
│      ScriptHost · Scene Graph · Input · Resources       │
├────────────────────────────┬────────────────────────────┤
│        SpriteBatch         │         Camera2D           │
│   (Dynamic VBO / Quads)    │ (Ortho VP / Coordinate Xform)
├────────────────────────────┴────────────────────────────┤
│         Texture2D          │          Shader            │
│   (stb_image / 1x1 White)  │     (GLSL 330 Core)        │
├────────────────────────────┴────────────────────────────┤
│              Renderer (OpenGL 3.3 Core)                 │
│              Platform Window (GLFW 3.4)                 │
└─────────────────────────────────────────────────────────┘
```

---

## Repository Layout

```
fake2d/
├── CMakeLists.txt           # Modern CMake configuration
├── include/fake2d/          # Public engine API
│   ├── atlas.h              # TexturePacker JSON texture atlas
│   ├── animation.h          # Frame-animation clip pool
│   ├── audio.h              # miniaudio one-shot clip player
│   ├── camera.h             # 2D Orthographic camera (+ trauma shake)
│   ├── character.h          # Kinematic platformer controller
│   ├── engine.h             # Engine host & main loop
│   ├── font.h               # TTF/8x8 bitmap glyph atlas
│   ├── input.h              # Per-frame keyboard/mouse snapshot
│   ├── math.h               # Vec2, Rect, Color, Mat4
│   ├── node.h               # Transform2D & scene graph nodes
│   ├── particle.h           # CPU particle pool & emitters
│   ├── physics.h            # Built-in AABB/circle physics world
│   ├── renderer.h           # Renderer façade
│   ├── tilemap.h            # Tiled JSON tile maps + library
│   ├── ui.h                 # Anchored panels, labels, buttons
│   ├── resource_manager.h   # Handle-based resource pool
│   ├── scene.h              # Layer/z-ordered scene rendering
│   ├── shader.h             # Shader pipeline & uniforms
│   ├── sprite_batch.h       # High-performance SpriteBatch
│   ├── texture.h            # Texture2D & 1x1 white fallback
│   └── version.h            # Version definitions
├── src/
│   ├── audio/               # miniaudio device, voice mixing, WAV decode
│   ├── core/                # Engine loop, lifecycle, resource pool
│   ├── gameplay/            # Platformer character controller
│   ├── physics/             # Built-in 2D physics world + spatial broadphase
│   ├── platform/            # GLFW window & GL context
│   ├── render/              # GL 3.3 Core render, font, particles, animation, tilemap
│   ├── scene/               # Node hierarchy & scene rendering
│   ├── script/              # FakeLua integration & bindings
│   └── ui/                  # Anchored widget system
├── third_party/             # stb_image, stb_truetype, stb_rect_pack, miniaudio, font8x8
├── scripts/                 # Pure-Lua samples: game.lua (breakout), scene_demo.lua, map_demo.lua, platformer_demo.lua, mario.lua
├── examples/hello/          # Samples: breakout, --scene-demo, --map-demo, --platformer-demo, --mario-demo, --bench, --phys-bench
├── docs/SCRIPTING.md        # Script authoring guide & API reference
├── docs/PACKAGING.md        # Linux / Windows / macOS packaging notes
└── docs/PLAN.md             # Detailed roadmap and milestone checklist
```

---

## Requirements

- CMake ≥ 3.16, C++23 compiler (GCC 13+, Clang 16+, Apple Clang 15+)
- OpenGL 3.3+ Core Profile / GLFW 3.4 (fetched via CPM)
- [FakeLua](https://github.com/esrrhs/fakelua) (installed in standard system prefix `/usr/local` or detected via `find_package(fakelua)`)

---

## Build

```bash
# Standard build with FakeLua scripting:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/bin/fake2d_hello

# Run headless smoke test (e.g. for CI or remote environments):
./build/bin/fake2d_hello --headless --frames 60

# Capture a framebuffer PNG in headless mode (visual regression tests):
./build/bin/fake2d_hello --headless --frames 460 --screenshot game.png
./build/bin/fake2d_hello --headless --frames 40 --scene-demo --screenshot scene.png

# Draw-call / flush-time benchmark (immediate vs sorted batching):
./build/bin/fake2d_hello --headless --bench

# Edit scripts/game.lua while running — the engine recompiles it on save:
./build/bin/fake2d_hello --hot-reload

# All demos are pure-Lua scripts (the binary only generates their PNG/JSON/WAV
# assets); each flag selects a different entry script:
./build/bin/fake2d_hello --scene-demo      # sprites, rotation, particles, text

# Tiled tilemap + built-in physics + UI (R or the RESET button):
./build/bin/fake2d_hello --map-demo

# Scrolling platformer (A/D + Space; attract AI headless):
./build/bin/fake2d_hello --platformer-demo

# Super Mario Bros 1-1 style demo (A/D + Shift + Space; attract AI headless):
./build/bin/fake2d_hello --mario-demo

# Physics broadphase benchmark (brute force vs spatial hash grid):
./build/bin/fake2d_hello --headless --phys-bench
```

Window-only skeleton build (without FakeLua, useful for rapid engine-only testing):

```bash
cmake -S . -B build -DFAKE2D_WITH_FAKELUA=OFF
cmake --build build --parallel
```

---

## Quick Script

`scripts/game.lua` (the default entry) is a playable breakout clone driven
entirely from Lua — sprites, input, and physics, ~200 lines:

```lua
local W = 960
local paddle_x = W * 0.5          -- persistent state lives in file-level locals
local score = 0 + 0               -- mutable numbers use an expression initializer
                                  -- (see "state rules" in docs/SCRIPTING.md)

function update(dt)
    -- paddle follows the mouse / arrow keys
    if input_key_down("left") then paddle_x = paddle_x - 620 * dt end

    -- step the ball, collide with bricks, draw everything
    step_ball(dt)
    draw()
    return 0
end
```

The full API reference, the persistent-state rules (FakeLua arena + JIT
codegen constraints), and performance best practices live in the
[Scripting Guide](docs/SCRIPTING.md).

---

## Implementation Plan

| Phase | Goal | Deliverables | Status |
|-------|------|--------------|:------:|
| **0 — Skeleton** | Bootable host | GLFW window, GL clear, FakeLua bridge, hello sample, bilingual docs, headless CLI | **Done** |
| **1 — Draw primitives** | First pixels | Ortho camera, colored quads, `SpriteBatch`, 1x1 white fallback, PNG textures via stb_image | **Done** |
| **2 — Scene & assets** | Structure | Transform2D hierarchy, layers/z-order, texture atlas (SpriteSheet), resource cache, hot-reload | **Done** |
| **3 — Script API** | Author games in Lua | Native modules (`sprite`, `camera`, `input`, `time`), per-frame input snapshot, playable breakout sample, [Scripting Guide](docs/SCRIPTING.md) | **Done** |
| **4 — Text & audio** | Presentation | TTF bitmap font (8x8 fallback) batched into SpriteBatch, miniaudio one-shot SFX, zero-allocation CPU particle system, framebuffer screenshots | **Done** |
| **5 — Polish (1.0)** | Production quality | Multi-key sorted batching + additive blending, HiDPI/Retina, `--bench` suite, cross-platform [packaging notes](docs/PACKAGING.md), tag-driven release automation | **Done** |
| **6 — Gameplay (1.1)** | Game systems | Built-in AABB/circle physics with sensor contacts, frame animation, Tiled JSON tilemaps (+solid colliders), anchored UI panels/labels/buttons; `--map-demo` | **Done** |
| **7 — Platformer & scale (1.2)** | Controller & scale | Spatial-hash broadphase (`--phys-bench` ~6x), line/debug primitives, trauma camera shake, WAV + looping music, tilemap platformer controller (coyote/buffer/one-way) + `--platformer-demo` | **Done** |
| **8 — Engine Gaps & Mario (1.3)** | Completeness | Tilemap slopes (`map_ground_y`), Tiled parallax + image layers, `SaveStore` (`storage_*`), `EntityStore` (`ent_*`), custom shader slots (`shader_*`), SMB 1-1 `--mario-demo` | **Done** |

Detailed checklist: [docs/PLAN.md](docs/PLAN.md).

---

## License

MIT — see [LICENSE](LICENSE).
