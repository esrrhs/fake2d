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

## Phase 8 — Engine gaps & Super Mario gameplay (1.3.x) — done

- [x] Tilemap slopes: parse `slope` ("up"/"down") and `slope_rise` tile properties; `map_slope_dir` query; `map_ground_y` linear surface height query; kinematic slope-walking controller support (showcased in `--slope-demo`: three Tiled-slope hills, a live surface overlay, and `map_slope_rise`)
- [x] Tiled parallax & Image Layer: parse `parallaxx`/`parallaxy`, `offsetx`/`offsety`, and `imagelayer` with `repeatx`/`repeaty`; interleaving layers sorted in JSON order; `map_layer_count` / `map_layer_parallax_x/y` queries
- [x] SaveStore KV persistence: JSON-based key/value storage in engine-managed save directory (`saves/`); slot validation against path traversal; `storage_load`, `storage_save`, `storage_get/set_num`, `storage_get/set_str`, `storage_has`, `storage_delete`, `storage_reset`
- [x] EntityStore native fixed-slot storage: 256 C++-owned entity slots surviving FakeLua arena resets; tag + 8 double slots + 4 string slots; generation handles; `ent_create`, `ent_destroy`, `ent_num`, `ent_str`, `ent_count`, `ent_at`, `ent_clear`
- [x] Custom GLSL shader slots: 8 slots; per-object `draw_use_shader(id)` batch switching with automatic per-frame restoration; `shader_load`, `shader_destroy`, `shader_set_float/int/vec2/vec4` (float/int/vec2/vec4 all bound to Lua)
- [x] Super Mario Bros 1-1 demo (`scripts/mario.lua` + `--mario-demo`): procedural retro pixel art (ground, brick, ?, used, pipes, coin, flag, stone, Goomba, mushroom, small & big Mario); dual-form player with variable jump / coyote / buffer; Goombas with patrol / squashing / bonk death; ? blocks and brick breaking with debris particles; mushroom growth & walk; lives/score/coins/timer HUD; flagpole sequence; deterministic attract AI with looping clears

## Phase 9 — Gamepad, sprite mirroring & window control (1.4.x) — done

- [x] Sprite UV mirroring: `SpriteBatch::DrawSpriteFlipped` (flip_x/flip_y swap UVs only — geometry and the batching key are unchanged), Renderer passthrough, `sprite_draw_flip(handle, sx, sy, sw, sh, dx, dy, dw, dh, flip_x, flip_y)` Lua binding; mario/platformer/slope demos now face the actual movement direction (the player art previously tracked `facing` but was never mirrored)
- [x] GLFW gamepad input: per-frame poll of all 16 joystick slots via the standard gamepad mapping, edge-detected buttons (`down/pressed/released`), six axes with radial stick dead-zone (0.20, rescaled) and triggers remapped to 0..1, hot-unplug reads neutral; Lua API `input_pad_connected/down/pressed/released/axis/name`; mario/platformer/slope demos playable with a controller (left stick/dpad, A jump, X/trigger run)
- [x] Window control from scripts: `window_quit` (programmatic close), `window_set_title`, `window_set_fullscreen` / `window_fullscreen` (borderless primary-monitor fullscreen with saved windowed geometry restore), backed by Engine + platform::Window; Esc / gamepad Back exits all three gameplay demos
- [x] Character walk-cycle animation: procedural multi-frame sprite strips generated from the single standing pose (no new art assets to maintain) — player strips are 4 frames (idle / stride A / stride B / airborne) at 80x28 (small) and 96x44 (big), derived by pixel-shifting the two leg/foot halves; the platformer blob gets an 80x30 squash-and-stretch strip via CPU resampling; Goombas get a 56x28 two-foot waddle strip with direction-aware mirroring. Scripts select frames from grounded/speed/air state (`walk_t` phase, SMB step-idle-step gait); mario/platformer/slope demos all animate now — the player art was previously a static pose in every state

## Phase 10 — Character clips in the engine animator (1.5.x) — done

- [x] AnimationSystem script control: `anim_pause` / `anim_resume` (freeze/continue without rewinding, unlike play/stop), `anim_set_frame(id, i)` pins the playback clock to an explicit frame, `anim_frame_index(id)` queries it; clips stay normal engine animations while gameplay owns the state machine
- [x] Flipped clip drawing: `AnimationSystem::DrawFlipped` + `anim_draw_flip(id, x, y, w, h, flip_x, flip_y)` mirroring via the same UV-swap path as `sprite_draw_flip` (no extra draw call)
- [x] Character migration off hand-rolled strip math: mario builds the two player clips once (small/big, 4 frames each, paused + per-frame `anim_set_frame`) and one looping 8 fps waddle clip per Goomba (anim id in entity slot 7, destroyed on every despawn path); platformer/slope players use the same paused-clip pattern. Lua scripts no longer compute source rectangles themselves — the animator owns clips and frame timing
- [x] SCRIPTING.md documents both styles (auto-loop decoration vs state-driven characters) and the C++-pool lifecycle rule (pair create/destroy, survives the per-frame arena reset)

## Phase 11 — Self-contained release pipeline (1.5.x)

- [x] Replace the engine-only (`FAKE2D_WITH_FAKELUA=OFF`) tag build in `.github/workflows/release.yml` with full-FakeLua self-contained archives on Linux / macOS / Windows: bundled libfakelua + OpenSSL, `scripts/`, shaders, pre-generated demo maps, and a per-platform launcher
- [x] Reusable packaging scripts under `tools/`: `package-macos.sh` (otool-driven dylib copy, `@executable_path/../lib`, real files only, ad-hoc codesign), `package-linux.sh` (ldd-resolved libs + patchelf `$ORIGIN` rpath), `package-windows.sh` (MSYS2 copy of MinGW runtime DLLs next to the exe); a dedicated Linux job generates the demo maps shared by all three packages
- [x] The shipped engine needs no compiler — TCC is disabled in `script_host.cpp` and the interpreter is the automatic fallback when GCC JIT is absent, so the packages deliberately exclude the tinycc support tree that CI's `flua` logic runs require
- [~] Verification: macOS packager validated locally on Apple Silicon (3 dylibs resolve from `@executable_path/../lib`, process reaches the GL layer); Linux/Windows packagers are syntax-checked and mirror the proven build.yml steps, pending end-to-end confirmation on the first release tag

## Phase 12 — GL2 compatibility rendering path (1.6.x)

- [x] Runtime context ladder in `platform::Window`: GLFW 3.3 core → GLFW 2.1 legacy → (macOS headless only) drawable-less CGL context + offscreen FBO, for VMs whose only renderer is the Apple Software Renderer and which expose no NSGL pixel format at all (GLFW 65545)
- [x] GL2 legacy render path selected at runtime via `glGetString(GL_VERSION)` (`glcaps::legacy_mode`): GLSL 120 default-shader variants with `glBindAttribLocation`-pinned attribute locations, no VAO (fixed-layout attribute pointers re-specified per flush, element buffer rebound explicitly), `GL_LUMINANCE` uploads for single-channel textures, forced clamp wrap for NPOT+repeat
- [x] Probe-verified on the software renderer: unsuffixed core FBO entry points dispatch correctly on legacy 2.1 contexts (FRAMEBUFFER_COMPLETE + exact-color `glReadPixels`), so `gl3.h` headers stay unchanged
- [x] Script binding fix surfaced by the new path: `anim_draw` / `anim_draw_flip` take the clip id as double — anim handles stored in EntityStore slots arrive as floats and were rejected by the previous `long long` parameter (`FakeluaToNativeLonglong failed`), which silently aborted the update mid-frame in goomba view
- [x] CI: the macOS engine job gains a real GPU smoke (scene/map/platformer/mario/slope headless runs + screenshot) through the CGL FBO path on the Apple Software Renderer, `continue-on-error` until proven stable on runners
- [x] Verification in the NSGL-restricted VM: all five demos render headless with zero `[ERROR]` lines, screenshot pixel-verified (tiles, sprites, goomba, HUD glyph atlas, lines); 24/24 headless Lua logic cases still green after the binding signature change

## Phase 13 — Physics query surface (1.7.x)

- [x] Ray casts: `PhysicsWorld::RayCast(from, to)` segment intersection against circles (quadratic, nearest root) and AABBs (slab method, entry-axis face normal); a segment starting inside a collider reports t=0 with the normal opposing the ray; nearest hit over all alive bodies returns body id, user tag, point, normal and distance, plus a buffered overload read back via `HasRayHit()`/`LastRayHit()`
- [x] Shape overlaps: `OverlapCircle` / `OverlapBox` against every alive body regardless of type or sensor flag (circle-circle, circle-box nearest-point, box-box AABB), with caller-vector and buffered overloads (`OverlapBody(i)` / `OverlapUser(i)`, safe out-of-range reads)
- [x] Full contact lifecycle: the per-step pair list becomes began / stayed / ended event streams — `StayedContacts()` and `EndedContacts()` join sorted touch snapshots via merge; ended events snapshot ids/user tags from the previous step so a destroyed body's last contacts still report; began lookup switched from linear scan to binary search
- [x] 17 Lua bindings: `phys_raycast` + 6 hit readers, `phys_overlap_circle/box` + id/user readers, `phys_stay_count/end_count` and the four stayed/ended user-tag readers; the headless fixture generator picks the new signatures up automatically (24/24 cases green)
- [x] `--phys-query-test`: headless C++ self-test with exact-value assertions (face points/normals/distances both ray directions, interior-start hits, misses, buffered readers, overlap counts incl. both-body and far-away cases, began→stayed→ended incl. the destroyed-body ended event); `--phys-bench` additionally reports per-frame raycast/overlap cost
- [x] `scripts/phys_test.lua` exercises the same surface through real bindings end to end; bodies are placed initially touching because headless frames advance by wall-clock dt and an empty scene renders in microseconds (collision response is positional and frame-deterministic); CI runs both gates on Linux, macOS (CGL headless) and Windows (Mesa llvmpipe)

---

## Non-goals (for now)

- Full 3D / PBR (Fake2D is strictly focused on high-performance 2D)
- Heavy built-in physics engine (clean external Box2D bindings can be added later)
- Complex GUI editor (lightweight data-driven scripts and tools first)
- Matching full PUC-Rio Lua standard library (FakeLua subset optimized for game hosts)
