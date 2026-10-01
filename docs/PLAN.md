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

## Phase 4 — Text & audio (0.5.x) — done

- [x] Bitmap font renderer: TTF glyph atlas via stb_truetype (runtime rasterized, printable ASCII) with an embedded public-domain 8x8 fallback font; glyph quads submitted straight into `SpriteBatch`
- [x] Audio clip playback integrated on miniaudio: 44.1 kHz stereo device, in-memory mono float clips, 32-voice one-shot pool; audio-less hosts (headless CI) degrade to no-ops
- [x] CPU particle system: fixed-slot emitters + 8192-particle ring pool (no per-frame allocation), life/speed/direction/size/gravity/drag/spin config, rotated tinted white-texture quads batched into `SpriteBatch`
- [x] Native script API for all three (`draw_text` / `text_width`, `audio_play` / `audio_enabled`, `part_*`); breakout sample gained HUD/banner text, SFX and brick debris
- [x] Headless visual verification: `--screenshot path` framebuffer capture (glReadPixels + PNG)

## Phase 5 — Polish & Production (1.0.x) — done

- [x] Multi-key batch sorting (Layer → Depth/Z → Texture ID → Blend Mode): deferred command buffer in `SpriteBatch`, opt-in sorted mode; scene graph renders through it; alpha/additive blend modes
- [x] HiDPI / Retina framebuffer scaling: physical framebuffer size + content scale from GLFW, orthographic projection in device pixels while game coordinates stay logical points, live resize/DPI-change tracking
- [x] Benchmark suite: `--bench` measures flush time and draw calls for immediate vs sorted batching (768 sprites / 8 textures: ~674 → 8 draw calls, ~1.35 ms → ~0.10 ms on the reference machine)
- [x] Cross-platform packaging notes (Linux / Windows / macOS): [PACKAGING.md](PACKAGING.md)
- [x] SemVer (`MAJOR.MINOR.PATCH` in CMake + `version.h`) and tag-driven release automation (`.github/workflows/release.yml`)

## Phase 7 — Platformer & scale (1.2.x) — done

- [x] Physics broadphase: uniform spatial hash grid replacing O(n²) pair scanning (conservative bounding-circle insertion, deduped candidates), toggleable; `--phys-bench` shows 600 dynamics + statics at ~1.66 ms → ~0.28 ms/step (5.9x)
- [x] Line primitives: dedicated GL_LINES stream for segments / rectangle outlines / circle outlines, flushed after the quads each frame
- [x] Camera trauma shake: squared-trauma decay with layered-sine noise driving offset + slight rotation; deterministic for reproducible headless runs; `camera_shake()`
- [x] WAV loading: PCM 8/16-bit mono/stereo chunk parser, downmix + linear resample to the device rate; one looping music voice (`audio_music` / `audio_music_stop`)
- [x] Kinematic platformer CharacterController on tilemaps: accel/friction, variable-height jumps, coyote time, jump buffering, one-way platforms (`oneway` tile property), substepped swept movement; complete `--platformer-demo` (scrolling follow cam, tile-rewrite coin pickups, goal flag, attract AI, looping WAV music, debug overlays)

## Phase 6 — Gameplay foundation (1.1.x) — done

- [x] Lightweight built-in 2D physics: circle/AABB colliders, static/dynamic bodies, gravity & restitution, 4-substep integration, circle/box/box-circle pairs, sensor triggers and de-duplicated "contact began" events (zero external dependencies)
- [x] Sprite frame animation: fixed-pool clips of texture regions, fps / loop / one-shot + finished polling, engine-driven updates, batched drawing
- [x] Tilemap: Tiled orthogonal JSON loading, per-layer sorted batched rendering (gid flip-flag masking), gid/solid queries, `solid` tile-property parsing and one-call `CreateStaticColliders` into the physics world
- [x] UI basics: anchor-laid Panel / Label / Button (5 anchors), hover & pressed states, click-on-release polling; `MouseReleased` added to the input snapshot
- [x] Samples & docs: animated spinning coin in breakout, `--map-demo` (generated Tiled level + 24 bouncing bodies + anchored RESET button), scripting guide chapter, CI demo step

---

## Non-goals (for now)

- Full 3D / PBR (Fake2D is strictly focused on high-performance 2D)
- Heavy built-in physics engine (clean external Box2D bindings can be added later)
- Complex GUI editor (lightweight data-driven scripts and tools first)
- Matching full PUC-Rio Lua standard library (FakeLua subset optimized for game hosts)
