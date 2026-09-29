# Fake2D Implementation Plan

Living checklist and architectural specification for the FakeLua-powered 2D rendering engine.

Status tags: `[ ]` todo · `[~]` in progress · `[x]` done

---

## Modern Architectural Pillars

Fake2D adheres strictly to modern 2D engine industry standards (aligned with MonoGame, Raylib, and Defold):

1. **Zero-GC Per-Frame Architecture**:
   - High-frequency game logic and temporary objects allocate from FakeLua's linear arena.
   - Per-frame `fakelua::inter::Reset(state)` guarantees deterministic 60/120+ FPS with zero GC stalls.
2. **Data-Oriented Dynamic SpriteBatching**:
   - Single dynamic streaming VBO + static pre-allocated index buffer (`0-1-2-2-3-0` quad pattern).
   - Unified 1x1 pure white texture: solid colored quads and textured sprites share the same GLSL 330 core shader without pipeline switching.
   - Future multi-key sorting (Layer → Depth/Z → TextureID → BlendMode) before flushing.
3. **Decoupled 2D Camera & Coordinates**:
   - Orthographic view-projection matrix calculation with viewport scaling, rotation, smooth panning, and zoom.
   - Full `ScreenToWorld` and `WorldToScreen` coordinate transformations.
4. **Data-Driven Scene & Assets**:
   - Spatial node hierarchy (`Transform2D`) with local/world dirty caching.
   - Texture Atlas / SpriteSheet support for single-draw-call scene rendering.
   - Live script hot-reloading via FakeLua's embedded JIT backend.
5. **Modern CI & Headless Testability**:
   - First-class support for `--headless` and `--frames N` CLI parameters for automated smoke and regression testing in virtualized CI environments without a display server.

---

## Phase 0 — Skeleton (0.1.x) — done

- [x] Replace legacy console fruit-machine demo with new project identity
- [x] CMake + CPM, GLFW, modern OpenGL clear path
- [x] `Engine` main loop (`PollEvents()` → `renderer.BeginFrame()` → `script.CallUpdate(dt)` → `Reset()` → `renderer.EndFrame()` → `SwapBuffers()`)
- [x] `ScriptHost` FakeLua bridge (`CompileFile` / `Call(update)` / `Reset`)
- [x] Standard `find_package(fakelua REQUIRED)` integration via `/usr/local` system package
- [x] `examples/hello` + `scripts/main.lua`
- [x] Bilingual README (`README.md`, `README_CN.md`) + GitHub Actions CI workflow
- [x] Optional `FAKE2D_WITH_FAKELUA=OFF` window-only skeleton for bring-up and CI
- [x] Wire richer FakeLua native object bindings (`draw_quad`, `camera_*`)
- [x] Headless backend support (`--headless`, `--frames N`) for CI without display

## Phase 1 — Draw primitives (0.2.x) — done

- [x] GLSL 330 Core shader pipeline (vert/frag) with projection matrix and uniform cache
- [x] Orthographic camera (`Camera2D`) with viewport, zoom, rotation, and coordinate conversion
- [x] `SpriteBatch` with dynamic VBO streaming, static shared index buffer, and automatic flush
- [x] `Texture2D` image loading (`stb_image`) + sampler state
- [x] 1x1 pure white fallback texture to unify solid quads and textured sprites in one batch
- [x] Draw API: `DrawQuad(rect, color)`, `DrawSprite(tex, src, dst, tint)`, `DrawSpriteRotated(...)`
- [x] Cross-platform Linux / macOS OpenGL compatibility (`GL_GLEXT_PROTOTYPES`, std int types)

## Phase 2 — Scene & assets (0.3.x) — done

- [x] `Transform2D` hierarchical spatial node tree (local transform, world matrix cache, dirty flag)
- [x] Layer and Z-ordering support
- [x] Texture Atlas / SpriteSheet parser (TexturePacker JSON format)
- [x] Resource manager (handle-based texture/atlas caching and reference counting)
- [x] Script hot-reload via FakeLua JIT backend (GCC JIT default, interpreter fallback)

## Phase 3 — Script API (0.4.x) — done

- [x] Expose native engine modules to FakeLua: `fake2d.sprite`, `fake2d.camera`, `fake2d.input`, `fake2d.time`
- [x] Input snapshot (keyboard, mouse position, button clicks) readable per-frame from Lua
- [x] Complete runnable mini-game sample (breakout clone, `scripts/game.lua`)
- [x] Documentation for FakeLua script authoring and performance best practices ([docs/SCRIPTING.md](SCRIPTING.md))

## Phase 4 — Text & audio (0.5.x) — next

- [ ] Bitmap / MSDF (Multi-channel Signed Distance Field) font renderer batched into `SpriteBatch`
- [ ] Audio clip playback integration (miniaudio or lightweight audio backend)
- [ ] CPU particle emitter with quad batching into `SpriteBatch`

## Phase 5 — Polish & Production (1.0.x)

- [ ] Multi-key batch sorting (Layer → Depth/Z → Texture ID → Blend Mode) to minimize draw calls
- [ ] HiDPI / Retina framebuffer scaling support
- [ ] Cross-platform packaging notes (Linux / Windows / macOS)
- [ ] Benchmark suite (draw calls, sprite count vs FPS, memory footprint)
- [ ] SemVer versioning policy and release automation workflow

---

## Non-goals (for now)

- Full 3D / PBR (Fake2D is strictly focused on high-performance 2D)
- Heavy built-in physics engine (clean external Box2D bindings can be added later)
- Complex GUI editor (lightweight data-driven scripts and tools first)
- Matching full PUC-Rio Lua standard library (FakeLua subset optimized for game hosts)
