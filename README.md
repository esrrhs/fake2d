# Fake2D

[English](README.md) | [中文说明](README_CN.md)

[![Build Status](https://github.com/esrrhs/fake2d/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/esrrhs/fake2d/actions)
[![License](https://img.shields.io/github/license/esrrhs/fake2d)](LICENSE)

**Fake2D** is a 2D game rendering engine driven by [FakeLua](https://github.com/esrrhs/fakelua): C++ owns the window, GPU resources, and scene data; FakeLua scripts orchestrate game logic with per-frame arena reset (no GC pauses).

> Status: **Phase 0 skeleton** — project layout, host loop, OpenGL clear, FakeLua `update(dt)` hook. See the [implementation plan](#implementation-plan) below.

## Design

Aligned with FakeLua’s host model:

| Layer | Responsibility |
|-------|----------------|
| **C++ host** | Window, GL context, renderer, textures/meshes, scene graph, input, assets |
| **FakeLua** | Gameplay glue: spawn entities, drive animations, UI flow, level scripts |
| **Per frame** | `update(dt)` → draw submitted commands → `fakelua::Reset(state)` |

Scripts should stay shallow-state. Heavy objects (sprites, atlases, physics bodies) live in C++ and are exposed through a narrow native API.

## Architecture (target)

```
┌─────────────────────────────────────────────┐
│                 FakeLua scripts             │
│         update / scene / UI logic           │
└─────────────────────┬───────────────────────┘
                      │ Call / Native bindings
┌─────────────────────▼───────────────────────┐
│                   Engine                    │
│  ScriptHost · Scene · Input · Resources     │
├─────────────────────┬───────────────────────┤
│   SpriteBatch       │  Camera / Transform   │
│   Texture / Atlas   │  Font (later)         │
├─────────────────────▼───────────────────────┤
│        Renderer (OpenGL 3.3 core)           │
│        Window (GLFW)                        │
└─────────────────────────────────────────────┘
```

## Repository layout

```
include/fake2d/     Public headers (Engine, Renderer, ScriptHost)
src/core/           Engine main loop
src/render/         GPU renderer
src/script/         FakeLua bridge
src/platform/       GLFW window
src/scene/          Scene graph (Phase 2+)
scripts/            Sample FakeLua entry scripts
examples/hello/     Minimal runnable window
docs/PLAN.md        Detailed roadmap checklist
```

## Requirements

- CMake ≥ 3.16, C++23 compiler
- OpenGL 3.3+ / GLFW 3.4 (fetched via CPM)
- [FakeLua](https://github.com/esrrhs/fakelua) — auto-detected from:
  1. Sibling checkout `../fakelua`
  2. `-DFAKELUA_SOURCE_DIR=/path/to/fakelua`
  3. CPM fetch of `esrrhs/fakelua` (fallback)

## Build

```bash
# Recommended: clone FakeLua next to this repo
#   /path/fakelua
#   /path/fake2d

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/bin/fake2d_hello
```

Window-only skeleton (no FakeLua link — useful for CI / bring-up):

```bash
cmake -S . -B build -DFAKE2D_WITH_FAKELUA=OFF
cmake --build build --parallel
```

A display is required for the interactive window; CI only checks that `fake2d_hello` links.

## Quick script

`scripts/main.lua`:

```lua
function update(dt)
    -- Submit draw commands via native bindings (Phases 1–2)
    return 0
end
```

## Implementation plan

| Phase | Goal | Deliverables |
|-------|------|--------------|
| **0 — Skeleton** *(current)* | Bootable host | GLFW window, GL clear, FakeLua compile/`update`/`Reset`, hello example, bilingual docs |
| **1 — Draw primitives** | First pixels | Ortho camera, colored quad, sprite batch, PNG texture load, UV rect |
| **2 — Scene & assets** | Structure | Node/transform hierarchy, layers, texture atlas, resource cache, hot reload (TCC) |
| **3 — Script API** | Author games in Lua | Native bindings: `sprite`, `camera`, `input`, `time`; sample mini-game |
| **4 — Text & audio** | Presentation | Bitmap/SDF font, sound playback stub, simple particle emitter |
| **5 — Polish** | Ship quality | Batch sorting, vsync/dpi, Android/desktop packing notes, benchmarks vs draw-call count |

Detailed checklist: [docs/PLAN.md](docs/PLAN.md).

## Version

`FAKE2D_VERSION_STRING` in [`include/fake2d/version.h`](include/fake2d/version.h) — currently **0.1.0** (Phase 0).

## License

MIT — see [LICENSE](LICENSE).
