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

## Phase 14 — Tiled object layers, data-driven level markup (1.8.x)

- [x] Object-group parsing in `Tilemap`: `objectgroup` layers with rectangle/point/ellipse/polyline/polygon shapes, per-object id/name/class (legacy `type` accepted)/x/y/w/h/rotation/visible, and Tiled custom properties (bool/int/float/string; file/color degrade to strings); polyline/polygon vertices are converted to absolute world coordinates at load time
- [x] C++ query surface: `LayerTypeAt` (0 tile / 1 image / 2 object / -1), `FindLayer(name)`, `ObjectsAt(layer)`, typed property readers with defaults (`PropBool/PropNum/PropStr`); object groups are skipped by every tile traversal (solid/one-way/slope/ground queries, gid reads/writes, draw)
- [x] 20 Lua bindings: `map_find_layer`, `map_layer_type`, and the `map_obj_*` family (count/id/name/class/x/y/w/h/rotation/visible/shape/point_count/point_x/point_y/has_prop/prop_bool/prop_num/prop_str)
- [x] Mario demo migrated off marker tiles: the host-generated `assets/mario.json` carries a `"spawns"` object group (seven goomba points + one `player_start`); `scripts/mario.lua` reads it once at boot into persistent entity-store slots and respawns from those slots on every level reset — the goomba marker gid is gone from both the generator and the script
- [x] `--map-object-test`: headless C++ self-test, 49 assertions on an inline map (every shape, property kinds and type-mismatch defaults, rotation/visibility defaults, layer lookup/count/type, out-of-range guards) plus an end-to-end check against the real generated mario map (8 objects, 7 goombas, zero leftover marker gids)
- [x] Headless fixtures extended with object-layer stubs; all map/layer/object metadata is packed into a single integer-keyed table because the concatenated chunk's main function sits near FakeLua's 255-register ceiling (mario spawn reading was likewise extracted into its own `read_spawns()` function); a new `900207` smoke assertion pins the seven goomba spawns — 24/24 cases green across tcc/gcc/interpreter
- [x] Verification: `--map-object-test` 49/49, 24/24 logic fixtures, all five demos headless with zero script errors; mario screenshot pixel-confirmed (player on the object-layer start point, goomba spawned on the ground)

## Phase 15 — Collision filtering (1.9.x)

- [x] Category/mask bitmasks on `BodyConfig` (`category` default bit 0 = 1, `mask` default 0xFFFF) plus the `CategoriesMatch(a, b)` predicate: a pair interacts only when each body's category passes the other's mask, so every existing scene behaves unchanged
- [x] Filtering applied in both broadphase paths (brute-force sweep and spatial-hash grid) before a candidate pair is inserted, so filtered pairs cost nothing beyond one bitmask test
- [x] Masked query surface: `RayCast` / buffered `RayCast`, `OverlapCircle` / `OverlapBox` (caller-vector and buffered overloads) take an optional `mask` (default 0xFFFF) and skip bodies whose category does not intersect it
- [x] 9 Lua bindings: `phys_create_box_cat` / `phys_create_circle_cat` (creators with an explicit filter), `phys_set_category` / `phys_set_mask` (runtime mutation — widening a mask re-enables an already-overlapping pair), `phys_category` / `phys_mask` readers, and `phys_raycast_cat` / `phys_overlap_circle_cat` / `phys_overlap_box_cat`
- [x] `--phys-filter-test`: headless C++ self-test, 14 assertions (default pair collides, category/mask mismatch never contacts and falls through, one-way mask rejection, runtime mask widening re-enables the pair, masked vs unmasked ray casts and overlaps incl. the buffered overload)
- [x] `scripts/phys_test.lua` extended with the same surface through real bindings: an intentionally overlapping filtered pair (mismatch → no began contact across 15 frames), masked ray/overlap reads, then `phys_set_mask` widening → the pair reports began; new log codes 1050–1059 (assertions) and 2004–2006 (final tally)
- [x] CI: `--phys-filter-test` runs alongside `--phys-query-test` on Linux, macOS (CGL headless) and Windows (Mesa llvmpipe)

## Phase 16 — Fixed timestep & camera tooling (1.10.x)

- [x] Fixed-step physics accumulator in the engine loop (`EngineConfig::fixed_dt`, default 1/60, `max_physics_steps` spiral-of-death guard, frame-delta clamp): the simulation advances in whole steps regardless of frame rate; `--fixed-hz N` / `--variable-step` CLI overrides
- [x] Zero-dt contact-only pass in `PhysicsWorld::Step`: frames that skip a whole step still report a correct began/stayed/ended set (no stale or replayed events); script teleports are picked up too
- [x] Render interpolation source: bodies keep their previous step position, `PhysicsWorld::SetInterpAlpha` + `SmoothPosition`, Lua `phys_smooth_x` / `phys_smooth_y`; engine introspection `time_sim_dt` / `time_steps` / `time_alpha`
- [x] Camera follow toolkit on `Camera2D` (documented top-left position semantics): world bounds clamp (`SetBounds`, level-smaller-than-view centring), follow deadzone, frame-rate-independent exponential smoothing, SMB-style forward-only scroll lock, `SnapTo` for level resets
- [x] 8 Lua bindings: `camera_set_bounds` / `camera_clear_bounds` / `camera_deadzone` / `camera_smoothing` / `camera_forward_only` / `camera_follow` / `camera_snap` (+ existing position getters reused as readback)
- [x] Demo migration: platformer uses smoothed engine follow + bounds; mario and slope use the forward-only scroll lock (behaviour-identical to their hand-rolled SMB rules, verified by screenshot); every reset snaps the camera back
- [x] `--timestep-test`: headless C++ self-test, 23 assertions (zero-dt pass semantics incl. teleport ended events, interpolation alpha endpoints/midpoint/clamp/stale handles, bounds clamping both corners + centring, deadzone hold/catch-up/settle, rate-0 snap, forward-only lock)

## Phase 17 — Scissor, nine-slice & text layout (1.11.x)

- [x] Scissor clip rects: `SpriteBatch` applies `glScissor` around every flush (immediate and sorted runs); `Renderer::SetClipRect` converts logical points (top-left origin) to GL window coords per the HiDPI content scale, and setting/clearing flushes the pending batch first so the rect clips exactly the draws submitted while active; clip resets at `BeginFrame`
- [x] Nine-slice panels: `SpriteBatch::DrawNineSlice` (corners keep source pixel size, edges stretch on one axis, center on both; degenerate rects shrink instead of drawing inside-out) drawn as 9 ordinary quads, so batching/sorting/shaders behave unchanged; `draw_nine_slice[_white]` bindings
- [x] Text layout: `Font::DrawTextEx` with 0/1/2 left/center/right alignment and greedy word wrap (long words hard-place on their own line, `\n` honored), returning the y below the last line for block stacking; `draw_text_ex` binding
- [x] Visual verification via headless screenshot probes: stretched dialog frame with wrapped/aligned text inside, a quad clipped to an exact sub-rect, and `end_y`-stacked text — pixel-checked on the Apple software renderer
- [ ] Remaining scope for a later pass: render targets / FBO (post-processing, minimaps, split-screen), more UI widgets (slider / checkbox / text input / scroll container)

## Phase 18 — Tilemap A* path finding & maze chase demo (1.12.x)

- [x] `Tilemap::FindPath(start, goal, diagonal, max_nodes)`: A* over the solidity grid (octile heuristic, binary heap), 8-way with **corner-cut prevention** (both orthogonal neighbours must be open) or strict 4-way; world endpoints snap to cells; endpoints that are solid or out of bounds fail. Results are **string-pulled** (LOS-based waypoint reduction; 4-way keeps every cell for tile-stepping movement) and buffered as world-space cell centres via `PathPointCount()` / `PathPointAt()` / `ClearPath()`
- [x] 6 Lua bindings: `map_find_path` / `map_path_count` / `map_path_x` / `map_path_y` / `map_clear_path`; the waypoint index is a double (indices come from counters / EntityStore slots, same float-vs-int lesson as `anim_draw`)
- [x] `--path-test`: headless self-test, 25 assertions (open-field pull to 2 waypoints, 4-way staircase preserved, detour through a gap, sealed goal, corner-cut refusal + legal-diagonal acceptance, solid/OOB endpoint guards, node cap, ClearPath)
- [x] `--path-demo` (maze chase): deterministic fixed-seed maze (recursive backtracker + 30% loop knocking, exported as Tiled JSON with a 3-tile sheet and a `spawns` object layer), three chasers that re-path to the player on staggered timers, player autopilot to the goal whenever there is no input (attract AI), live path polylines (`draw_line`, toggle with P), catches/clears counters, Phase 16 camera follow + bounds
- [x] **Fix: per-frame contact events were dropped on multi-step frames.** The fixed-step accumulator runs `PhysicsWorld::Step` several times per rendered frame, but every `Step` cleared the began/stayed/ended buffers, so an event raised on an earlier step of the frame was replaced before gameplay (which reads once per frame) could see it — `scripts/phys_test.lua` missed `began` and reported `2001/2002/2003` on roughly half of all runs, depending purely on frame-time jitter. `PhysicsWorld` now has `SetEventAccumulation(true)` + `ClearFrameEvents()`: the engine clears once per frame and every step *appends* (de-duplicated per slot pair), so no event is lost. Off by default, so hosts that step once per frame (and the C++ tests) keep the classic "buffers describe the last step" behaviour. Regression covered by 7 new `--timestep-test` assertions (30 total)
- [x] Verification: `--path-test` 25/25; `--path-demo` headless smoke with pixel-checked screenshots (autopilot progress, chasers converging); `phys_test.lua` 12/12 consecutive headless runs green; all gates re-run green

---

## Phase 19 — Offscreen render targets (FBO) (1.13.x)

- [x] `RenderTarget` (FBO + one RGBA colour texture): `Create(w, h)` / `Destroy()` / `Bind()` / `Unbind(window_fbo)` / `ReadPixel(x, y)`; size is clamped to 1..8192 and an incomplete FBO is reported as a failure instead of a silent black surface. `RenderTargetLibrary` is a fixed 8-slot pool addressed by a 1-based id (0 = the window), the same handle scheme as `TilemapLibrary`
- [x] `Renderer` integration: `BeginRenderTarget(id)` flushes the pending batch, swaps in a **canvas camera** (top-left origin, 1 unit = 1 pixel, the target's own size) and sets the viewport; `EndRenderTarget()` flushes and restores the window; `EndFrame()` releases a target the script left bound; `ClearActiveTarget()` and three `DrawRenderTarget()` overloads (full / scaled / sub-rect). Draws inside a target use `ActiveViewProjection()`, so a shader switch mid-target keeps the canvas transform
- [x] **Two platform traps handled:** (1) a headless context presents through *its own* offscreen FBO, so "the screen" is not framebuffer 0 — `Renderer::Init` captures the bound id and `Unbind` restores exactly that; (2) sampling an FBO texture flips V (FBO rows are bottom-up, sprite UVs sample top-down), so `DrawRenderTarget` draws with `flip_y`. Both are covered by assertions (`--rt-test`), the V-flip one by blitting one target into another and checking that an off-centre pattern keeps its orientation
- [x] 10 Lua bindings: `rt_create` / `rt_destroy` / `rt_valid` / `rt_width` / `rt_height` / `rt_begin` / `rt_end` / `rt_clear` / `rt_draw` / `rt_active`. `rt_clear` acts on the *bound* surface (the screen when none is), which is what "fade the previous frame" needs
- [x] `--rt-test`: 38 headless assertions (creation guards, nesting refusal, unknown/destroyed ids, readback of an asymmetric pattern, V-orientation through a target-to-target blit, sub-rect blit, idempotent destroy, `EndFrame` auto-release, and a screenshot proving the window framebuffer survived the round trip)
- [x] `--rt-demo`: bake-once background (grid + deterministic starfield + baked captions, blitted at two sizes from one texture) beside an accumulating feedback trail (the target is faded, never cleared) with an immediate-mode pane running the same motion for contrast. Pixel probe: 8128 bright px at full size vs 1263 at half size; 669 trail px vs 58 immediate px
- [x] **Fix (pre-existing):** `Input::NewFrame` polled every key code up to 352, but `glfwGetKey` rejects anything below `GLFW_KEY_SPACE` or above `GLFW_KEY_LAST` — 35 `GLFW_INVALID_VALUE` errors per frame on every platform, flooding CI logs. The poll now covers exactly `[GLFW_KEY_SPACE, GLFW_KEY_LAST]` and `kKeyCount` matches `GLFW_KEY_LAST + 1` (349)
- [x] Verification: `--rt-test` 38/38; `--rt-demo` headless smoke + pixel-checked screenshot; all gates re-run green

---

## Phase 20 — UI widgets: slider, checkbox, text input, scroll (1.14.x)

- [x] `UISystem` rework: the per-key state struct is now a full `WidgetState` (hover / pressed / dragging / grab offset / text) shared by all widgets, plus a `focused_` key for text fields. All widgets stay immediate-mode and string-keyed; values live in the caller
- [x] `Slider(key, anchor, x, y, w, h, value, min, max)`: drag the thumb (grab-offset preserved so it never jumps) or click the track to jump; returns the value clamped to `[min, max]` (input clamped too, zero-width range degenerates to min)
- [x] `Checkbox(key, anchor, x, y, size, checked, label, text_scale)`: square box + tick when checked, optional right-side label; returns true exactly on the click frame — the caller owns the toggled value (same contract as `ui_button`)
- [x] `TextInput(key, anchor, x, y, w, h, max_chars)`: click to focus (which unfocuses every other field; clicking elsewhere drops focus), typed codepoints append and backspace pops — both working on **whole UTF-8 codepoints**, so multi-byte characters never corrupt. `max_chars` counts characters, not bytes; 0 = unlimited. Content is owned by the UI system (scripts cannot hold a mutable string): `TextValue(key)` / `SetTextValue(key, text)` / `FocusedTextInput()`. Backed by the new `Input` char callback (`TextCodepoints()` / `HasTextInput()`)
- [x] `ScrollArea(key, anchor, x, y, w, h, content_height, scroll_y)`: wheel while hovered (`kWheelStep` = 40 px) and a draggable right-edge thumb whose size reflects the visible fraction; returns the offset clamped to `[0, content_height - h]` (0 when the content fits, divide-by-zero guarded). **The caller clips the content** with `draw_set_clip` and draws items at `y - scroll_y` — the widget only owns the offset and the scrollbar
- [x] `DrawFittedText` helper (returns the pen x, single draw per field) plus a shared palette for tracks/thumbs/frames; `IsHovered(key)` exposed for cursor feedback
- [x] 8 Lua bindings: `ui_slider` / `ui_checkbox` / `ui_text_input` / `ui_text_get` / `ui_text_set` / `ui_scroll` / `ui_hovered` / `ui_text_focused`. FakeLua's strict arity means optional C++ defaults must be passed explicitly from scripts
- [x] `--ui-test`: 30 headless assertions (slider clamping at both ends + zero-width range, grab-offset drag, checkbox toggle-frame semantics, UTF-8 append/pop/count with a 4-byte emoji and per-character max_chars, focus bookkeeping, scroll clamping incl. short/empty content and the thumb-height guard, per-frame quad accounting for every widget, placeholder-vs-text draw evidence)
- [x] `--ui-demo`: three sliders with live percentage captions, three checkboxes with a "ticked" summary, a 12-char text field with a CLEAR button, and a 24-row scroll list that culls off-screen rows
- [x] Verification: `--ui-test` 30/30; `--ui-demo` headless smoke + pixel-checked screenshot (layout overlap between the slider captions and the text-field column found and fixed via screenshot probes); all gates re-run green

---

## Phase 21 — Post-processing & baked minimap (1.15.x)

- [x] **`Renderer::ActiveCamera()`**: the camera of the surface actually being drawn into (the target's canvas camera while a target is bound, else the window camera). `Tilemap::Draw` now uses it — previously a tilemap drawn inside a target followed the *window* camera, so `map_draw` could not render a camera view offscreen. Regression-covered by drawing with the window camera scrolled far away and probing that the bake still lands at the canvas origin
- [x] **`Renderer::SetTargetCamera(x, y)` / `rt_camera(x, y)`**: aim the bound target's canvas camera at a world offset — the piece that makes a whole-world post pass possible (`map_draw` inside the target then renders exactly what the game camera sees). Flushes and restarts the batch so later draws pick up the new transform; refused while no target is bound
- [x] **`Tilemap::DrawScaled(renderer, scale)` / `map_draw_scaled(id, scale)`**: the whole map at a uniform scale about its origin, parallax and layer offsets dropped, image layers skipped — the minimap bake. The per-layer loop was extracted into a shared `DrawTileLayer` helper (scale + optional camera) so `Draw` and `DrawScaled` cannot drift apart
- [x] **`Renderer::PresentRenderTarget(id, shader, tint)` / `rt_present(id, shader)`**: blit a target across the whole of the active view (the window, or another target for a ping-pong pass) with a shader override scoped to that one blit — `ActiveShader()` is untouched, so the rest of the frame keeps its program. Two extra flushes per pass, same contract as `UseShaderById`
- [x] `assets/post_grade.{vert,frag}`: presentation-grade fragment stage (desaturate, brightness lift, vignette, scanlines) where **every uniform defaults to "unchanged"** — an untouched shader is a straight blit, which is also what makes the pass assertable
- [x] `--post-test`: 44 headless assertions, all pixel probes (a generated two-tile tileset + an inline Tiled JSON): bake at scale 1 and 0.5, scale 0 no-op, the ActiveCamera regression, `rt_camera` aiming, plain-present coverage of a larger destination, identity-through-default-uniforms, +0.25 brightness, full desaturate, scanline row alternation, self/window/unknown-id guards, `ActiveShader` isolation, and the window framebuffer surviving a present. Mutation-checked: reverting `Draw` to the window camera, ignoring `DrawScaled`'s scale, and sizing the present quad from the source each fail the suite
- [x] `--post-demo`: the world (mario.json when generated, else a procedural skyline) drawn into a full-screen target and presented through the grade shader; the HUD is drawn *after* the present so it stays ungraded. Right column: post/greyscale checkboxes + lift/vignette/scanline sliders driving the uniforms live; a baked minimap with the camera's viewport rectangle clipped to the pane; SPACE/G hotkeys; graceful no-FBO fallback draws straight to the screen
- [x] **Fix (pre-existing):** `scripts/ui_demo.lua`'s footer sat at y=660 on a 960x540 logical canvas — invisible, but the demo still exited 0 and the earlier screenshot check eyeballed the picture instead of the bounds. Layout pulled inside the canvas and the check is now a pixel bounding-box probe; the same sweep found every other demo either inside (scene, rt, ui) or intentionally full-bleed (map/platformer/mario/slope)
- [x] Verification: `--post-test` 44/44; `--post-demo` headless smoke + pixel-probed screenshot (panel overflow past x=960 and an unclipped viewport rect found and fixed by probe); all gates re-run green

---

## Phase 22 — Audio mixer (1.16.x)

- [x] **Mixer separated from the device**: the Phase 4 audio path gated everything behind `IsEnabled()`, so buses/envelopes/pause were untestable headless. `AddClip`/`AddClipWav`/`Play`/`PlayMusic`/`Mix` now run with or without a device — clips are memory, mixing is arithmetic — and only device creation/consumption differs. `EnsureGameAudio` dropped its `IsEnabled()` early-return so every demo registers its clips identically on all hosts
- [x] **Three buses** (`AudioBus::Master/Sfx/Music`): every voice is multiplied by its own bus and by master (`effective = voice × bus × master`, both clamped to 0–1). One-shots route through sfx, music through music, so ducking the music bus cannot touch SFX and master 0 silences everything
- [x] **Per-voice gain envelopes** (`gain_from`/`gain_to`/`fade_pos`/`fade_dur`/`fade_ends_voice`): fade-ins on start, linear ramps between any two gains, and fade-outs that retire the voice when they complete. `PlayMusic(name, volume, fade)` with `fade` > 0 **cross-fades**: the outgoing track ramps down and stays audible (still `IsMusicPlaying()`, but `MusicName()` already reports the new track) until its ramp finishes and frees the voice; `fade` = 0 reuses the voice in place
- [x] **`StopMusic(fade)` / `FadeMusicTo(volume, seconds)` / `SetMusicVolume`**: fade-out stop (still audible during the ramp), arbitrary gain ramps (voice retirement only when the ramp targets 0), and immediate set that cancels any ramp
- [x] **`SetPaused`/`IsPaused`**: freezes the mix — silence out, `Peak()` forced to 0, and neither sample cursors nor fade positions advance, so resuming continues exactly where it stopped (asserted sample-exactly with a ramp clip)
- [x] **`Peak()`**: loudest sample of the last mixed block for VU meters; tracks the bus/master gains, reads 0 headless. **`Mix(out, frames)` is public** so tests can drive the mixer directly
- [x] 10 new Lua bindings: `audio_music_fade`, `audio_bus_set/get`, `audio_pause(paused)`, `audio_paused`, `audio_music_playing`, `audio_music_name`, `audio_peak`; `audio_music`/`audio_music_stop` gained the `fade` argument (strict arity: `platformer_demo.lua` updated)
- [x] `--audio-test`: 51 headless assertions driving `Mix()` on standalone never-`Init()`ed engines — bus math incl. clamping and bus isolation, one-shot lifetime vs the voice-pool cap, fade-in envelope sampled at ¼/½/settled, cross-fade level-hold and retirement of the outgoing voice, fade-out vs zero-fade stop, ramp-then-set, pause freeze/resume with no cursor drift, peak tracking, WAV decode (22050 Hz resample path) and junk-file rejection
- [x] `--audio-demo`: mixer front panel — bus sliders with live captions, two synthesized looping themes (A minor vs B major arpeggios, no binary asset), cross-fade and level sliders, SFX buttons, pause checkbox, a 24-segment VU meter fed by `audio_peak`, 1/2/S/P hotkeys, and a "no device" hint so headless runs are self-explanatory
- [x] Verification: `--audio-test` 51/51; `--audio-demo` headless smoke + pixel-checked screenshot (all widgets inside the 960×540 canvas); all gates re-run green
- Note: the cross-fade test initially flapped because the scenario mixed exactly to the 1.0 s mark, leaving the outgoing ramp a hair short of completion — the suite now mixes past it with slack, which also documents the envelope's boundary behaviour

---

## Phase 23 — Positional audio (1.17.x)

- [x] **`PlayAt(name, volume, x, y)`**: one-shots placed in the world. Placement lives *outside* the gain envelope (`atten` 0–1 folded in at mix time), so rolling a sound off with distance never disturbs a fade, and buses/master still multiply on top (`effective = voice × atten × bus × master × pan_l/r`)
- [x] **Distance rolloff**: linear to silence at `SetListenerRange(d)` (default 1000 world units, roughly a screen; ≤ 0 refused). **Pan** is horizontal only and reaches full separation at **half** the range — being hard-panned must not coincide with being inaudible, so a voice at the edge of the stereo image still has half its level
- [x] **Pan law is a linear crossfade, documented**: a centred voice keeps full level on both channels (every existing mix is centred and must not change level), a hard-panned one mutes the far side. Constant power would have made the default state louder by √2 and silently broken every existing level expectation
- [x] **`SetListener(x, y)` re-evaluates voices that are already sounding** — walking past an emitter sweeps its sound across the ears instead of leaving it parked where it started. Unplaced (`Play`) voices ignore the listener entirely, and a reused voice slot wipes its predecessor's placement (`ResetVoice`), so a plain `Play` can never come out panned
- [x] **`PeakLeft()`/`PeakRight()`**: the per-channel VU feed; the difference between them *is* the stereo image. `Peak()` stays the scalar max
- [x] 5 new Lua bindings: `audio_play_at`, `audio_listener`, `audio_range`, `audio_peak_l`, `audio_peak_r`; fixtures stub list extended in step
- [x] `--audio-test` grows 51 → **74 assertions**, all offline through the public `Mix()`: centred emitters even on both channels, hard pan muting one side, partial pan trimming it, distance-only falloff past full separation, out-of-range silence (voice still counted), listener movement re-evaluating live voices both ways, unplaced voices immune, sfx bus still applying on top, default/guarded range values, per-channel peaks
- [x] `--spatial-demo`: a top-down world with four periodic emitters and a listener moved by WASD/arrows/mouse; emitter rings dim exactly as their reach does, the listener's range circle shows where sounds die, and an L/R meter pair plus a balance indicator run off the per-channel peaks
- [x] Verification: `--audio-test` 74/74; `--spatial-demo` headless smoke + pixel-checked screenshot (world panel clipped so range rings cannot escape it — `draw_line` primitives ignore the scissor, so the ring is drawn as scissored dots); all gates re-run green
- Note: two placed one-shots on one engine **sum**, which initially broke a "single emitter" expectation — each placement scenario gets its own engine in the suite

---

## Phase 24 — Multi-pass FX chain (1.18.x)

- [x] **`rt_copy(src, dst, shader)` / `Renderer::CopyRenderTarget`**: one ping-pong pass — blit `src` across the whole canvas of target `dst` through an optional shader. Equivalent to `rt_begin(dst)` + `rt_present(src)` + `rt_end()`, but the failure modes are pre-refused: `src == dst` (a surface cannot be sampled while it is drawn into), the window as either side, and any nesting. `dst` is not cleared (the blit covers the whole canvas)
- [x] **`rt_resize(id, w, h)` / `Renderer::ResizeRenderTarget`**: reallocate a target, contents lost — the enabler for half/quarter-resolution passes (a blur over a quarter of the pixels costs a quarter of the taps). Refused while that target is bound; **the size is validated before `Create` is called**, because `Create` destroys the old attachment first and a refused resize used to leave the target destroyed (found by the test, fixed in the implementation)
- [x] `assets/fx_blur.{vert,frag}`: a separable 9-tap Gaussian. `u_dir` is the step **in UV** (the caller owns the resolution, the shader does not need to know it), so `u_dir = 0` samples one texel nine times = a straight copy — the "0 = unchanged" uniform rule again, and what makes each pass measurable. `u_dim` rides along (0 = unchanged) so the composite gain needs no second program
- [x] **`--fx-test`: 47 headless pixel-probe assertions**: a plain copy is an identity (dot lands at the same canvas position, background too), `u_dir = 0` through the blur shader is still an identity, one pass smears only along its direction (2 px steps: x=+8 lit, y=+8 untouched), two ping-pong passes cover both axes with a small-but-positive corner, self/window/unknown/nested copies refused and a refused copy leaves the destination untouched, resize reports its size, guards (zero size, unknown id, bound target, window) and a refused resize keeps the old attachment, a half-size copy rescales the dot to half coordinates, a grade pass composes on top of a chain, and the window framebuffer survives
- [x] **`--fx-demo`**: a bloom over a procedural skyline — scene into a full-res target, downsample into a half-res pair, blur H then V twice (two ping-pong pairs for a wider glow), present the sharp scene through the grade shader (vignette), then composite the dimmed glow additively. Radius/strength sliders live-drive `u_dir`/`u_dim`; a SPLIT button (or `s` key) swaps to a split-screen mode that renders the same world from two cameras clipped to their halves
- [x] Gotchas written down where they bit: a shader present (`rt_present(id, shader)`) restarts the batch, which **resets blend mode** — an additive composite must use `rt_draw` after `draw_set_blend(1)`, not a shader present; and FakeLua int64 params (`rt_create`, `rt_resize`) reject float literals, so canvas constants are `960 + 0`, not `960.0 + 0.0`
- [x] Verification: `--fx-test` 47/47; `--fx-demo` headless smoke + pixel-checked screenshots of both modes; mutation checks below; all gates re-run green

---

## Phase 25 — UI widgets round 2 (1.19.x)

- [x] **`ProgressBar(anchor, ox, oy, w, h, value, fill, track)` / `ui_bar`**: a pure draw — frame, track, and a fill of `w * t` from the left with the value clamped to 0..1, so a bar fed 1.4 or -0.2 still looks like a bar. No key, no state, nothing to click. The clamp is pixel-asserted **outside** the track (x=120 for over-range, x=4 for under-range): a probe inside the track reads filled either way and the assertion would never fail — the initial 108/30 probes were exactly that vacuous, caught by the mutation check
- [x] **`RadioButton(key, anchor, ox, oy, size, selected, label, scale)` / `ui_radio`**: one row — a square indicator (inset block when selected, like the checkbox tick) plus a right-side label. Click semantics match `ui_button` exactly: release inside clicks, **releasing outside cancels**. Selection is caller-owned, so a group is N rows and one index, and an already-selected row still reports the click
- [x] **`ComboBegin(...)` / `ui_combo` and `ComboItem(...)` / `ui_combo_item`**: a combo is composed, not encapsulated — the header draws the current text plus a two-step triangle chevron (down when closed, up when open), and the popup is N item rows the script draws right after. The one deliberate divergence from a button: **a press on the header that releases outside still clicks**, because the list is a popup that swallows the gesture and users flick it. Item rows highlight on hover/selection and report their own clicks, so an open list cannot fall through to whatever is beneath it (the header is a separate widget key)
- [x] **`Input::InjectFrame(mouse, keys, buttons, wheel, text)` + mutable `Engine::GetInput()`**: fills a frame's input snapshot by hand with the same edge bookkeeping as `NewFrame` (current → previous). This unlocks the click paths — previously unreachable headless — for every widget, not just these four. A synthetic frame is replaced wholesale by the next real one, so nothing can leak into a windowed run
- [x] 4 new Lua bindings with explicit full arity (`ui_bar`, `ui_radio`, `ui_combo`, `ui_combo_item`); fixtures stub list extended in step
- [x] **`--ui-test` grows 30 → 52 assertions**: radio press-not-click / release-clicks / outside-cancels / elsewhere-never-clicks / already-selected-still-reports, combo press-not-open / release-clicks / flick-ending-outside-still-clicks / header-does-not-swallow-a-list-click, combo item press-not-pick / release-picks / other-row-not-picked, and the progress bar drawn inside a 128×128 render target with the fill read back in pixels (half fill boundary, track colour, over-range clamped with no spill, under-range with no backwards fill), plus a geometry count for all four widgets
- [x] **Mutation-checked (4)**: a RadioButton that clicks on press, a ComboBegin whose outside-release no longer clicks, a ProgressBar without the clamp, and an `InjectFrame` that skips the previous-state copy (killing edge detection) — each fails the suite, the last with five failures across every release-path assertion. The clamp mutation initially **passed**, which exposed the vacuous in-track probes above
- [x] **`--ui2-demo`**: a looping loading bar (sawtooth through empty and full), a pulsing health bar, a slider-driven mana bar whose range reaches 150% to show the clamp, a three-row difficulty radio group and a four-item unit combo with an openable list — all state in file locals, label sets as functions (no file-level tables). Verified by a pixel bounding-box probe (content within 960×540) plus one eyeball pass that caught an overlapping label and an unreadable chevron
- [x] `--ui2-demo` wired into CI on Linux (xvfb), macOS and Windows
- [x] Verification: `--ui-test` 52/52; `--ui2-demo` headless smoke + pixel-probed screenshot; headless Lua fixtures 24/24; all gates re-run green

---

## Phase 26 — Fog of war: multiply blending & mask sampling (1.20.x)

- [x] **`BlendMode::Multiply` / `draw_set_blend(2)`**: `result = source * destination` (`GL_DST_COLOR, GL_ZERO`) — the missing compositing primitive. Alpha covers pixels over, additive only brightens, so neither can dim a scene by a per-pixel coverage value; multiply makes any grayscale surface a soft mask (white = no-op, black = erase, greys dim per channel). Because it is an ordinary batch blend it composites **inside** a bound target — exactly what `rt_copy` cannot do, since it refuses any nesting — so "scene × mask" is three draws in one target, no extra pass. Documented gotcha: the source RGB drives the result and its alpha does not gate it, so a mask modulates colour, not opacity
- [x] **`rt_sample(id, x, y)`**: one pixel back out of a target as a scalar (luminance), with draw-space orientation (top-left origin, matching what was drawn). Returns **-1** for anything that is not a real sample — unknown id, out-of-bounds coordinates — so `0` always means a genuine black pixel, never a failed lookup. This is the query side of a mask: "has this world point been revealed?" becomes game state (AI, spawns, minimap tooltips) instead of pixels nobody can inspect
- [x] **`--fog-test`: 39 headless pixel-probe assertions**: white mask is an identity, black mask erases, a red mask keeps red and drops green/blue (per-channel, which is what distinguishes multiplication from an alpha cover or an inverse), a half mask lands strictly between full and empty, multiply composites inside a bound target where `rt_copy` is refused by nesting, blend mode is per-draw (alpha after multiply in one frame, and multiply over a black base stays black), accumulation persists across frames (two additive visits stack, never-visited stays black), `ReadPixel` bounds and the window surviving a masked draw
- [x] **Mutation-checked (3)**: multiply degraded to alpha (2 failures), to additive (7), and the batch not splitting runs on a blend change. The last one **passed** at first — there was no probe on "multiply over a black base", the one spot where alpha and multiply visibly differ — which added exactly that probe
- [x] **`--fog-demo`**: a procedural tile world (deterministic sin-hash, no assets, no RNG) with a fog-of-war pipeline in four steps — reveal stamped additively into a never-cleared mask target, the scene drawn through the target's camera (`rt_camera`), the mask blitted over the whole world with multiply (one transform, so tiles and mask line up by construction), then present + HUD. A baked tile minimap is multiplied by the same mask at 1/8 scale, clicking the minimap jumps the view (and the explorer) there, `F` toggles the fog, `R` forgets, and an "explored %" readout is driven by `rt_sample` on a coarse grid every 12 frames. The tile art is baked once instead of being redrawn 768 times a frame
- [x] Verified by pixel bounding-box probe (content within 960×540) plus one eyeball pass that caught the `pos` readout overflowing the right edge of the canvas
- [x] `--fog-test` / `--fog-demo` wired into CI on Linux (xvfb, exit-code + summary-line gate), macOS and Windows
- [x] Verification: `--fog-test` 39/39; `--fog-demo` headless smoke + pixel-probed screenshot; all gates re-run green

---

## Phase 27 — Animated passes: auto uniforms & a bloom-grade FX kit (1.21.x)

- [x] **Auto uniforms, stamped by the renderer**: `u_time` (the frame clock, seconds — fed from `Engine`'s elapsed time every frame) and `u_resolution` (pixels of the surface being drawn into: the target's own size inside a pass, the window's outside one, stamped on `BeginFrame`, `BeginRenderTarget` and `EndRenderTarget`). A shader that declares them animates with **no per-script bookkeeping**; one that does not is untouched (a uniform the program never declared has no location, and setting it is a no-op). The stamp re-binds each program to write, then unbinds, so the next draw's own bind wins
- [x] **`assets/fx_bright.{vert,frag}`**: the threshold every bloom starts with — Rec.709 luma against `u_threshold` with a soft `u_knee` ramp (hard cuts flicker as pixels cross the line). House rule holds: `threshold = 0, knee = 0` is a straight copy, which is what makes the pass assertable; the knee is clamped a hair above zero inside so smoothstep's edges can never coincide
- [x] **`assets/fx_wave.{vert,frag}`**: an animated pass — rows displaced sideways by a travelling sine (heat shimmer, water, VHS wobble). `u_amp` is in **pixels**, converted to UV through the stamped `u_resolution`, so the same number displaces by the same amount at any resolution. `u_amp = 0` is a straight copy; `u_time = 0` puts every row at phase zero. Sign note written where it bit: moving the *sample point* right slides the picture **left**, so the shader subtracts to keep a positive amplitude pushing the image right
- [x] **`--fx-test` grows 47 → 80 assertions**: bright pass (identity at defaults, hard cut keeps only the highlight, a knee lets mid grey through *partially* and stays monotonic), wave (no movement at `t = 0` even with amplitude, ±8 px at the quarter periods, and — the resolution proof — the same 8-pixel displacement at 128 and at 64, which fails if the stamp reported the window size instead of the target's), plus the clock read back through `Time()`
- [x] **Mutation-checked (3)**: dropping the `u_time` stamp (6 failures), stamping the window size instead of the bound target's (6 — the two half-size probes are the only ones that can see it), and a bright pass that ignores the knee (1). The first run also caught two real bugs before any mutation: the suite's target B was still 64×64 from the resize section (probes out of bounds), and the wave shader's sign moved the image opposite to its amplitude
- [x] **`--fx-demo`** chain grows to `scene → wave → bright → blur H/V → composite`: the bright pass gates what is allowed to glow (a bloom without it is a haze), the wave wobbles the skyline on the stamped clock, and both get a checkbox and sliders (threshold, amplitude in px, frequency)
- [x] Verified by pixel bounding-box probe (full-bleed skyline as designed; panel and footer inside 960×540) plus eyeball passes of both the wave-on and wave-off screenshots
- [x] Verification: `--fx-test` 80/80; `--fx-demo` headless smoke + screenshots of both wave states; all gates re-run green

---

## Phase 28 — UI widgets round 3: window, tab, tree (1.22.x)

- [x] **`Window(key, anchor, ox, oy, w, h, title, scale)` / `ui_window`**: a titled panel dragged by its bar (bar height sized off the text, floor 22 px, plus a one-pixel rule that says "this part moves the panel"). It reports *being dragged*, not a position — the position stays with the caller, moved by the per-frame `input_mouse_dx/_dy`. The grab frame is deliberately silent, so the panel never jumps by whatever distance the cursor travelled to reach the bar; dragging survives the cursor leaving the bar while the button is down, and a press on the body never grabs
- [x] **`Tab(key, anchor, ox, oy, w, h, label, active, scale)` / `ui_tab`**: one tab of a tab bar; which tab is active is the caller's (an index in the script, like every value here). Click semantics match a button (release inside clicks, releasing outside cancels). The active tab sits **brighter**, grows 2 px and drops its bottom edge so it reads as connected to the panel below; idle siblings go dark (`kField` fill, `kDimLine` border). The first revision inverted this — the active tab was drawn in the dark panel colour and the idle ones bright — which the eyeball pass on the VIDEO tab screenshot caught; the fix came with a pixel assertion ("active brighter than idle") that now pins the promise
- [x] **`Tree(key, anchor, ox, oy, w, h, label, open, scale)` / `ui_tree`**: a disclosure row — a triangle that points right when closed and down when open (three quads, no line primitives, so no scissor worries), plus a label; the children are a plain `if open` block in the script. A toggle fires once per click, then goes quiet while held
- [x] **`input_mouse_dx` / `input_mouse_dy`**: the per-frame cursor movement a drag needs — a window, a scrollbar or a node editor all move by the delta, not to the position
- [x] 5 new Lua bindings with explicit full arity (`ui_window`, `ui_tab`, `ui_tree`, `input_mouse_dx`, `input_mouse_dy`); fixtures stub list extended in step
- [x] **`--ui-test` grows 52 → 76 assertions**: window (grab frame silent, holding-and-moving drags, the same delta readable through `MouseDelta`, release ends it, motion without a button is nothing, and a press on the body neither grabs nor drags), tab (press-not-select, release-selects, outside-release cancels, a tab away from the cursor never clicks), tree (press-not-toggle, release-toggles, holding still never re-toggles, an idle frame is quiet), plus pixel probes in render targets — the window's bar/body/rule land where they should with no spill, the active tab is brighter than an idle one, and the active tab's fill reaches 2 px past its declared height
- [x] **Mutation-checked (6)**: a drag that reports motion on the grab frame, a window whose whole body is a handle, a tree that toggles every frame while held, a window bar drawn in the body colour, the active tab drawn dark (the inversion above), and the 2 px grow removed — each fails the suite; the grow mutation initially **passed** and earned the "drops its bottom edge" probe
- [x] **`--ui3-demo`**: a draggable SETTINGS window with three tabs — audio (three labelled mixer sliders + mute), video (two collapsible tree groups, display open with brightness/gamma, effects closed with bloom/fog), controls (radio + combo) — a RECENTER button that puts the panel back, and hover tooltips built from plain quads + `draw_text` + `ui_hovered`. The window position is two file locals clamped so the title bar can never leave the canvas
- [x] Verified by pixel bounding-box probe (content within 960×540) plus eyeball passes of the audio tab and the video tab (the latter caught the tab-style inversion)
- [x] `--ui3-demo` wired into CI on Linux (xvfb), macOS and Windows
- [x] Verification: `--ui-test` 76/76; `--ui3-demo` headless smoke + pixel-probed screenshots of both tabs; headless Lua fixtures; all gates re-run green

---

## Phase 29 — Decoded files & streamed music (1.23.x)

- [x] **`AddClipFile(name, path)` / `audio_load_file`**: decode **any** file miniaudio's decoder reads — WAV, FLAC, MP3, OGG/Vorbis — downmixed to mono and resampled to the mixer rate, then register it under the same clip shape `AddClipWav` produces (which stays as the dependency-free WAV path; miniaudio was already compiled in, so no new vendored code). One call turns a compressed track into one-shot or music material
- [x] **`AddStream(name, path)` / `audio_stream`**: the same files as **streams** — the decoder stays open, the clip holds an 8192-frame window (~0.19 s at 44.1 kHz) and the mixer refills it on demand as the cursor walks the *file's* full length (`length_frames` read once at open; `pcm` is just the current window). A three-minute track costs kilobytes of residency instead of megabytes, and `PlayMusic`/one-shots work on it unchanged
- [x] **Sequential refill, not a seek per window**: a refill only seeks when the requested frame isn't exactly the one after the window just read — a seek resets miniaudio's resampler, and resetting at every window boundary is audible (and would make a stream differ from a straight decode). Loop-over-the-tail and jumps do seek
- [x] **`ClipDuration(name)` / `audio_clip_len`** (full file length, streamed or not) and **`ResidentFrames(name)` / `audio_clip_cached`** (whole file for decoded clips, at most one window for a stream — feeds "buffered" read-outs); streams are released in the destructor; unknown names and failed opens are rejected, re-registering a taken name is rejected
- [x] 4 new Lua bindings with explicit full arity (`audio_load_file`, `audio_stream`, `audio_clip_len`, `audio_clip_cached`); the fixture generator's default-stub fallback covers them (they need no special return values)
- [x] **`--audio-test` grows 74 → 106 assertions**: decoded files (length/peak/energy of the same source through the WAV parser vs miniaudio's decoder — *not* sample-identical: the two paths use different resampler interpolators, so the cross-check is on shape, not bits; missing file, unknown format, taken name and unknown name all reject) and streamed tracks (residency stays at one window, `ClipDuration` reads the full file before any refill, a stream and a decode of the same 20 000-frame source agree block-by-block across 40 × 256-frame reads, looping over the tail re-seeks, pause/resume/stop behave)
- [x] **Mutation-checked (4)**: refilling with an unconditional seek (2 failures — the stream/decode comparison hears the resampler reset), forcing `window_start = 0` (2), decoding everything up front (3 — residency betrays it), and a decoder that stops early (2). All restored via file backups, never `git checkout`
- [x] **`--audio-demo`** grows a FILE PLAYBACK panel: DECODED / STREAMED buttons for the same `assets/music.wav`, its duration, a `buffered X vs Y` read-out and two bars comparing what each path holds in memory — the whole point of streaming, made visible
- [x] Verified by pixel bounding-box probe (content within 960×540) plus an eyeball pass of the new panel
- [x] Verification: `--audio-test` 106/106; `--audio-demo` headless smoke; headless Lua fixtures 24/24; all gates re-run green

---

## Phase 30 — Animated tiles: Tiled tileset cycles on one clock (1.24.x)

- [x] **Tiled tileset animation parsed**: `tilesets[].tiles[].animation` — a per-tile list of `{duration (ms), tileid (local)}` frames, looped forever from the moment the map loads (there is no one-shot or ping-pong variant in the format). A frame with a zero duration or a broken id makes the phase computation degenerate, so the *whole animation is dropped* and the tile stays static rather than playing something wrong
- [x] **One clock per tile id, not per cell**: Tiled animates a *tile*, so every cell showing it is in lockstep — which is why the whole feature is a single `anim_time_` accumulator (double, so a long session never loses frame resolution to float drift) plus a frame lookup, not per-cell state
- [x] **Ticked by the engine**: every loaded map advances once per frame (`TilemapLibrary::AdvanceAll` from the frame loop), like the animator and unlike a script-fed uniform — water and torches play with no script bookkeeping. `Advance(dt)` stays public for tests and scrubbing; negative dt is ignored (time never runs backwards)
- [x] **The draw is the only consumer**: a cell stores its base gid forever; only the submitted *picture* rides the clock (`AnimatedLocalId` in the tile-layer draw). `SolidAt` / `OneWayAt` / slopes / `GroundYAt` keep reading the base gid, so a tile whose frames differ cannot open a hole in the floor halfway through its cycle — asserted across a full cycle in the test
- [x] **Queries**: `AnimatedGid` (what is drawn), `AnimationFrameCount` (0 = static), `AnimationCycle` (seconds), `AnimationFrameIndex` (index into the timeline, *not* found by matching the shown tile id — an animation may name the same tile twice, a 0,1,0,1 flicker), `AnimationTime`, `ResetAnimation`; 6 new Lua bindings (`map_anim_time`, `map_anim_count`, `map_anim_cycle`, `map_anim_index`, `map_anim_gid`, `map_anim_reset`) with explicit full arity; the fixture generator's default-stub fallback covers them
- [x] **`--tile-test` grows the suite to twelve engine self-tests (55 assertions)**: parse (frame counts, cycle lengths, zero-duration and empty rejections, second-tileset keying), the clock (frame 0 at t=0, mid-frame continuity, wrap at the cycle end, accumulation across calls, negative dt, reset), the repeating-cycle index trap (frames 2 and 3 of a 0,1,0,1 cycle are not frames 0 and 1), collision stability across a whole cycle, pixel probes in a render target (the animated cell switches colour with the clock, a static neighbour never moves, the minimap `DrawScaled` bake animates too), and the engine's own tick read from inside the frame hook (`Run()` calls `Shutdown()` on return, which clears the library — the clock is sampled per frame, not after)
- [x] **Mutation-checked (5)**: dropping the engine's per-frame advance, a frame index stuck at 0, accepting a zero-duration frame, forgetting to resolve the frame in the draw path, and deriving the frame index by matching the shown tile id — each fails the suite; the last one is exactly what the repeating-cycle assertions exist to catch. All restored via file backups, never `git checkout`
- [x] **`--tile-demo`**: a 30×12 map with a rippling pond (3-frame water cycle) and flickering torches (2-frame), a HUD built from the read-only queries (frame indices, cycle lengths, phase bars, the current shown gid, the solid grid staying put) and a RESET CLOCK button (`map_anim_reset`). Verified by pixel bounding-box probe (content inside 960×540) plus an eyeball pass, and by a cross-frame pixel diff proving the pond really animates
- [x] `--tile-test` / `--tile-demo` wired into CI on Linux (xvfb), macOS and Windows
- [x] Verification: `--tile-test` 55/55; `--tile-demo` headless smoke + screenshots; headless Lua fixtures 24/24; all gates re-run green

---

## Phase 31 — Rigid bodies: orientation, polygons, friction, impulses (1.25.x)

- [x] **Shapes**: a body is a circle (`radius > 0`), an oriented box (`half_extents`, the legacy AABB path) or a convex polygon (3–8 local vertices, `BodyConfig::SetVertices` / `phys_create_poly`). Creation recenters polygon vertices on their centroid and shifts `position` so the reported position is always the centre of mass
- [x] **Mass properties derived from density**: circle `ρπr²` with `I = ½mr²`, box `ρ·4hx·hy` with `I = m(hx²+hy²)/3`, polygon by shoelace area plus the standard second-moment sum; recomputed per step (so a script edit to `density` takes effect) and on `ApplyImpulse`
- [x] **Angular dynamics**: `rotation` / `angular_velocity` in `BodyConfig`, integrated with the same semi-implicit substeps; impulses and forces applied at a world point add `cross(r, P)` torque, so off-centre hits spin bodies
- [x] **Narrowphase**: circle-circle, circle-polygon (closest point, plus a nearest-face push for a fully contained centre) and polygon-polygon SAT with reference/incident face selection, side-plane clipping and up to two contact points. Capsule-free; corner points within linear slop are kept so a face contact never degenerates into one-sided support (which made stacks creep sideways)
- [x] **Solver**: sequential impulses over the manifolds with accumulated normal impulses (clamped ≥ 0), Coulomb friction clamped to `μ·Pn`, restitution only above a rest threshold, and warm starting through a per-feature impulse cache that survives across substeps and frames (purged on body destruction). `SetVelocityIterations` defaults to 8
- [x] **Position solve**: pseudo-velocity (split-impulse) iterations that accumulate the correction and apply it to the transforms at the end — real velocities untouched, so overlap resolution never becomes launch energy, and iterating propagates correction through stacked chains (a local per-pair push cannot lift a column)
- [x] **Queries follow rotation**: `RayCast` against world-space polygons (half-plane clipping), `OverlapCircle` / `OverlapBox` / `PointTest`, plus `WorldVertexCount` / `WorldVertex` for debug draw and Lua rendering
- [x] **16 new Lua bindings**: `phys_create_poly` (vararg), `phys_set_rot` / `phys_rot`, `phys_set_ang_vel` / `phys_ang_vel`, `phys_set_friction` / `phys_friction`, `phys_set_density` / `phys_mass`, `phys_apply_impulse`, `phys_apply_force`, `phys_apply_torque`, `phys_vertex_count` / `phys_vertex_x` / `phys_vertex_y`, `phys_smooth_rot`
- [x] **`--phys-rigid-test` (32 assertions)**: mass for box/circle/polygon plus static/infinite and density edits, off-centre impulse spin and integration, rotated world vertices, 45° corner support and a face-normal ray on a tilted box, friction vs frictionless sliders, restitution bounce without tunneling, deep containment push-out, five-box stack stability and edge-landing spin, force integration and `SmoothRotation`
- [x] Wired into CI on Linux (xvfb), macOS and Windows next to the existing physics self-tests
- [x] Verification: `--phys-rigid-test` 32/32, `--phys-query-test` 22/22, `--phys-filter-test` 14/14, `--timestep-test` 30/30, all twelve preceding engine self-tests green; Lua `scripts/phys_test.lua` PASS; `--map-demo` (24 balls on tile colliders) headless smoke; `--phys-bench` 600 dynamics ≈ 2.9 ms/step with the grid

---

## Phase 32 — Complete physics engine: kinematic, sleeping, joints, CCD (1.26.x)

- [x] **Kinematic bodies** (`BodyType::Kinematic`): infinite mass and rotational inertia like a static body, but `velocity` / `angular_velocity` are integrated, so a script-driven platform carries riders through friction and is never pushed back by contacts. Kinematic bodies never sleep, ignore gravity, and still generate contact events
- [x] **Damping and fixed rotation**: `linear_damping` / `angular_damping` apply `v /= 1 + d*dt` per substep (dynamic and kinematic), `fixed_rotation` forces infinite rotational inertia and zeroes angular velocity every step
- [x] **Automatic sleeping**: bodies below the linear/angular tolerances for `sleep_time` (default 3 px/s, 0.15 rad/s, 0.5 s) stop integrating and solving; they wake on an impulse/force/torque, on contact with a moving body, through a joint with a moving partner (motorised joints never sleep), via `WakeBody`, or when sleeping is disabled globally. `allow_sleep` opts a single body out
- [x] **Joints** (`JointConfig`, `JointId`, up to 1024): **Distance** (rigid rod), **Rope** (pull-only max length), **Revolute** (anchor coincidence, optional motor with torque cap and angular limits) and **Weld** (anchors + locked relative angle). Velocity constraints run inside the existing iteration loop; anchor coincidence and angle violations are corrected positionally afterwards (limit is a hard clamp, anchor error fixed last so angular corrections cannot leave the hinge visibly split). Connected bodies skip collision by default (`collide_connected` opts in) via a broadphase pair filter
- [x] **Contact points**: `ContactEvent::point` carries the first world-space manifold point for began events (`phys_contact_px/py`)
- [x] **Substepped anti-tunneling**: when the fastest awake body would travel more than the smallest collider feature per substep, extra substeps are inserted up to `max_substeps` (default 16); the spatial-grid broadphase inflates each moving body by `speed * dt`, so a 1 px bullet at 2400 px/s still stops at a 4 px wall. This is substepped CCD, not exact TOI
- [x] **Handle generation fix**: body and joint slots preserve their incremented generation on reuse, so a stale handle can never resolve to the new occupant (asserted)
- [x] **19 new Lua bindings**: `phys_set_kinematic`, `phys_set_damping`, `phys_set_fixed_rot`, `phys_wake`, `phys_sleeping`, `phys_set_sleep`, `phys_set_max_substeps`, `phys_substeps`, `phys_joint_distance/rope/revolute/weld`, `phys_joint_motor/limit/length/count/destroy`, `phys_contact_px/py`; `phys_set_pos/vel/rot/ang_vel/density` and impulse/force/torque now wake the body
- [x] **`--phys-extended-test` (39 assertions)**: kinematic rider carried by a platform (and not pushing it back), damping decay, sleep/wake/allow_sleep/global toggle, stale handle rejection, distance rod length, rope taut/slack, revolute motor speed and integration, angular limits + anchor coincidence, weld lock, connected-pair collision filtering, contact point on the floor surface, fixed rotation, and a fast bullet versus a thin wall (extra substeps + no tunneling)
- [x] **`--angry-birds-demo` validation sandbox**: a slingshot (world-space drag + launch velocity), two wooden towers built from sleeping dynamic boxes, three pigs popped by bird contact events or by being shoved, tumbling tinted blocks (`sprite_draw_rotated_tinted`, a new binding), drag-trajectory preview, particle puffs, score/birds/pigs HUD and `R` restart. The camera follows the bird through a deadzone while it flies, then eases back to the sling framing; impacts and pops play the synthesized `brick` / `coin` / `launch` / `win` / `lose` clips and add camera trauma. Headless `ANGRY_TEST=1` auto-aims three shots; the run pops all three pigs (300 points) and flattens both towers, exercising stacking, wake-on-impact, impulses, friction, contact events, camera follow and render interpolation in one scene
- [x] **`--limbo-demo` joint-heavy sandbox**: a 6-part ragdoll (head/torso/arms/legs) driven by impulses through 5 revolute joints; a 9-node hanging rope chain with a heavier hook that the right arm can weld to at runtime (E) and release; a 6-plank revolute bridge sagging over the water; buoyancy + drag impulses so a light ball floats, wood bobs and stone sinks (verified numerically: ball 421, stone 480); misty silhouette rendering with a following camera and a screen-anchored HUD reading live joint counts. `K` destroys every ragdoll joint and re-tethers the pieces with slack rope joints, so the body separates but the parts still pull on each other; drowning in deep water triggers the same severing. A new `to_int` binding bridges entity-read floats to handle-based APIs
- [x] Wired into CI on Linux (xvfb), macOS and Windows next to the other physics self-tests (plus the Angry Birds and LIMBO demo headless smokes)
- [x] Verification: `--phys-extended-test` 39/39, `--phys-rigid-test` 32/32, `--phys-query-test` 22/22, `--phys-filter-test` 14/14, `--timestep-test` 30/30, the other twelve engine self-tests green, Lua `scripts/phys_test.lua` PASS, `--map-demo` headless smoke, `--phys-bench` still ~5× faster than brute force, Angry Birds auto run typically pops all three pigs (score 300) and flattens both towers

---

## Phase 33 — Text & platform layer: CJK, i18n, user paths, save v2, Steam (1.27.x)

- [x] **UTF-8 everywhere**: `Font` decodes codepoints (invalid sequences become U+FFFD instead of breaking the line), `MeasureText` / `DrawText` / `DrawTextEx` walk codepoints, and `DrawTextEx` wraps **per character for CJK** (`zh`/`ja`/`ko` ranges) while Latin still wraps at spaces. Leading spaces produced by a wrap are dropped
- [x] **On-demand glyph atlas**: vector fonts start with **zero** pre-rasterized glyphs; `GetGlyph(char32_t)` looks a codepoint up with `stbtt_FindGlyphIndex` → `stbtt_GetCodepointHMetrics` / `BitmapBox` / `MakeCodepointBitmap`, shelf-packs it into a 1024×1024 RGBA atlas and uploads just that rect with the new `Texture2D::UpdateRegion` (`glTexSubImage2D`), so a 10 000-glyph CJK session grows the atlas instead of pre-baking it. The embedded 8×8 bitmap fallback stays ASCII-only
- [x] **TTC aware**: `font_load` detects a `ttcf` collection and opens face 0 via `stbtt_GetFontOffsetForIndex`, so `Hiragino Sans GB.ttc` / `PingFang.ttc` / `msyh.ttc` work as-is
- [x] **New text calls**: `Font::DrawTextRotated` (rotated single line around its origin), `Renderer::DrawTextFit` (uniform shrink to a width floor), `TextLineHeight`, `LoadFont`, `HasVectorFont`; Lua `font_load`, `text_line_height`, `draw_text_rotated`, `draw_text_fit`
- [x] **i18n** (`I18n`, engine-owned): TSV packs keyed by the **source English text** (`# name: 中文` names the language), `LoadDirectory(dir, default_code)`, `LoadFile` for mods/DLC, runtime `SetLanguage`, unknown keys falling back to the key itself, CJK detection by language code, and per-pack entry counts. Lua `i18n_load/add_file/set_language/language/t/has/is_cjk/language_count/language_code/language_name/entry_count`
- [x] **`AppPaths`**: resource root auto-detection (`FAKE2D_ROOT` → cwd containing `assets/` → executable dir, with a macOS `.app` looking in `Contents/Resources`; Windows uses `GetModuleFileNameW`, Linux `/proc/self/exe`), per-user directory (`%APPDATA%/Fake2D`, `~/Library/Application Support/Fake2D`, `$XDG_DATA_HOME/fake2d`, `FAKE2D_USER_DIR` override), and `saves/` / `logs/` / `settings.json` / `mods/` conventions; all UTF-8 paths. Lua `paths_resource_root/user_dir/saves_dir/logs_dir/config_path/mods_dir/resolve/set_resource_root/set_user_dir/ensure_dir`
- [x] **Save containers v2**: `SaveStore::SaveBlob` writes `F2SV | container version | game version | header length | header | CRC32 | payload` atomically (tmp + rename) and `LoadBlob` verifies magic, header length and CRC; a legacy bare payload still loads as version 0. `ReadBlobInfo`, `ListSlots` (`.json` + `.sav`, sorted/de-duplicated), `DeleteSlot`, `RenameSlot`. Lua `storage_blob_write/read/version/header/size`, `storage_slot_count/name/delete/rename`
- [x] **UI text tools**: `UI::LabelFit` (shrink to `max_width` down to `min_scale`), `UI::Tip` (per-frame hover rect record) + `DrawTooltip` (gold border, dark fill, clamped to the viewport), bound as `ui_label_fit`, `ui_tip`, `ui_tip_draw`
- [x] **Steamworks adapter**: optional SDK backend behind `-DFAKE2D_WITH_STEAM=ON -DFAKE2D_STEAM_SDK=<sdk>` (CMake locates `libsteam_api.dylib` / `steam_api64.lib` / `libsteam_api.so`), default **dependency-free stub** so game code calls it unconditionally: `steam_init/available/shutdown/tick/unlock/rich/language/cloud_enabled/set_cloud`
- [x] **`--font-test` (12 assertions)**: ASCII measurement/advance sums, line height, truncated UTF-8 safety, ASCII word wrapping, a failed load leaving the live font intact, missing-file rejection, CJK font candidates (skipped with `[font] SKIP` on fontless hosts), CJK advance, mixed CJK+Latin measurement, per-character wrapping, rotated draw
- [x] **`--platform-test` (47 assertions)**: two packs + switch + mod override + entry counts + CJK detection + display names; user dir/saves/config/ensure/resolve; blob round-trip, version/header/size, bad-CRC and legacy loads, `.json` + `.sav` coexistence, rename/delete and illegal slot names; Steam stub state (unavailable, init false, `english`, cloud flag); `ui_label_fit` and `draw_text_fit` width constraints
- [x] **`--text-demo`**: one screen combining CJK/Latin wrapping, rotated text, shrink-to-fit, live language switching (L / button), the resolved user/saves/config paths, a v2 save container's bytes/version/header/slot count (written under `$TMPDIR/fake2d_text_demo` so real profiles stay clean), a UI panel with hover tooltip, and the Steam stub state
- [x] Wired into CI on Linux (xvfb), macOS and Windows: both self-tests as exit-code gates plus `--text-demo --screenshot` smokes
- [x] Verification: `--font-test` 12/12, `--platform-test` 47/47, `--text-demo` headless screenshot (CJK title/paragraph/rotated text/panel all render), the twelve preceding engine self-tests, headless Lua fixtures, and the demo smokes re-run green

---

## Phase 34 — Production layer: fonts, i18n, saves, logging, mods (1.28.x)

- [x] **Fallback font faces**: `Font` now holds a stack of parsed faces sharing one atlas. `GetGlyph` walks primary → fallbacks (TTC aware, per-face metrics/ascents), caches misses, and re-tries them when a face is added. `font_add_fallback` / `font_clear_fallbacks` / `font_face_count` / `font_face_path` / `font_has_glyph`, plus `AddFallbackFont` in the renderer
- [x] **Typography**: Latin kerning from the font's `kern` table (`stbtt_GetGlyphKernAdvance`, same-face only); CJK 禁则处理 (closing marks never start a line, opening brackets never end one); the atlas doubles to 2048² (~3k CJK glyphs at 32px) and is zero-filled so bilinear edges never sample stray alpha
- [x] **i18n hardening**: UTF-8 BOM skipped on the first TSV line; per-language missing-key report (deduplicated, capped at 512, cleared on switch) exposed as `i18n_missing_count/key/clear_missing/set_track_missing`; `EngineConfig.localization_dir/language` auto-load `<resource root>/assets/lang` at startup
- [x] **Save engineering**: `EngineConfig.save_dir` defaults to `AppPaths::SavesDir()` (per-user platform directory; explicit paths still win for portable builds); every blob save keeps the previous container as `<slot>.sav.bak`, corrupt/missing primaries load the backup (`restored_from_backup` / `storage_blob_backup_used`), and delete/rename cover the backup; `storage_dir()` exposes the resolved directory
- [x] **Logging & crash reports**: `log::Init` opens `<dir>/game.log` with rotation to `game.prev.log`, timestamps, stderr mirroring and optional crash handlers writing `crash-<timestamp>.txt` (POSIX `backtrace`, Windows `CaptureStackBackTrace` + unhandled-exception filter). Lua `log_info/warn/error/active/path`
- [x] **Achievements**: local text mirror (`AppPaths::UserDir()/achievements.txt` by default), idempotent unlocks, Steam forwarding + `SyncToSteam`, `ach_unlock/unlocked/count/id/sync/path/set_path/clear`. Steam adapter gains `BackendName()` ("steamworks"/"stub") and `RestartAppIfNecessary()`
- [x] **Mod manager**: `mod.json` discovery (`name/version/enabled/load_order`), stable ordering, enable/disable, mod-aware `Resolve` (last load order wins, then base), `lang/*.tsv` merge into I18n, report text. Lua `mods_scan/scan_more/count/name/version/path/order/enabled/active_count/active_name/set_enabled/base_dir/resolve/load_lang/report`
- [x] **UI & CI hardening**: tooltip hover delay (`SetTipDelay` / `ui_set_tip_delay`, default 0.3 s); `Renderer::DistinctFramebufferColors` + `--shot-min-colors N` rejects screenshots that saved but drew nothing (exit 2); CI runs the text demo with a colour floor on all three OSes and compile-checks the Steam SDK branch against `tests/fake_steam` (`-DFAKE2D_TEST_STEAM_HEADERS=ON`)
- [x] **`--font-test` 15 assertions**: fallback registration/face path, fallback-supplied CJK glyph, plus the previous measurement/wrap/rotation checks
- [x] **`--platform-test` 83 assertions**: BOM, missing-key report, save backup/rollback/delete, log + rotation, achievements mirror, mod discovery/ordering/override/languages/report, Steam backend/restart, tooltip delay
- [x] Verification: `--font-test` 15/15, `--platform-test` 83/83, `--text-demo` screenshot with `--shot-min-colors`, the other 14 self-tests, demo smokes and CI on Linux/macOS/Windows

---

## Phase 35 — Gameplay essentials: RNG, easing, snapshots, window & input (1.29.x)

- [x] **Deterministic RNG**: splitmix64 `Random` (seed/state/Uint64/Float01/Range/Int/Chance) owned by the engine (`Engine::GetRandom`), Lua `rng_seed/state/float/range/int/chance`. Demos no longer hand-roll LCGs
- [x] **Easing utilities**: header-only `fake2d::ease` (Lerp, Clamp01, SmoothStep, frame-rate-independent Approach, quad/cubic/back/elastic curves, named lookup), Lua `ease/lerp/clamp01/smoothstep/approach`
- [x] **Entity snapshots**: `EntityStore::Serialize/Deserialize` (tab-separated `F2ES1`, exact 17-digit doubles, escaped strings, stale handles stay stale after restore), Lua `ent_snapshot/ent_restore` plus `ent_save/ent_load` through the CRC-checked v2 container
- [x] **Input action mapping**: `Input::BindAction/ClearActions/ActionDown/Pressed/Released` over every bound key; Lua `input_bind/input_actions_clear/input_action_down/pressed/released` (rebinding = clear + bind)
- [x] **Window controls**: `EngineConfig.resizable/min_width/min_height`; window size query + resize, runtime resizable toggle, minimum size, six standard cursor shapes (GLFW cursors cached and destroyed); Lua `window_width/height/set_size/resizable/set_resizable/set_min_size/cursor`
- [x] **Audio control**: playback-rate pitch (clamped 0.25–4), looping non-music voices and `StopClip`; Lua `audio_play_pitched/audio_loop/audio_stop`
- [x] **Runtime stats**: smoothed FPS and last-frame ms on the engine, draw calls from the renderer; Lua `stats_fps/stats_frame_ms/stats_draw_calls`
- [x] **Text polish**: `draw_text_shadow` (offset drop shadow) and `draw_text_outline` (8-direction ring) through the same glyph batch
- [x] **`--gameplay-test` (40 assertions)**: seed reproducibility/divergence, uniform bounds and inclusive ints, chance edges; easing endpoints/clamp/smoothstep/approach; entity snapshot round-trip (exact doubles, escaped strings, stale handles, malformed rejection, blob save/load); action down/pressed/released with multiple bindings; window resize/resizable; shadow/outline pen advance; pitch/loop/stop and stats reads
- [x] Wired into CI on Linux, macOS and Windows as an exit-code + `[gameplay] PASS` gate
- [x] Verification: `--gameplay-test` 40/40, the other 16 self-tests (787 assertions total), all demo smokes and the text-demo colour probe

---

## Phase 36 — 2.5D brawler toolkit & Dino Brawl demo (1.30.x)

- [x] **Gameplay time scale + hit-stop**: `Engine::SetTimeScale/TimeScale/Hitstop`; the scaled delta drives scripts, physics, animations, particles, tiles and camera effects together, while rendering and input keep running. `Hitstop(seconds)` freezes for real time and overlapping impacts extend it. Lua `time_set_scale/time_get_scale/time_hitstop`
- [x] **Actor capacity**: `EntityStore` grows to 16 numeric slots / 4 string slots and 512 entities, enough for a brawler roster (x, depth, height, velocities, facing, hp, state, state time, clip, i-frames, attack serial, hit-by serial, AI timer, type, flags)
- [x] **Clip capacity**: `AnimationSystem::kMaxAnimations` 32 → 128 so every pose of every character type fits without id recycling
- [x] **`--combat-test` (18 assertions)**: time scale defaults/clamps, hit-stop does not change the scale, 16-slot entity round-trip and snapshot restore, out-of-range slots, 512-entity roster, 128-clip pool exhaustion
- [x] **`--beat-em-up-demo` (Dino Brawl)**: a complete Cadillacs-and-Dinosaurs-style stage built purely in `scripts/beat_em_up_demo.lua`:
  - ground-plane projection (x / depth / height) with per-depth scale and `draw_set_z` y-sorting over a sorted batch
  - per-actor state machine: idle / walk / 3-hit punch combo / kick / spin / hurt / knockdown / getup / dead, with attack active windows
  - AABB hit-vs-hurt boxes, per-attack serial (multi-hit prevention), 0.25 s invulnerability, heavy knockdowns, hit-stop + camera shake + additive impact sparks
  - wave spawning (3 grunts → 4 grunts + brute → boss + 2 grunts) with a camera locked to each arena, a short heal between waves, lives/score/continue flow
  - enemy AI (approach, depth alignment, staggered global attack token), boss with horns and extra bulk, attract AI that finishes the stage headlessly
  - HUD health bar, wave banner, stage-clear / game-over flow
- [x] **CC0 art pass**: pixel-art fighters from Chasersgaming (Renegade / Soldier, 16×32 frames with punch/kick/knockout/getup strips) and FisherG's 4-colour night-city parallax layers, plus a generated seamless asphalt tile; committed under `assets/brawler/` with `LICENSE.txt`. New Lua bindings `sprite_load_nearest` (pixel-art filtering) and `sprite_draw_flip_tinted` (mirrored palette swaps / hit flashes); the primitive painter stays as a fallback when the art is absent
- [x] Wired into CI on Linux, macOS and Windows (`--combat-test` exit gate + `--beat-em-up-demo` headless run and screenshot colour probe)
- [x] Verification: `--combat-test` 18/18; all 18 self-tests at 805 assertions; every demo smoke; text-demo colour probe

---

## Phase 37 — Vertical shooter & Raiden-style demo (1.31.x)

- [x] **`--shmup-demo` (Raiden-style vertical shooter)**, built in `scripts/shmup_demo.lua`:
  - vertically looping desert + parallax clouds; a fixed screen-space camera so world and screen coordinates agree
  - player ship: 8-way movement, held auto-fire, banking frames, engine glow, invulnerability blink and respawn
  - three upgradeable weapons — Vulcan (parallel / 3-way / 5-way), Laser (piercing beams), homing Missiles — plus pickups and a bomb that clears bullets and damages the screen
  - fighters (sine descent, aimed shots), medium ships (wave movement, 3-spread), big ships (hover + 5-spread, power-up drops)
  - a multi-pattern boss (aimed fan, 16-bullet ring, mixed volley) with an entry/hover cycle and a health bar
  - lives/score/stage loop, game over, restart, and a bullet-dodging attract AI for headless runs
- [x] **CC0 art**: ansimuz's pixel shmup pack (ship/enemies/explosions/bolts/power-ups + 256×608 looping desert and cloud layer), committed under `assets/shmup/` with `LICENSE.txt`; uses the nearest-filter and flip/tint bindings added in 1.30.1
- [x] **`--shmup-test` (10 assertions)**: every sprite sheet/background dimension, nearest-filter loading and missing-file rejection
- [x] Wired into CI on Linux, macOS and Windows (`--shmup-test` exit gate + `--shmup-demo` headless run and screenshot colour probe)
- [x] Verification: `--shmup-test` 10/10; all 19 self-tests at 815 assertions; every demo smoke; text/beat/shmup colour probes

---

## Phase 38 — Snooker & billiards physics demo (1.32.x)

- [x] **`--snooker-demo` (playable snooker frame)**, built in `scripts/snooker_demo.lua`:
  - a purpose-built billiards solver: 5 fixed substeps per frame, constant cloth rolling deceleration, equal-mass elastic ball-ball impulses with positional separation
  - continuous contact rewind: each overlapping pair is solved at the exact contact instant (quadratic time-of-impact) so the object-ball direction stays accurate enough to pot
  - cushions reflect the normal component (restitution + tangential damping) and bend the rebound with side spin; follow/draw transfers top/back spin along the pre-impact cue direction
  - six pocket mouths swallow balls inside a radius; cue-ball pot respots in the D and colours return to their spots
  - cue control: mouse aim, hold-to-charge power gauge, ghost-ball aim line, cue-stick pull-back, clickable spin pad and arrow-key spin
  - full frame flow: reds then a nominated colour, colours respotted while reds remain, then yellow → black in order, foul penalties (cue potted, wrong first contact, wrong ball potted), two-player scoring, breaks and frame wins
  - an attract AI with a ghost-ball pot solver (legal ball + pocket + clear object path + solid contact), falling back to a safe first-contact line; deterministic and active whenever the controls are idle, so headless runs play themselves
- [x] **`--snooker-test` (10 assertions)**: generated shaded 64×64 ball sprite (linear-filter load, opaque lit centre, transparent corner, darker rim) plus white texel and missing-file rejection
- [x] Wired into CI on Linux, macOS and Windows (`--snooker-test` exit gate + `--snooker-demo` headless run and screenshot colour probe)
- [x] Verification: `--snooker-test` 10/10; all 20 self-tests at 825 assertions; every demo smoke; text/beat/shmup/snooker colour probes

---

## Phase 39 — Run-and-gun & Contra-style demo (1.33.x)

- [x] **`--contra-demo` (Contra-inspired run-and-gun)**, built in `scripts/contra_demo.lua`:
  - a 3400px jungle stage with layered parallax (moon, stars, diamond mountains, three palm layers, mist); ground segments, pits, one-way steel beams and solid crates
  - kinematic player: acceleration/friction, coyote time, jump buffering, variable jump height, crouch, one-hit deaths, lives and safe respawns
  - 8-direction fire with a procedural gun arm and muzzle flash; Rifle, Machine Gun, Spread Gun and laser weapon capsules dropped by flying carriers
  - enemy AI: advancing rifle soldiers, wall turrets with tracked barrels, sine-flying carriers; enemy bullets with spread, solid-platform collision and pooled bullets
  - a fortress boss: destructible top/bottom turrets plus an armoured reactor core with radial fans and aimed bursts; a core kill triggers a staged explosion sequence and a stage-clear banner
  - HUD (score, lives icons, weapon, stage, boss health), pickup banners, stage-clear/game-over overlays, synthesized SFX and a looping march theme
  - an attract AI that free-aims, jumps pits and crates, mechanically plays the whole stage and loops it; in attract mode hits do not spend the player's lives (the human player keeps the one-hit rule)
- [x] **Generated art pass**: moon disc, additive glow and a five-frame blast sheet under `assets/contra/`; the CC0 NES Soldier / Renegade sprites from the brawler pack are reused for the player and enemy soldiers
- [x] **`--contra-test` (14 assertions)**: soldier/idle/hurt/enemy strips, the blast sheet, the generated moon/glow (dimensions + alpha probes) and missing-file rejection
- [x] Wired into CI on Linux, macOS and Windows (`--contra-test` exit gate + `--contra-demo` headless runs; the Linux job also asserts "fortress core destroyed" to prove the full stage/boss flow)
- [x] Verification: `--contra-test` 14/14; all 21 self-tests at 839 assertions; all demos smoke; text/beat/shmup/snooker/contra colour probes

---

## Phase 40 — City simulation & SimCity-style demo (1.34.x)

- [x] **`--city-demo` (city builder)**, built in `scripts/city_demo.lua`:
  - a generated 56x42 region (grass, forest, lake + pond, sand shores) loaded as a Tiled map; the script mutates the tile grid in place with roads, R/C/I plots, parks and power pads
  - a screen-space HUD projected through the camera so it stays pixel-correct at any zoom: funds, date, population, jobs, RCI demand meters, adjustable tax slider, monthly income/upkeep, happiness, speed controls, alert banner and a clickable minimap
  - tool palette with keyboard hotkeys: road (drag-paint), residential / commercial / industrial zoning, parks, power plants and bulldozing, each with money costs, validity checks and synthesized SFX
  - monthly simulation: road-network flood fill from the west entrance, power coverage diamonds around plants, zone development, three growth levels per building, abandonment/rebuilding, pollution/happiness, tax income and road/plant/park upkeep
  - camera pan (WASD/arrows), wheel zoom around a 1792x1344 world, hover cell targeting, per-tile building tooltips (population/jobs/power/road status) and y-sorted 32x64 building sprites
  - a deterministic "mayor" attract AI that places power, extends streets and zones by relative RCI demand, so headless runs produce a functioning, growing town
- [x] **Generated art pass**: a 12-tile terrain sheet (grass/flowers/water/sand/asphalt/zoning plots/rubble/forest/power pad), a 128x384 building atlas (5 types x 4 levels plus construction and abandoned shells) and the Tiled base map under `assets/city/`
- [x] **`--city-test` (17 assertions)**: tileset/building atlas dimensions, alpha probes on houses/towers/smokestacks/trees/chimneys, the generated 56x42 map (entrance/starter roads/lake/terrain), and missing-file rejection
- [x] Wired into CI on Linux, macOS and Windows (`--city-test` exit gate + a 2500-frame headless city run with a screenshot colour probe and population-milestone grep)
- [x] Verification: `--city-test` 17/17; all 22 self-tests at 856 assertions; all demos smoke; text/beat/shmup/snooker/contra/city colour probes

---

## Phase 41 — RTS & Red Alert-style demo (1.35.x)

- [x] **`--rts-demo` (real-time strategy skirmish)**, built in `scripts/rts_demo.lua`:
  - a 64x48 battle map with mirrored bases, ore fields, lakes, rocks and trees; generated 32px terrain tileset and per-unit / per-building sprites tinted by team colour
  - base building: power plants, barracks, war factory, refinery and turrets with construction timers; power generation/consumption gates production speed and alerts
  - economy: harvesters auto-mine ore cells (rich → poor → dirt), haul cargo to the nearest refinery and unload credits; credits pay for structures and units
  - units: infantry, rocketeer, tank and harvester with distinct HP / speed / range / damage / cooldown; homing rifle, rocket and cannon projectiles, muzzle effects, damage flashes, explosion particles and health bars
  - controls: click/drag-box selection, right-click move/attack/harvest/return, WASD pan, wheel zoom, 1-5 structure hotkeys, 6-9 unit hotkeys, minimap click-to-jump
  - a screen-space RTS HUD: credits, power meter, army counts, mission timer, selection card, production progress and the complete structure/unit command panel
  - an enemy commander that rebuilds lost structures, builds turrets, trains units and sends attack waves at the player base; an attract commander plays the player side (including two home guards) whenever input is idle, so headless runs resolve into a real battle and victory
- [x] **Generated art pass**: 12-tile terrain sheet, four 32x32 unit sprites, six 64x64 building sprites and the Tiled base map under `assets/rts/`
- [x] **`--rts-test` (27 assertions)**: every terrain/unit/building texture dimension, alpha/shape probes (helmet, tank barrel, command-yard body), the 64x48 map's concrete pads/ore/lake and solid water, plus loader rejection
- [x] Wired into CI on Linux, macOS and Windows (`--rts-test` exit gate + a 2400-frame headless battle with a screenshot colour probe and `first blood` / `player victory` log greps)
- [x] Verification: `--rts-test` 27/27; all 23 self-tests at 883 assertions; all demos smoke; headless battle wins at frame 2080; long runs stable

---

## Phase 42 — Visual novel & Tokimeki-style dating sim (1.36.x)

- [x] **`--vn-demo` (dating-sim visual novel)**, built in `scripts/vn_demo.lua`:
  - eight illustrated 960x540 backgrounds (school gate, classroom, library, gym, park, cafe, rooftop, summer festival) and sixteen 320x480 heroine portraits (four girls x neutral / smile / blush / sad). The shipped art is AI-illustrated and tracked under `assets/vn/`; the host painters remain as a procedural fallback when a file is missing
  - a title screen, a 14-day calendar loop and a confession ending; stats for study / fitness / charm, stamina, money and affection for four heroines
  - daily actions (study, train, style, part-time work, rest), free chats with three topics per girl (the third unlocks at affection 20), gifts, dates at affection 30+, and a day-7 summer-festival choice
  - a full dialogue runner: portraits with expression changes, typewriter text (UTF-8/CJK aware), speaker name plates, 3-4 clickable choices, affection toasts, fade-in, portrait slide and falling cherry petals
  - quick save/load with a real `storage_save("vn_quick")` slot and a title-screen continue button
  - an attract player that reads the story, picks choices and finishes the romance headlessly, so CI screenshots always contain a live scene and the log proves the whole arc
- [x] **Generated art pass**: AI-illustrated backgrounds and expression sheets, chroma-keyed portraits, plus the procedural host painters as a fallback under `assets/vn/`
- [x] **`--vn-test` (31 assertions)**: every background/portrait dimension, opaque background and portrait alpha/shape probes, and missing-file rejection
- [x] Wired into CI on Linux, macOS and Windows (`--vn-test` exit gate + a 2400-frame headless story with a screenshot colour probe and day-7 / ending log greps)
- [x] Verification: `--vn-test` 31/31; all 24 self-tests at 914 assertions; all demos smoke; headless story reaches day 7, the festival and a successful confession at frame 1590

---

## Non-goals (for now)

- Full 3D / PBR (Fake2D is strictly focused on high-performance 2D)
- Exact time-of-impact continuous collision, 3D, and exotic joints (prismatic / pulley / gear) — the built-in world uses substepped anti-tunneling and the four common 2D joints; clean external Box2D bindings can still be added later
- Complex GUI editor (lightweight data-driven scripts and tools first)
- Matching full PUC-Rio Lua standard library (FakeLua subset optimized for game hosts)
---

## Phase 43 — Shipped art for every game demo (1.37.x)

- [x] **Art is source data, not first-run output**: `.gitignore` no longer throws away `assets/art`, `assets/arcade`, `assets/city`, `assets/contra`, `assets/limbo`, `assets/rts`, `assets/snooker` and the three relit tile sheets at the `assets/` root. Until now the AI key art existed only in the author's working tree, so every CI run and fresh clone silently fell back to the primitive painters.
- [x] **`tools/art_key.py`**: the bake pipeline the committed art is generated with, so a raw generation can be re-baked reproducibly. Magenta chroma key with un-premultiply plus a magenta-dominance term (strips the pink bloom the model paints around lit sprites without eating white, gold or cyan art); `sprite`, `grid`, `spin`, `texture` and `relight` commands.
- [x] **Breakout (`scripts/game.lua`)**: neon-arcade brick, paddle and ball, a four-frame spinning coin derived by squashing one illustrated disc, and the arcade-hall backdrop. The score bar and life icons are drawn from the same set; the flat quads remain as the missing-art fallback.
- [x] **Limbo**: illustrated silhouette head, torso and limbs replace the tinted plank-and-ball ragdoll, a real rope strand replaces the chain of dotted knots, and the pool is painted art that bobs instead of stacked sine quads.
- [x] **Mario**: the sky quad is replaced by the illustrated valley backdrop, and the 16px tile sheet is relit from an AI dirt/brick material.
- [x] **Platformer / slope lab**: tile sheets relit from AI grass and cut-stone materials.
- [x] **City / RTS**: terrain, building and unit sheets relit from AI park-grass, stone and military-steel materials.
- [x] **Relighting, not redrawing**: a sheet's cell layout is fixed by the Tiled maps and the `anim_frame` rects, so `relight` stretches an AI material over the sheet, desaturates it to carry detail only, and multiplies it under the painted tile colours. Tiles keep the identity the maps expect and gain real surface grain; the geometry never moves.
- [x] **World objects, not just terrain**: the Contra fortress boss (wall plates, reactor core, sandbag turrets, weapon pods and pickup crates), the snooker cue stick, the Angry Birds slingshot band and the slope lab's night sky all moved from primitive quads to illustrated sprites, each with the painter kept as the missing-art fallback.
- [x] **`--art-test` (162 checks)**: walks every shipped art folder plus the map-driven sheets at the `assets/` root, loads each PNG and fails when a folder is missing, empty or unreadable — the gate that catches a folder silently dropping out of `.gitignore`, which is exactly how this regression hid.
- [x] Verification: `--art-test` 122 textures loaded; 127 sprite assertions across the 7 per-demo suites; all 12 game demos headless with screenshots, including a full Contra run that still destroys the fortress core.

---

## Phase 44 — Demo launcher (1.38.x)

- [x] **`--launcher`**: an in-engine picker, so a demo is chosen instead of remembered as a command-line flag. A windowed run with no demo flag opens it; a bare headless run keeps the historical Breakout entry so existing smoke tests are untouched.
- [x] **One catalogue, two front doors**: `include/fake2d/demos.h` lists every demo as `{flag, script, category}`. The menu reads it through `demo_count()` / `demo_flag()` / `demo_script()` / `demo_category()` and the command line parses the same flags, so the launcher can only ever offer what the host can start. Adding a row lists the demo automatically.
- [x] **`app_launch(flag)`**: the host re-execs itself with `--<flag>-demo` once `Run()` returns, forwarding `--frames`. Every demo keeps a clean process and its own asset/audio setup. A headless run reports the request instead of execing, so a CI smoke test cannot spawn an unbounded second session.
- [x] **The menu itself**: cover art drawn from the shipped `assets/` tree, category chips (all / games / engine), a detail panel with the summary, control scheme, exact command line and script path, and full mouse plus keyboard navigation (arrows, PgUp/PgDn, Home, Tab, Enter, Esc, wheel).
- [x] **`--launcher-test` (110 checks)**: unique flags, only categories the menu can group by, and a script that actually exists for every row.
- [x] **State model**: launcher state lives in a `new_global_obj` NativeObject, because file-level locals are const bindings. `docs/SCRIPTING.md` rules 3 and 4 now say that plainly — the `+ 0` idiom the older demos use predates the current semantics, and the demo suite still needs migrating to object state.
- [x] Verification: launcher renders headless under the colour probe; `LAUNCHER_TEST_PICK` proves select → relaunch request → re-exec into the chosen demo; catalogue test green.

---

## Phase 45 — Demos move to NativeObject state (1.38.x)

FakeLua binds every file-level `local` as const, so the `local score = 0 + 0` idiom the whole demo suite was written against stopped compiling: each script died at `cannot reassign file-level constant 'x'` and the window stayed blank. All 27 demos now keep their cross-frame state in a `new_global_obj` NativeObject whose fields read and write like an ordinary table.

- [x] **`tools/migrate_state.py`**: the conversion, done as a tool rather than 27 hand edits so it is reviewable. It moves exactly the locals that are reassigned outside their own declaration line (names that are never reassigned stay put as the constants they are), skips comment and string text, leaves table-constructor keys alone, picks an object name that collides with no local at any scope, and turns the old declarations into a `reset_state()` seeding function.
- [x] **Seeding before first use**: `update()` opens with `if S.seeded == nil then reset_state() end`. An unset NativeObject field reads back nil, not 0, and several demos touch state on their very first line — `mario.lua` incremented its frame counter before its old `inited` guard ran. Seeding at the top of `update` is the one position that is always early enough.
- [x] Verification: all 27 demos compile and run 120 headless frames with no script error; the long runs still reach their milestones (Contra clears the fortress core, RTS goes first blood → player victory, the snooker AI resolves shot after shot, the VN story advances); 301 assertions across the 9 suites, including `--launcher-test` and `--art-test`.

---

## Phase 46 — Real-time 2D lighting with projected shadows (1.39.x)

A 2D game can still have light sources: they move, the objects in the scene answer to them, and it stays fast. `LightSystem` (`include/fake2d/lighting.h`, `src/render/lighting.cpp`) builds a light buffer per frame — one generated radial-falloff sprite stamped additively per light, hard projected shadow volumes multiplied back out behind each caster, one blit across the view — so every pixel of every sprite responds to the lights without a shader and without the game compositing anything itself.

- [x] **Blend state instead of a shader**: the batch has no multi-texture program, so the pass is built out of the blend modes it does have. Additive accumulates pools, Multiply removes light, and the ambient lives in the buffer's clear colour. `light_render()` is explicit and sits between the world and the HUD, so interface text is never dimmed.
- [x] **Shadows are confined to their own light's circle**: every extruded edge is clamped onto the rim of the light that cast it, so no light can punch a hole in a neighbour's pool. That is what lets the shadowless fill lights be composited *after* the shadow pass — an opaque crate stops the lantern, not the ceiling lamp.
- [x] **Only the edges that face the light are extruded** (silhouette test against the polygon's own signed area, so it is winding- and axis-agnostic). It is the correct shadow volume *and* it cut the demo's quads from 68 to 24; extruding back edges had been painting long black wedges on the wrong side of the caster.
- [x] **Batched by blend state, not by light**: the first version walked the lights and switched Additive → Multiply per light, which flushed the batch once per light — 129 draw calls and 8.7 ms/frame for 64 lights. Grouping the segments instead took it to 4 draw calls and 0.7 ms. `--light-bench` exists to keep that from silently regressing.
- [x] **Cost bounded three ways**: lights outside the view are skipped, casters outside a light's reach are never looked at, and a per-frame quad budget caps the worst case. The bench shows the cap working — pulling it to 1200 quads takes the frame from 0.73 ms to 0.42 ms rather than stalling.
- [x] **The light buffer is taken lazily.** `Init()` allocates only the two textures; the first `Render()` claims a render-target slot, so a game that never lights anything keeps all 8.
- [x] **Script API**: 24 calls — `light_create/destroy/valid/count`, `light_set_position/radius/color/shadows/enabled`, `occluder_add_rect/add_poly/add_body/set_rect/set_poly/destroy/set_enabled/count/sync_bodies`, `light_set_ambient/shadow_darkness/core/on`, `light_render`, `light_stat`. Caster ids stay valid across frames, so a script caches them once.
- [x] **Body-backed casters**: `occluder_add_body` + `occluder_sync_bodies()` re-read a physics body's world polygon every frame, so a crate that gets pushed keeps casting the right shadow with no geometry re-upload. Covering it in `--light-test` caught a real defect: `Occluder::body` was an `int`, while `BodyId` is a slot+generation handle packed into 64 bits — so every handle truncated to a stale one and the caster silently deactivated instead of casting. The field, the API and the binding now carry the full `int64_t`, which is the convention the rest of the script layer already uses. A circle-backed caster goes inactive on sync, since only polygons have vertices to read.
- [x] **`scripts/light_demo.lua`** (`--light-demo`, in the launcher): a stone cavern with a lantern a soldier paces about — attract mode now sweeps the length of the room instead of stopping after one leg — a flickering wall torch, an orbiting cyan orb, a shadowless fill, a pulsing alarm, script-slid crates and one kinematic-body crate whose shadow comes out of the simulation. `SPACE` bursts 24 more lights into the same buffer; `L` toggles the pass.
- [x] **`--light-test` (43 checks)** reads the light buffer back rather than trusting the counters: pool bright to exactly its radius, dark behind the caster, `== ambient` at 24 probes around the rim (the containment gate), a neighbour's pool untouched outside the first light's reach, a body-backed caster casting where the body is and following it when it moves, culling, budget clipping, id stability across `occluder_set_rect`, pool exhaustion, and bad ids ignored.
- [x] **Gates on work submitted, not wall clock**: the bench asserts draw calls and geometry, which hold on any machine, and prints the milliseconds. CI runners use software GL, where a ceiling loose enough to pass would also let a real regression through.
- [x] **One source of truth for the demo catalogue**: wiring the new demo exposed that `--<flag>-demo` was a hand-written chain of 27 branches duplicating the paths already in `demos.h` — and an unlisted flag was *silently ignored*, so `--light-demo` had been launching Breakout. The chain is now one branch that resolves through `FindDemo()`, unknown flags are a hard error, and `--launcher-test` (110 → 173 checks) proves every catalogued row is startable through the same parser the launcher's re-exec string goes through.
- [x] Verification: `--light-test` and `--light-bench` green, `--launcher-test` 173 checks green, all 28 demos sweep headless with no script error, lighting wired into the Linux/macOS/Windows CI lists with a colour-probed screenshot, `docs/SCRIPTING.md` gains a Lighting section.
