# Fake2D

[English](README.md) | [中文说明](README_CN.md)

[![Build Status](https://github.com/esrrhs/fake2d/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/esrrhs/fake2d/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-brightgreen.svg)](https://en.cppreference.com/w/cpp/23)
[![OpenGL](https://img.shields.io/badge/OpenGL-3.3%20Core%20%7C%202.1-orange.svg)](https://www.opengl.org/)
[![FakeLua](https://img.shields.io/badge/Script-FakeLua-red.svg)](https://github.com/esrrhs/fakelua)
[![Version](https://img.shields.io/badge/version-1.36.0-informational.svg)](include/fake2d/version.h)

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
- **Real-Time 2D Lighting**: Point lights with soft radial falloff and hard projected shadow volumes, so a light that moves sweeps its shadows across the scene with it. One generated falloff sprite and one multiply blit, grouped by blend state — 64 moving lights against all 256 caster slots cost 4 draw calls and 0.7 ms. Casters are rects, polygons or live physics bodies, each shadow is confined to its own light's circle, and a per-frame quad budget bounds the worst case;
- **2D Skeletal Animation**: bone hierarchies posed by keyframed clips, so a walk cycle is a list of angles instead of two hundred pictures. Rigs load from JSON (a Spine-shaped schema of bones, slots, attachments, skins and clips), interpolate per channel with linear or stepped keys, cross-fade with the outgoing clip still animating underneath, fire timeline events that land on the frame they were authored for even across the loop seam, and reach live targets with two-bone and aim IK. The cost is measured, not assumed: twelve parts on one sheet are one draw call, and twelve soldiers are still one;
- **Immediate-Mode UI (UISystem)**: Ready-to-use widgets including Panels, Buttons, Labels, Sliders, Checkboxes, UTF-8 TextInputs, ScrollAreas, ProgressBars, RadioButtons, Combos, draggable Windows (with mouse deltas), Tab bars, disclosure Trees, and Tooltips.
- **Text & Platform Layer**: UTF-8 text with an on-demand glyph atlas (CJK/Japanese/Korean included, `font_load` for any TTF/TTC/OTF, fallback faces for missing glyphs), CJK per-character vs Latin word wrapping with 禁则 rules and kerning, rotated, shadowed, outlined and shrink-to-fit text, TSV localization packs switched at runtime (BOM-safe, missing-key reports), cross-platform resource/user directories, CRC-checked v2 save containers with automatic `.bak` rollback, entity snapshots through the same container, file logging with crash reports, an offline achievement mirror, a `mod.json` mod manager, and an optional Steamworks adapter (achievements / Rich Presence / language / cloud flag) that stubs out when the SDK is absent.
- **Gameplay Utilities**: deterministic splitmix64 RNG, named easing curves and frame-rate-independent `Approach`, input action maps with rebinding, window size/resizable/min-size/cursor controls, audio pitch/looping/stop, and runtime FPS / frame-ms / draw-call stats.
- **2.5D Brawler Toolkit**: a global gameplay time scale plus a hit-stop primitive (`time_set_scale` / `time_hitstop`) that freezes scripts, physics, animations and particles together; 16 numeric + 4 string slots per entity and 512 entities for full actor rosters; a 128-clip animation pool; y-sorted draw layers (`draw_set_z` over `batch_set_sorted`) for depth-correct side-scrollers.

### 5. Developer Experience & Automated Testing
- **JIT Script Hot-Reload**: GCC JIT backend with automatic interpreter fallback; edit Lua scripts live and see changes reflected instantly without restarting;
- **Headless & CI-Ready**: First-class `--headless`, `--frames N`, and `--screenshot` CLI flags allow visual regression tests and automated smoke validation in headless Linux/macOS/Windows environments;
- **29 Built-in Self-Test Suites**: 1388 assertions covering rigid-body dynamics, joints, sleeping, anti-tunneling, physics queries, filters, timesteps, tilemap objects, A\* pathfinding, render targets, UI widgets, post-FX, fog of war, 2D lighting and shadow containment, bone hierarchy and clip playback with cross-fade, IK and timeline events, audio mixing, animated tiles, UTF-8/CJK text with fallback faces, the platform layer, deterministic RNG, easing, entity snapshots, input actions, window controls, gameplay time scale / hit-stop, 16-slot entity actors, the 128-clip animation pool, the shmup sprite/background asset pipeline, the city-builder tileset/building atlas/base map, the RTS terrain/unit/building/map pipeline and the visual-novel background/portrait art (all mutation-tested);
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
│                    Platform Layer · I18n · Mods · Log · Steam               │
│          OpenGL 3.3 Core (GL 2.1 Fallback) · GLFW 3.4 (Input/Gamepad)       │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 🎮 Runnable Demos Matrix

Fake2D comes with 29 rich, self-contained demos plus an in-engine launcher for picking between them, demonstrating platformer mechanics, retro brawler and run-and-gun gameplay, cue-sport physics, city simulation, real-time strategy, dating-sim storytelling, real-time 2D lighting with projected shadows, keyframed 2D skeletal animation with IK, spatial audio, immediate UI, and graphical post-processing:

| Demo Flag | Topic | Key Highlighted Features |
|---|---|---|
| `--launcher` | **Demo Launcher (default)** | Cover-art picker for all 29 demos: category filter, mouse and keyboard navigation, and the exact command line behind each entry; picking one re-execs into it |
| `--breakout-demo` | **Breakout** | Ball physics reflections, brick destruction, particle debris, HUD text, and procedural audio chirps |
| `--mario-demo` | **Super Mario Bros 1-1** | Faithful SMB 1-1 recreation; small/big Mario forms, Goomba patrols & squashing, ? blocks, brick shatter particles, flag pole sequence, attract AI |
| `--angry-birds-demo` | **Angry Birds Sandbox** | Slingshot impulse launch, stacked towers that sleep and wake, contact-driven pig pops, camera follow, synthesized SFX |
| `--limbo-demo` | **LIMBO Jointed Sandbox** | 6-part revolute ragdoll, 9-node rope chain and revolute bridge, buoyancy water, and sever-on-death pieces that stay tethered |
| `--platformer-demo` | **Platformer Controller** | Kinematic tile platforming; coyote time, jump buffering, one-way ledges, coin pickups, smooth follow camera |
| `--beat-em-up-demo` | **2.5D Brawler (Dino Brawl)** | CC0 pixel-art fighters (Chasersgaming Renegade/Soldier) and a CC0 night-city parallax backdrop (FisherG), nearest-filtered and palette-swapped; ground-plane x/depth/height movement with y-sorted actors; 3-hit combos, kicks, spins, hit-stop, i-frames, knockdown/getup, wave spawning with a locked camera, enemy AI and a boss, plus an attract AI for headless runs |
| `--shmup-demo` | **Vertical Shooter (Raiden-style)** | CC0 pixel art by ansimuz: scrolling desert + clouds, player ship with 8-way movement and auto-fire, three upgradeable weapons (Vulcan / Laser / Missiles), bullet-clearing bombs, fighter waves with aimed/spread fire, a multi-pattern boss with a health bar, lives/score/stage loops, and an attract AI for headless runs |
| `--snooker-demo` | **Snooker & Billiards Physics** | Purpose-built billiards solver in Lua: fixed substeps with continuous contact rewind, elastic ball-ball impulses and positional separation, cushion restitution with side-spin rebound, six pockets, cloth rolling friction, follow/draw, cue-stick aiming with a ghost line and spin pad. Full frame rules (reds → nominated colour → yellow-to-black), fouls, respots, two-player scoring, and a ghost-ball pot-finding attract AI |
| `--contra-demo` | **Run-and-Gun (Contra-style)** | A jungle stage with layered palm/mountain parallax, run / variable jump / crouch and 8-direction fire, weapon capsules (Rifle, Machine Gun, Spread Gun, Laser), advancing soldiers, wall turrets, flying carriers, pits, a fortress boss with destructible turrets and a reactor core, lives, respawns, stage loops, synthesized march theme and an attract AI that runs and clears the whole stage headlessly |
| `--city-demo` | **City Simulation (SimCity-style)** | A generated 56x42 region you road, zone (housing / shops / factories), park and power yourself. Monthly simulation drives road-network connectivity, power coverage, RCI demand, three-level building growth, abandonment, pollution and a tax/upkeep budget. Pixel-correct screen-space HUD, clickable minimap, drag painting, tooltips, synthesized city theme and an attract "mayor" AI that builds a growing town headlessly |
| `--rts-demo` | **Real-Time Strategy (Red Alert-style)** | A 64x48 battle map with mirrored bases. Harvest ore with auto-cycling harvesters, build power plants / barracks / war factory / refinery / turrets, train infantry, rocketeers, tanks and more harvesters, then drag-box select and right-click to move / attack. Homing rifles, rockets and shells, team-tinted generated sprites, power-gated production, minimap, mission HUD, an enemy commander with attack waves, and an attract commander that plays the whole match headlessly |
| `--vn-demo` | **Visual Novel / Dating Sim (Tokimeki-style)** | Four heroines with AI-illustrated expression portraits, eight illustrated backgrounds and a branching Chinese-language story. A 14-day calendar with study / fitness / charm / money / stamina, free chats with three topics per girl (the third unlocks with affection), gifts, dates, a summer festival and a confession ending. Typewriter dialogue, clickable choices, affection toasts, quick save/load, and an attract player that reads the whole story headlessly |
| `--slope-demo` | **Slopes & Terrain** | Tiled slope calculation (`map_slope_dir` / `map_ground_y`), smooth angle traversal and live ground overlays |
| `--path-demo` | **Maze Chase & A\* Pathfinding** | Generated maze, staggered AI tracking, player autopilot mode, and live polyline path drawing |
| `--tile-demo` | **Animated Tiles** | Tiled tileset animations driven by a single engine clock; rippling pond water and torch flames |
| `--fog-demo` | **Fog of War** | Procedural world; multiply blend mask, cumulative exploration reveal, clickable minimap, and `rt_sample` probes |
| `--light-demo` | **2D Lighting & Projected Shadows** | Moving light sources with hard projected shadows: a carried lantern, a flickering torch, an orbiting orb, a shadowless fill, sliding crates and a physics-simulated crate that drag their shadows; 24 lights burst on one keypress, whole pass in 4 draw calls |
| `--skeleton-demo` | **2D Skeletal Animation** | A rigged soldier on a 15-bone hierarchy: a walk cycle authored as keyframed angles, an idle that breathes, aim at the cursor through two-bone arm IK, muzzle-flash and recoil, hit and death one-shots cross-faded from whatever pose was running, timeline events that spawn dust, flash and a casing at the exact frame they were authored for, skin swaps, a bone overlay, and a rank of six more soldiers marching behind him off the same asset |
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

# 4. Launch — opens the demo picker; pass --<name>-demo to skip it
./build/bin/fake2d_hello
```

> **Run it from the repository root.** `scripts/` and `assets/` are resolved
> against the working directory, so starting the binary from elsewhere opens an
> empty window. On macOS, double-clicking `run.command` does the right thing: it
> `cd`s into the repository, builds on first run, and opens the picker.

> **C++ Only Mode**: To build a windowed skeleton without linking FakeLua, pass `-DFAKE2D_WITH_FAKELUA=OFF`.

---

## 💻 CLI Usage Guide

### Shipped Art

Every game demo renders its scene from committed image assets rather than
primitives painted at first run: CC0 pixel packs (Chasersgaming, ansimuz,
FisherG), AI-illustrated key art under `assets/`, and tile sheets relit from
AI-generated materials. `tools/art_key.py` bakes the committed PNGs from the raw
generations, and `--art-test` walks the whole art tree and loads every file, so
a folder that falls out of `.gitignore` fails the build instead of silently
dropping a demo back to its painters.

### Demo Launcher

Running the binary without a demo flag opens an in-engine picker: every demo as
a card with its shipped cover art, a one-line summary, its control scheme and
the exact command line behind it.

```bash
./build/bin/fake2d_hello              # windowed, no flag -> the launcher
./build/bin/fake2d_hello --launcher   # the same, explicitly
```

UP/DOWN or the mouse selects, ENTER or a click launches, TAB cycles the
category filter (all / games / engine features), the wheel and PAGEUP/PAGEDOWN
scroll, ESC quits. The catalogue is `include/fake2d/demos.h` — the same table
the command line parses — so the menu can only ever offer a demo the host knows
how to start, and adding a row there lists it automatically. `--launcher-test`
gates that table (unique flags, known categories, script files that exist).

Picking a demo re-execs the process with `--<flag>-demo`, so every demo gets a
clean session with its own asset and audio setup. To play something else, come
back to the launcher and pick again.

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

# 2D Lighting & Projected Shadows (LEFT/RIGHT walk, SPACE burst, L toggle)
./build/bin/fake2d_hello --light-demo

# Skeletal Animation (LEFT/RIGHT walk, SPACE shoot, H hit, K die, R revive, B bones, N skin)
./build/bin/fake2d_hello --skeleton-demo

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

# 2.5D brawler (WASD move, SPACE/J punch, K kick, L spin)
./build/bin/fake2d_hello --beat-em-up-demo

# Vertical shooter (WASD fly, SPACE/J fire, K bomb)
./build/bin/fake2d_hello --shmup-demo

# Snooker (mouse aim, hold LMB to charge, arrow keys / spin pad for spin, R new frame)
./build/bin/fake2d_hello --snooker-demo

# Run-and-gun (A/D run, W aim up, S crouch, Space jump, J/X fire)
./build/bin/fake2d_hello --contra-demo

# City simulation (1-7 tools, drag to pave/zone, WASD pan, wheel zoom, Space pause)
./build/bin/fake2d_hello --city-demo

# Real-time strategy (LMB select / drag box, RMB orders, 1-5 build, 6-9 train, WASD pan)
./build/bin/fake2d_hello --rts-demo

# Visual novel / dating sim (click or Space to advance, 1-4 choose, F5/F9 save/load)
./build/bin/fake2d_hello --vn-demo
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

# Reject screenshots that saved but drew nothing (exit 2): colour floor probe
xvfb-run ./build/bin/fake2d_hello --headless --text-demo --screenshot text.png --shot-min-colors 12

# Run the 29 built-in engine self-test suites (verified via exit codes)
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
xvfb-run ./build/bin/fake2d_hello --light-test         # Light pools, shadow containment, budget
xvfb-run ./build/bin/fake2d_hello --light-bench        # Lighting stays batched: calls flat in lights
xvfb-run ./build/bin/fake2d_hello --skeleton-test      # Bone hierarchy, clips, cross-fade, IK, events
xvfb-run ./build/bin/fake2d_hello --audio-test         # Mixer buses, envelopes, streamed music
xvfb-run ./build/bin/fake2d_hello --font-test          # UTF-8/CJK glyph atlas, wrapping, rotation, shrink
xvfb-run ./build/bin/fake2d_hello --platform-test      # i18n packs, user paths, save v2, Steam stub, UI text
xvfb-run ./build/bin/fake2d_hello --gameplay-test      # RNG, easing, entity snapshots, actions, window
xvfb-run ./build/bin/fake2d_hello --combat-test        # Time scale, hit-stop, 16-slot actors, clip pool
xvfb-run ./build/bin/fake2d_hello --shmup-test         # Shooter sprite sheets, nearest filtering, missing files
xvfb-run ./build/bin/fake2d_hello --snooker-test       # Shaded ball sprite, alpha/shading probes, loader rejection
xvfb-run ./build/bin/fake2d_hello --contra-test         # Soldier strips, blast sheet, moon/glow probes, loader rejection
xvfb-run ./build/bin/fake2d_hello --city-test           # City tileset/building atlas, base map roads/lake, loader rejection
xvfb-run ./build/bin/fake2d_hello --rts-test            # RTS terrain/unit/building sprites, battle map pads/ore/water
xvfb-run ./build/bin/fake2d_hello --vn-test             # VN backgrounds/portraits, alpha probes, loader rejection
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
├── run.command              # macOS: double-click to build and open the picker
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
│   ├── lighting.h           # Point lights with falloff and projected shadows
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
│   ├── skeleton.h           # Bone hierarchies, keyframed clips, IK & skins
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
│   ├── light_demo.lua       # 2D lighting & projected shadows
│   ├── skeleton_demo.lua    # Skeletal animation & rig posing
│   ├── fx_demo.lua          # Bloom post-processing & split-screen
│   ├── post_demo.lua        # Color grading & minimap bake
│   ├── rt_demo.lua          # Offscreen FBO trails
│   ├── audio_demo.lua       # Mixing console & streaming benchmark
│   ├── spatial_demo.lua     # 3D positional audio
│   ├── text_demo.lua        # UTF-8/CJK, i18n, paths, save v2 & Steam
│   ├── snooker_demo.lua     # Substepped snooker physics & full frame rules
│   ├── contra_demo.lua      # Contra-style run-and-gun on a jungle stage
│   ├── city_demo.lua        # SimCity-style zoning, economy & growth simulation
│   ├── rts_demo.lua         # Red Alert-style bases, harvesters, units & combat
│   ├── vn_demo.lua          # Tokimeki-style visual novel & dating sim
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
