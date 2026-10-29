# Fake2D

[English](README.md) | [中文说明](README_CN.md)

[![Build Status](https://github.com/esrrhs/fake2d/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/esrrhs/fake2d/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-brightgreen.svg)](https://en.cppreference.com/w/cpp/23)
[![OpenGL](https://img.shields.io/badge/OpenGL-3.3%20Core%20%7C%202.1-orange.svg)](https://www.opengl.org/)
[![FakeLua](https://img.shields.io/badge/Script-FakeLua-red.svg)](https://github.com/esrrhs/fakelua)
[![Version](https://img.shields.io/badge/version-1.27.0-informational.svg)](include/fake2d/version.h)

**Fake2D** is a modern, lightweight, high-performance 2D game engine powered by [FakeLua](https://github.com/esrrhs/fakelua):
- **C++23 Host Engine**: Controls window management, GPU rendering pipelines, 2D physics, multi-bus audio mixing, and native resource lifecycles;
- **FakeLua Scripting Layer**: Orchestrates gameplay systems, UI, and entity behaviors, executing per-frame linear arena resets for **guaranteed Zero Garbage Collection pauses (Zero-GC Pauses)**.

Engineered around modern industry standards (aligned with Raylib, MonoGame, and Defold philosophies), Fake2D delivers a complete, cohesive 2D suite: **dynamic streaming batching, Tiled orthogonal maps, 2D physics & collision filtering, spatial/multi-bus audio, multi-pass post-processing chains, immediate-mode UI, UTF-8/CJK text with localization, and cross-platform automated testability**. It is production-ready for commercial and indie 2D game development.

---

## 🌟 Modern Architectural Highlights

### 1. Deterministic Zero-GC Per-Frame Architecture
- **Eliminates GC Stutter**: Traditional game engines running Lua, JavaScript, or C# suffer from frame drops and unpredictable GC spikes when creating high-frequency temporary objects;
- **Linear Arena Reset**: Fake2D scripts can freely allocate frame-local tables, vectors, and closures. At the end of each frame, the engine resets the linear memory arena via `fakelua::inter::Reset(state)` in microseconds—delivering rock-solid 60 / 120+ FPS with zero GC stalls;
- **Native Cross-Frame Storage**: Persistent data is strictly managed through the C++ `EntityStore` (256 native handle-based entity slots) and `SaveStore` (JSON key-value persistence plus CRC-checked binary save containers with metadata headers), maintaining a clear separation between transient frame calculations and long-lived game state.

### 2. Data-Oriented Streaming SpriteBatch
- **Dynamic Streaming VBO**: Backed by a pre-allocated static index buffer (standard `0-1-2-2-3-0` quad pattern);
- **Unified 1×1 Pure White Texture**: Solid colored geometry, UI panels, health bars, and textured sprites share the exact same GLSL shader and vertex layout—completely eliminating batch-breaking pipeline swaps;
- **Multi-Key Batch Sorting**: Deferred command buffer supporting multi-key stable sorting (`Layer → Depth/Z → Blend Mode → Texture ID`). Identical textures and states coalesce into unified draw calls automatically; supports Alpha, Additive, and Multiply blending;
- **Extreme Throughput**: Built-in benchmark (`--bench`) demonstrates **674.4 → 8.0 draw calls** in a 768-sprite / 8-texture workload.

### 3. Decoupled 2D Camera & Coordinates (Camera2D)
- Independent orthographic view-projection matrix calculation supporting smooth panning, zoom scaling, rotation, and exact `ScreenToWorld` / `WorldToScreen` coordinate inversions;
- **Native HiDPI / Retina**: Rendering runs at physical framebuffer pixel resolution while game coordinates remain in logical units; window resizes and DPI changes are tracked in real-time;
- **Camera Follow Toolkit**: World boundaries clamping (`SetBounds`), viewport centering, frame-rate-independent exponential smoothing, SMB-style forward-only scroll lock, deadzone tracking, and Trauma-decay camera shake (`camera_shake`).

### 4. Comprehensive 2D Subsystems
Fake2D is a fully battery-included engine, not just a bare graphics library:
- **Tiled Map System**: Orthogonal JSON parser supporting tile layers, parallax scrolling, image layers, object groups (`objectgroup` spawns/triggers), slope physics, and **animated tiles (`animation` cycles synchronized to an engine clock)**;
- **Built-in Physics & Pathfinding**: Zero-dependency 2D rigid bodies (circles, oriented boxes and convex polygons) with density-derived mass/inertia, sequential impulses with warm starting and Coulomb friction, kinematic bodies, damping, automatic sleeping, distance/rope/revolute/weld joints (motors and limits), substepped anti-tunneling, spatial hash grid broadphase (≈5× faster than brute force), fixed timestep with render interpolation (`phys_smooth_x/y`), 16-bit category/mask filters, ray casts & shape overlaps, and grid A\* pathfinding (octile heuristic, corner-cut prevention, string-pulling line-of-sight smoothing);
- **Studio-Grade Audio Engine**: Device-decoupled software mixer, 3-bus hierarchy (Master / SFX / Music), linear gain envelopes and music cross-fades, 3D positional audio (distance rolloff and stereo pan), low-memory streamed playback (8192-frame streaming windows for WAV / MP3 / OGG / FLAC), and real-time stereo VU peak metering;
- **Multi-Pass Post-Processing & FBO**: Offscreen `RenderTarget` pool, ping-pong blur passes, bloom thresholding (`fx_bright`), animated wave shimmer (`fx_wave` with auto `u_time`/`u_resolution`), fog of war with multiply blending & `rt_sample` exploration probes, and baked minimaps;
- **Immediate-Mode UI (UISystem)**: Ready-to-use widgets including Panels, Buttons, Labels, Sliders, Checkboxes, UTF-8 TextInputs, ScrollAreas, ProgressBars, RadioButtons, Combos, draggable Windows (with mouse deltas), Tab bars, disclosure Trees, and Tooltips.
- **Text & Platform Layer**: UTF-8 text with an on-demand glyph atlas (CJK/Japanese/Korean included, `font_load` for any TTF/TTC/OTF), CJK per-character vs Latin word wrapping, rotated and shrink-to-fit text, TSV localization packs switched at runtime, cross-platform resource/user directories, v2 save containers with CRC, and an optional Steamworks adapter (achievements / Rich Presence / language / cloud flag) that stubs out when the SDK is absent.

### 5. Developer Experience & Automated Testing
- **JIT Script Hot-Reload**: GCC JIT backend with automatic interpreter fallback; edit Lua scripts live and see changes reflected instantly without restarting;
- **Headless & CI-Ready**: First-class `--headless`, `--frames N`, and `--screenshot` CLI flags allow visual regression tests and automated smoke validation in headless Linux/macOS/Windows environments;
- **16 Built-in Self-Test Suites**: Over 680 assertions covering rigid-body dynamics, joints, sleeping, anti-tunneling, physics queries, filters, timesteps, tilemap objects, A\* pathfinding, render targets, UI widgets, post-FX, fog of war, audio mixing, animated tiles, UTF-8/CJK text, and the platform layer (all mutation-tested);
- **Standalone Packaging**: Bundling scripts under `tools/` generate self-contained archives on Linux, macOS, and Windows with all dependencies, shaders, and assets included.

---

## 🏛 Engine Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                             FakeLua Scripts                                 │
│          update(dt) · Entity Logic · State Machines · UI Layout             │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       │ Native Bindings (draw_*, phys_*, audio_*, map_*, ...)
┌──────────────────────────────────────▼──────────────────────────────────────┐
│                                Fake2D Engine                                │
│   ScriptHost (JIT / Hot-Reload) · Resources · EntityStore · SaveStore (v2)  │
├───────────────────────┬──────────────────────────────┬──────────────────────┤
│   Rendering Pipeline  │       Physics & World        │     Audio Engine     │
│  · SpriteBatch (Sort) │  · PhysicsWorld (AABB/Circle)│  · Software Mixer    │
│  · Camera2D (VP/Shake)│  · Spatial Hash Broadphase   │  · 3 Buses (Master)  │
│  · RenderTarget (FBO) │  · Fixed-Step Accumulator    │  · 3D Positional Pan │
│  · PostFX & Shaders   │  · Collision Filters (Mask)  │  · 8K-Frame Streams  │
│  · UTF-8/CJK Atlas    │  · Tiled Map & Tile Anim     │  · VU Peak Meters    │
│  · CPU Particles (8K) │  · A* Grid Pathfinding       │                      │
├───────────────────────┴──────────────────────────────┴──────────────────────┤
│                       Immediate-Mode UI (UISystem)                          │
│     Window · Panel · Button · Slider · Input · Radio · Combo · Tab · Tree   │
│     LabelFit (shrink-to-fit) · Tooltip queue                                │
├─────────────────────────────────────────────────────────────────────────────┤
│                    Platform Layer · I18n · AppPaths · Steam                 │
│          OpenGL 3.3 Core (GL 2.1 Fallback) · GLFW 3.4 (Input/Gamepad)       │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 🎮 Runnable Demos Matrix

Fake2D comes with 16 rich, self-contained demos demonstrating platformer mechanics, retro gameplay, spatial audio, immediate UI, and graphical post-processing:

| Demo Flag | Topic | Key Highlighted Features |
|---|---|---|
| `./fake2d_hello` | **Breakout (Default)** | Ball physics reflections, brick destruction, particle debris, HUD text, and procedural audio chirps |
| `--mario-demo` | **Super Mario Bros 1-1** | Faithful SMB 1-1 recreation; small/big Mario forms, Goomba patrols & squashing, ? blocks, brick shatter particles, flag pole sequence, attract AI |
| `--angry-birds-demo` | **Angry Birds Sandbox** | Slingshot impulse launch, stacked towers that sleep and wake, contact-driven pig pops, camera follow, synthesized SFX |
| `--limbo-demo` | **LIMBO Jointed Sandbox** | 6-part revolute ragdoll, 9-node rope chain and revolute bridge, buoyancy water, and sever-on-death pieces that stay tethered |
| `--platformer-demo` | **Platformer Controller** | Kinematic tile platforming; coyote time, jump buffering, one-way ledges, coin pickups, smooth follow camera |
| `--slope-demo` | **Slopes & Terrain** | Tiled slope calculation (`map_slope_dir` / `map_ground_y`), smooth angle traversal and live ground overlays |
| `--path-demo` | **Maze Chase & A\* Pathfinding** | Generated maze, staggered AI tracking, player autopilot mode, and live polyline path drawing |
| `--tile-demo` | **Animated Tiles** | Tiled tileset animations driven by a single engine clock; rippling pond water and torch flames |
| `--fog-demo` | **Fog of War** | Procedural world; multiply blend mask, cumulative exploration reveal, clickable minimap, and `rt_sample` probes |
| `--fx-demo` | **Multi-Pass Post-FX & Bloom** | Procedural skyline; FBO downsampling, separable 9-tap Gaussian blur, bloom thresholding, wave wobble, split-screen |
| `--post-demo` | **Post-Grade & Baked Minimap** | Presentation-grade shaders (desaturation, lift, vignette, scanlines) and camera-baked tilemap minimap |
| `--rt-demo` | **Render Targets & Feedback Trails**| Bake-once background canvas beside an accumulating fading motion trail |
| `--spatial-demo` | **3D Positional Audio** | 4 world audio emitters with WASD listener navigation; linear distance rolloff, stereo pan, range rings, and L/R VU meters |
| `--audio-demo` | **Audio Console & Mixer** | 3-bus gain sliders, cross-fades, mix freezing pause, 24-segment VU meter, and decoded vs streamed memory comparison |
| `--ui-demo` / `--ui2-demo` / `--ui3-demo` | **Immediate-Mode UI Showcases** | Sliders, checkboxes, UTF-8 text fields, scroll areas, progress bars, radio groups, combo popups, draggable windows, tab bars, and trees |
| `--text-demo` | **Text & Platform Layer** | UTF-8/CJK rendering with live language switching, CJK per-character wrapping, rotated and shrink-to-fit text, user save/config paths, a v2 save container with CRC, and Steam adapter state |

---

## 🚀 Getting Started

### Prerequisites

- **CMake**: ≥ 3.16
- **C++ Compiler**: Modern compiler with **C++23** support (GCC 13+, Clang 16+, Apple Clang 15+, MSVC 2022)
- **Graphics**: OpenGL 3.3+ Core Profile (runtime fallback to GL 2.1 supported); GLFW 3.4 and nlohmann_json fetched automatically via CPM
- **Scripting Engine**: [FakeLua](https://github.com/esrrhs/fake2d) installed in `/usr/local` or discovered via CMake `find_package(fakelua REQUIRED)`

### Build & Run

```bash
# 1. Clone the repository
git clone https://github.com/esrrhs/fake2d.git
cd fake2d

# 2. Configure build files (Release mode)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# 3. Build in parallel
cmake --build build --parallel

# 4. Launch the default Breakout game
./build/bin/fake2d_hello
```

> **C++ Only Mode**: To build a windowed skeleton without linking FakeLua, pass `-DFAKE2D_WITH_FAKELUA=OFF`.

---

## 💻 CLI Usage Guide

### Launching Demos

```bash
# Super Mario Bros 1-1 (A/D to move, Shift to run, Space to jump, Gamepad supported)
./build/bin/fake2d_hello --mario-demo

# Angry Birds sandbox (drag, release; camera follows the shot, SFX; R restarts)
./build/bin/fake2d_hello --angry-birds-demo

# LIMBO-style jointed sandbox (A/D, SPACE, E rope, K sever, R reset)
./build/bin/fake2d_hello --limbo-demo

# Scrolling Platformer Demo (Smooth camera, coyote time, one-way platforms)
./build/bin/fake2d_hello --platformer-demo

# Maze Chase & A* Pathfinding (Press P to toggle path lines)
./build/bin/fake2d_hello --path-demo

# Tiled Animated Tiles (Rippling pond & flickering torches)
./build/bin/fake2d_hello --tile-demo

# Fog of War System (Press F to toggle fog, R to reset)
./build/bin/fake2d_hello --fog-demo

# Multi-pass Post-FX & Bloom (Press S to toggle split-screen mode)
./build/bin/fake2d_hello --fx-demo

# 3D Positional Audio (WASD to move the listener, mouse interaction)
./build/bin/fake2d_hello --spatial-demo

# Audio Mixer & Streaming Benchmark
./build/bin/fake2d_hello --audio-demo

# Full Immediate-Mode UI Suite (Draggable windows, tabs, combos)
./build/bin/fake2d_hello --ui3-demo

# UTF-8/CJK text & platform layer (L switches language, SAVE writes a v2 slot)
./build/bin/fake2d_hello --text-demo
```

### Development & Benchmarking Options

```bash
# Enable live script hot-reload (saving scripts/game.lua updates the running game)
./build/bin/fake2d_hello --hot-reload

# Execute a custom Lua entry script
./build/bin/fake2d_hello --entry scripts/mario.lua

# Adjust fixed physics simulation rate (default 60 Hz; or variable step)
./build/bin/fake2d_hello --fixed-hz 120 --mario-demo
./build/bin/fake2d_hello --variable-step --mario-demo

# Run graphics SpriteBatch benchmark (immediate vs sorted)
./build/bin/fake2d_hello --headless --bench

# Run physics broadphase benchmark (brute-force vs spatial hash)
./build/bin/fake2d_hello --headless --phys-bench
```

### Headless Verification & Testing

Fake2D supports first-class headless execution in display-less CI environments (e.g. Linux with xvfb):

```bash
# Run 60 frames headlessly and exit cleanly
xvfb-run ./build/bin/fake2d_hello --headless --frames 60

# Capture framebuffer screenshot for visual regression testing
xvfb-run ./build/bin/fake2d_hello --headless --mario-demo --shot-frame 60 --screenshot mario.png

# Run the 16 built-in engine self-test suites (verified via exit codes)
xvfb-run ./build/bin/fake2d_hello --phys-query-test    # Ray casts, overlaps, contact lifecycle
xvfb-run ./build/bin/fake2d_hello --phys-filter-test   # 16-bit category/mask collision filtering
xvfb-run ./build/bin/fake2d_hello --phys-rigid-test    # Rotational inertia, friction, resting stacks
xvfb-run ./build/bin/fake2d_hello --phys-extended-test # Joints, sleeping, kinematic bodies, anti-tunneling
xvfb-run ./build/bin/fake2d_hello --timestep-test      # Fixed-step accumulator & camera smooth
xvfb-run ./build/bin/fake2d_hello --map-object-test    # Tiled object groups & custom properties
xvfb-run ./build/bin/fake2d_hello --path-test          # Tilemap A* pathfinding & string pulling
xvfb-run ./build/bin/fake2d_hello --tile-test          # Tiled tileset animation clock ticks
xvfb-run ./build/bin/fake2d_hello --rt-test            # RenderTarget FBO pool & blit checks
xvfb-run ./build/bin/fake2d_hello --ui-test            # Immediate UI widgets & input injection
xvfb-run ./build/bin/fake2d_hello --post-test          # Post-processing grade & minimap bake
xvfb-run ./build/bin/fake2d_hello --fx-test            # Multi-pass ping-pong blur & uniforms
xvfb-run ./build/bin/fake2d_hello --fog-test           # Fog of war multiply blending & rt_sample
xvfb-run ./build/bin/fake2d_hello --audio-test         # Mixer buses, envelopes, streamed music
xvfb-run ./build/bin/fake2d_hello --font-test          # UTF-8/CJK glyph atlas, wrapping, rotation, shrink
xvfb-run ./build/bin/fake2d_hello --platform-test      # i18n packs, user paths, save v2, Steam stub, UI text
```

---

## 📝 Quick Scripting Example

Here is a concise sample demonstrating sprite rendering, input handling, camera tracking, and immediate UI in Fake2D:

```lua
-- scripts/my_game.lua
local W, H = 960, 540
local player_x = W * 0.5
local player_y = H * 0.8
local speed = 350.0 + 0.0     -- Initialized with float expressions

function update(dt)
    -- 1. Read input snapshot (keyboard or gamepad)
    if input_key_down("left") or input_pad_down(0, "dpad_left") then
        player_x = player_x - speed * dt
    end
    if input_key_down("right") or input_pad_down(0, "dpad_right") then
        player_x = player_x + speed * dt
    end
    if input_key_pressed("space") or input_pad_pressed(0, "a") then
        audio_play("jump", 0.8)
    end

    -- 2. Exponentially smooth camera tracking
    camera_follow(player_x, player_y, 8.0, dt)

    -- 3. Batched rendering
    draw_clear(24, 28, 36, 255)
    
    -- Draw active tilemap layer
    map_draw(1)
    
    -- Draw player quad (automatically batched with the 1x1 white texture)
    draw_quad(player_x - 16, player_y - 16, 32, 32, 60, 180, 255, 255)
    
    -- Immediate-mode UI elements
    ui_panel("hud", "top_left", 10, 10, 160, 40)
    ui_label("score", "top_left", 20, 20, "SCORE: 1200", 1.0)
    if ui_button("btn_pause", "top_right", -110, 10, 100, 32, "PAUSE") then
        -- Handle pause logic
    end

    return 0
end
```

> 📖 **Reference Documentation**:
> - For a deep dive into FakeLua syntax constraints, Arena memory lifecycle rules, and the complete API surface, read the [**Scripting Guide (docs/SCRIPTING.md)**](docs/SCRIPTING.md).
> - For standalone distribution and cross-compilation instructions, refer to [**Packaging Notes (docs/PACKAGING.md)**](docs/PACKAGING.md).

---

## 🗂 Codebase Layout

```
fake2d/
├── CMakeLists.txt           # Modern CMake root build configuration
├── include/fake2d/          # Public C++ engine interface headers
│   ├── animation.h          # Keyframe AnimationSystem
│   ├── atlas.h              # TexturePacker JSON parser
│   ├── audio.h              # Multi-bus audio engine, 3D pan & streamed tracks
│   ├── camera.h             # Camera2D, screen/world transforms, smoothing & shake
│   ├── character.h          # Kinematic platformer character controller
│   ├── engine.h             # Engine host lifecycle & game loop
│   ├── entity_store.h       # C++ entity slot persistence across arena resets
│   ├── font.h               # TTF rasterizer & 8x8 bitmap glyph atlas
│   ├── input.h              # Keyboard, mouse deltas & gamepad snapshot
│   ├── math.h               # Vec2, Rect, Color, Mat4 math library
│   ├── node.h               # Transform2D spatial node hierarchy
│   ├── particle.h           # Zero-allocation 8192 particle pool & emitters
│   ├── physics.h            # Rigid bodies, joints, sleeping & broadphase
│   ├── renderer.h           # Renderer façade (OpenGL abstraction)
│   ├── render_target.h      # Offscreen RenderTarget (FBO) pool
│   ├── resource_manager.h   # Handle-based resource manager
│   ├── save_store.h         # JSON key-value disk persistence
│   ├── scene.h              # Layered scene graph rendering
│   ├── script_host.h        # FakeLua runtime bridge & JIT host
│   ├── shader.h             # GLSL shader manager with automatic uniform stamps
│   ├── sprite_batch.h       # High-throughput multi-key sorted SpriteBatch
│   ├── texture.h            # Texture2D & 1x1 white texture singleton
│   ├── tilemap.h            # Tiled maps, slopes, animated tiles & A* pathfinder
│   ├── ui.h                 # Immediate-mode UISystem & widgets
│   └── version.h            # Semantic engine versioning
├── src/                     # C++ engine implementations
│   ├── audio/               # miniaudio device interface, mixing & codecs
│   ├── core/                # Engine tick, timesteps, entities & saves
│   ├── gameplay/            # Gameplay controllers & helpers
│   ├── physics/             # Rigid bodies, spatial hash, contacts & queries
│   ├── platform/            # GLFW windowing, input polling & events
│   ├── render/              # GL rendering, batching, FBOs, shaders, fonts
│   ├── scene/               # Spatial node transforms & caching
│   ├── script/              # FakeLua native bindings & execution
│   └── ui/                  # Immediate-mode UI layout & interaction
├── scripts/                 # Complete pure-Lua demo and game scripts
│   ├── game.lua             # Classic Breakout game
│   ├── mario.lua            # Super Mario Bros 1-1 level
│   ├── angry_birds_demo.lua # Slingshot rigid-body sandbox
│   ├── limbo_demo.lua       # Ragdoll, rope, bridge & buoyancy sandbox
│   ├── platformer_demo.lua  # Kinematic platformer demo
│   ├── slope_demo.lua       # Terrain slope traversal
│   ├── path_demo.lua        # Maze chase & A* tracking
│   ├── tile_demo.lua        # Animated tiles showcase
│   ├── fog_demo.lua         # Fog of war & exploration mask
│   ├── fx_demo.lua          # Bloom post-processing & split-screen
│   ├── post_demo.lua        # Color grading & minimap bake
│   ├── rt_demo.lua          # Offscreen FBO trails
│   ├── audio_demo.lua       # Mixing console & streaming benchmark
│   ├── spatial_demo.lua     # 3D positional audio
│   ├── text_demo.lua        # UTF-8/CJK, i18n, paths, save v2 & Steam
│   └── ui_demo*.lua         # Immediate-mode UI demonstrations
├── tools/                   # Standalone packaging scripts (Linux/macOS/Windows)
├── docs/                    # Extensive documentation
│   ├── SCRIPTING.md         # Scripting guide & complete Lua API reference
│   ├── PACKAGING.md         # Packaging & distribution instructions
│   └── PLAN.md              # Architectural blueprint & milestone records
└── tests/                   # Regression and unit tests
```

---

## 📦 Distribution & Packaging

Fake2D provides standalone bundling scripts under `tools/` for Linux, macOS, and Windows. They automatically bundle dependent shared libraries, shaders, and scripts into self-contained distributable archives requiring no local runtime installation:

```bash
# Package for macOS (tar.gz with codesigned dylibs and @executable_path rpath):
bash tools/package-macos.sh

# Package for Linux (resolves libraries via patchelf with $ORIGIN rpaths):
bash tools/package-linux.sh

# Package for Windows (copies MinGW runtime DLLs beside the executable):
bash tools/package-windows.sh
```

See [**Packaging Notes (docs/PACKAGING.md)**](docs/PACKAGING.md) for detailed packaging workflows.

---

## 📄 License

Fake2D is open-sourced under the **[MIT License](LICENSE)**. Free for personal, educational, and commercial projects.
