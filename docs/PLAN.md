# Fake2D Implementation Plan

Living checklist for the FakeLua-powered 2D rendering engine.
Status tags: `[ ]` todo · `[~]` in progress · `[x]` done

## Phase 0 — Skeleton (0.1.x) — done

- [x] Replace legacy fruit-machine demo with new project identity
- [x] CMake + CPM, GLFW, OpenGL clear path
- [x] `Engine` main loop (poll → script update → reset → present)
- [x] `ScriptHost` FakeLua bridge (`CompileFile` / `Call(update)` / `Reset`)
- [x] `examples/hello` + `scripts/main.lua`
- [x] Bilingual README + CI workflow stub
- [x] Optional `FAKE2D_WITH_FAKELUA=OFF` window-only skeleton for CI / bring-up
- [x] Wire richer FakeLua native object bindings (engine handle / draw_quad / camera)
- [x] Optional headless/null backend for CI without display

## Phase 1 — Draw primitives (0.2.x) — done

- [x] Shader pipeline (vert/frag) for textured + solid quads
- [x] Orthographic camera (pixel or world units)
- [x] `SpriteBatch` with dynamic VBO flush
- [x] Texture2D load (stb_image) + sampler state
- [x] Draw API sketch: `draw_quad`, `draw_sprite(tex, src, dst)`

## Phase 2 — Scene & assets (0.3.x)

- [ ] Transform2D / scene node tree
- [ ] Layers & z-order
- [ ] Texture atlas + frame metadata
- [ ] Resource manager (ref-counted handles)
- [ ] Script hot-reload via FakeLua TCC backend

## Phase 3 — Script API (0.4.x)

- [ ] Register native modules: `fake2d.sprite`, `fake2d.camera`, `fake2d.input`, `fake2d.time`
- [ ] Input snapshot (keyboard/mouse) readable from Lua each frame
- [ ] Mini-game sample (e.g. catch-the-falling-sprites)
- [ ] Document FakeLua subset constraints relevant to games

## Phase 4 — Text & audio (0.5.x)

- [ ] Bitmap font renderer (then SDF optional)
- [ ] Audio clip playback (miniaudio or similar)
- [ ] Simple CPU particle emitter batched into SpriteBatch

## Phase 5 — Polish (1.0.x)

- [ ] Batch sort keys (texture / blend / depth)
- [ ] HiDPI / framebuffer scaling
- [ ] Packaging notes (Linux / Windows / macOS)
- [ ] Draw-call & batch benchmarks
- [ ] Version bump policy + release workflow

## Non-goals (for now)

- Full 3D / PBR
- Built-in physics engine (may bind external later)
- Editor IDE (data-driven scripts first)
- Matching full PUC-Rio Lua (FakeLua subset only)
