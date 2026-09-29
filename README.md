# Fake2D

[English](README.md) | [中文说明](README_CN.md)

[![Build Status](https://github.com/esrrhs/fake2d/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/esrrhs/fake2d/actions)
[![License](https://img.shields.io/github/license/esrrhs/fake2d)](LICENSE)

**Fake2D** is a lightweight, modern 2D game rendering engine powered by [FakeLua](https://github.com/esrrhs/fakelua): C++ owns the window, GPU resources, and scene graph; FakeLua scripts orchestrate gameplay logic with a per-frame linear arena reset (**zero GC pauses**).

> Status: **Phase 2 complete** — bootable host, OpenGL 3.3 Core shader pipeline, 2D orthographic camera, dynamic `SpriteBatch`, 1x1 white fallback texture, FakeLua native bindings, `Transform2D` scene graph with layer/z ordering, TexturePacker JSON atlas parsing, handle-based resource manager, and script hot-reload. See the [Implementation Plan](#implementation-plan) and [docs/PLAN.md](docs/PLAN.md).

---

## Modern Design Goals

Fake2D is built from the ground up to follow modern 2D game engine industry standards (aligned with architectures like MonoGame, Raylib, and Defold):

1. **Zero-GC Per-Frame Execution**:
   - Eliminates garbage collection spikes (the primary performance pitfall in traditional Lua/JS/C# game engines).
   - Gameplay scripts can allocate frame-local tables, arrays, and variables freely; all frame memory is reclaimed instantly via `fakelua::inter::Reset(state)` at frame boundary.
2. **Data-Oriented SpriteBatching**:
   - Quad-based dynamic streaming VBO with pre-allocated static index buffer (`0-1-2-2-3-0` quad pattern).
   - **Unified 1x1 White Texture**: Colored geometry (rectangles, progress bars, debug cards) and textured sprites share the exact same GLSL shader and batch buffer, preventing pipeline breaks.
   - Multi-key batch sorting (Layer → Depth/Z → Texture ID → Blend Mode) to minimize draw calls (Phase 5).
3. **Decoupled 2D Camera & Coordinates**:
   - Dedicated `Camera2D` supporting viewport scaling, rotation, smooth panning, and zoom.
   - Two-way coordinate transformation (`ScreenToWorld` and `WorldToScreen`) for pixel-perfect HUDs and world interactions.
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
│   ├── camera.h             # 2D Orthographic camera
│   ├── engine.h             # Engine host & main loop
│   ├── math.h               # Vec2, Rect, Color, Mat4
│   ├── node.h               # Transform2D & scene graph nodes
│   ├── renderer.h           # Renderer façade
│   ├── resource_manager.h   # Handle-based resource pool
│   ├── scene.h              # Layer/z-ordered scene rendering
│   ├── shader.h             # Shader pipeline & uniforms
│   ├── sprite_batch.h       # High-performance SpriteBatch
│   ├── texture.h            # Texture2D & 1x1 white fallback
│   └── version.h            # Version definitions
├── src/
│   ├── core/                # Engine loop, lifecycle, resource pool
│   ├── platform/            # GLFW window & GL context
│   ├── render/              # OpenGL 3.3 Core render implementation
│   ├── scene/               # Node hierarchy & scene rendering
│   └── script/              # FakeLua integration & bindings
├── third_party/stb/         # stb_image.h
├── scripts/                 # Sample FakeLua entry scripts
├── examples/hello/          # Minimal runnable sample (supports --headless)
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

# Edit scripts/main.lua while running — the engine recompiles it on save:
./build/bin/fake2d_hello --hot-reload
```

Window-only skeleton build (without FakeLua, useful for rapid engine-only testing):

```bash
cmake -S . -B build -DFAKE2D_WITH_FAKELUA=OFF
cmake --build build --parallel
```

---

## Quick Script

`scripts/main.lua`:

```lua
local time = 0.0

function update(dt)
    time = time + dt

    -- Draw UI background card
    draw_quad(40, 40, 260, 160, 0.15, 0.18, 0.25, 0.9)

    -- Draw animated bouncing quad
    local x = 450 + 180 * math.sin(time * 2.0)
    local y = 200 + 80 * math.cos(time * 3.0)
    draw_quad(x, y, 80, 80, 0.9, 0.4, 0.7, 1.0)

    return 0
end
```

---

## Implementation Plan

| Phase | Goal | Deliverables | Status |
|-------|------|--------------|:------:|
| **0 — Skeleton** | Bootable host | GLFW window, GL clear, FakeLua bridge, hello sample, bilingual docs, headless CLI | **Done** |
| **1 — Draw primitives** | First pixels | Ortho camera, colored quads, `SpriteBatch`, 1x1 white fallback, PNG textures via stb_image | **Done** |
| **2 — Scene & assets** | Structure | Transform2D hierarchy, layers/z-order, texture atlas (SpriteSheet), resource cache, hot-reload | **Done** |
| **3 — Script API** | Author games in Lua | Native modules (`sprite`, `camera`, `input`, `time`), per-frame input snapshot, demo mini-game | Next |
| **4 — Text & audio** | Presentation | Bitmap / MSDF font renderer, audio playback, particle emitter batched into SpriteBatch | Planned |
| **5 — Polish** | Production quality | Multi-key batch sorting, HiDPI / Retina framebuffer scaling, draw-call benchmarks | Planned |

Detailed checklist: [docs/PLAN.md](docs/PLAN.md).

---

## License

MIT — see [LICENSE](LICENSE).
