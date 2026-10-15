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

## Non-goals (for now)

- Full 3D / PBR (Fake2D is strictly focused on high-performance 2D)
- Heavy built-in physics engine (clean external Box2D bindings can be added later)
- Complex GUI editor (lightweight data-driven scripts and tools first)
- Matching full PUC-Rio Lua standard library (FakeLua subset optimized for game hosts)
