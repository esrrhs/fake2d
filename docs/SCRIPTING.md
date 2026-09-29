# Fake2D Scripting Guide

How to author gameplay with FakeLua scripts on the Fake2D engine. Entry point:
`scripts/<your>.lua` (set `EngineConfig::script_entry`, default `scripts/game.lua`).

Run:

```bash
./build/bin/fake2d_hello                 # runs scripts/game.lua (breakout sample)
./build/bin/fake2d_hello --hot-reload    # recompiles the script on every save
./build/bin/fake2d_hello --headless --frames 3000   # no window, N frames, then exit
```

---

## The per-frame model

The engine calls `update(dt)` once per frame:

```
PollEvents → BeginFrame(clear) → update(dt) → arena Reset → [C++ frame hook] → flush → SwapBuffers
```

Everything the script allocates during `update` — tables, closures, varargs —
lives in a linear arena that is reset the instant the frame ends. That is the
zero-GC contract: allocate freely per frame, and never pay for a garbage
collector. In exchange, the script obeys the state rules below.

Hot reload re-compiles the entry script into the live state on save. All
script state resets on reload (game data lives in C++ by design); a broken
edit keeps the previous program running and reports the compile error once.

---

## State rules (read this before writing game logic)

Persistent script storage is **file-level `local` scalars only**, and the JIT
code generator (FakeLua 2.0) imposes restrictions that are easy to trip over.
Each rule below was verified empirically against the installed FakeLua 2.0.0:

1. **Only definitions are allowed at file level** — `local x = ...` and
   `function/ local function` definitions. A bare call such as `start_game()`
   at the bottom of the file is rejected by semantic analysis. Do one-time
   setup on the first `update` instead (see `inited` in `scripts/game.lua`).

2. **File-level locals must have an initializer.** `local lives` alone is
   rejected ("global constant must be initialize").

3. **Never initialize a mutable numeric with a bare literal.**
   A literal initializer (`local score = 0`) makes the JIT declare the
   variable `static const int64_t` in generated C; assigning it from a
   function then fails the GCC compile with "cannot assign to variable ...
   with const-qualified type". Initialize from an expression instead —
   the `+ 0` idiom used throughout `scripts/game.lua`:

   ```lua
   local score = 0 + 0      -- mutable (expression initializer)
   local lives = 3 + 0      -- mutable
   local MAX  = 640         -- never reassigned: a literal is fine
   ```

   Assigning a function *parameter* to a literal-initialized local happens to
   work, but do not rely on it; the expression initializer is the uniform,
   always-correct idiom. Booleans are safe as literals (`local done = false`).

4. **File-level tables are runtime-const.** `local S = {}` followed by
   `S.hp = 10` inside a function throws "attempt to modify a const table".
   Persistent structured state must be flattened into scalars (e.g. a bitmask
   number, as `brick_mask` does) or live on the C++ side.

5. **Globals are not supported.** Assignments to undeclared names fail the
   JIT compile. Everything is a file-level `local` (upvalue).

6. **`update(dt)` must return an integer** (e.g. `return 0`). The host reads
   the return value as `int`; returning a float throws
   "FakeluaToNativeInt failed, type is Float".

7. **Per-frame tables are free.** Build, iterate, and discard tables inside
   `update` as much as you like — the arena reclaims them at the frame
   boundary. Rebuild derived data every frame instead of caching it in
   persistent tables.

### The persistent-state checklist

```lua
local W, H = 960, 540        -- constants: literals are fine (never reassigned)
local ST_READY, ST_PLAY = 1, 2

local inited = false         -- booleans: literal initializer is safe
local state = ST_READY + 0   -- mutable number: expression initializer
local score = 0 + 0
local bricks = 0 + 0         -- structured state flattened into a bitmask
local spawned = {}           -- DANGER: const table — never do this
```

---

## Native API reference

All functions are globals registered by the engine. Coordinates are in
screen pixels with a **top-left origin, y grows downward**, unless a camera
function says otherwise.

### Drawing

| Function | Description |
|---|---|
| `draw_quad(x, y, w, h, r, g, b, a)` | Solid color quad; channels 0–1. |
| `draw_quad_rgb(x, y, w, h, r, g, b)` | Same, alpha = 1. |
| `sprite_load(path) -> handle` | Load (or fetch from cache) a PNG; returns an integer handle, `0` on failure. Cached by path — loading again returns the same texture. |
| `sprite_free(handle)` | Drop one reference to the texture. |
| `sprite_width(handle) -> n` / `sprite_height(handle) -> n` | Texture size in pixels; `0` for a stale handle. |
| `sprite_draw(handle, x, y, w, h)` | Draw a sprite; `w/h <= 0` uses the texture size. |
| `sprite_draw_tinted(handle, x, y, w, h, r, g, b, a)` | Sprite multiplied by a tint color (white texture + color = tinted shapes). |
| `sprite_draw_region(handle, sx, sy, sw, sh, dx, dy, dw, dh)` | Sub-rectangle of the texture. |
| `sprite_draw_rotated(handle, sx, sy, sw, sh, dx, dy, dw, dh, angle_rad, ox, oy)` | Region drawn rotated; `(ox, oy)` is the rotation origin inside the destination quad. |

Draw calls are batched into one vertex stream; texture switches flush the
batch, so grouping draws by texture keeps draw calls low.

### Camera

| Function | Description |
|---|---|
| `camera_set_position(x, y)` / `camera_move(dx, dy)` | Pan the world. |
| `camera_set_zoom(z)` | `> 1` zooms in, `< 1` out. |
| `camera_set_rotation(rad)` | Rotate the view around its center. |
| `camera_x() / camera_y() / camera_zoom() / camera_rotation()` | Current values. |
| `camera_screen_to_world_x(sx, sy) -> x` / `camera_screen_to_world_y(sx, sy) -> y` | Inverse transforms for input handling and HUD anchoring. |
| `camera_world_to_screen_x(wx, wy) -> x` / `camera_world_to_screen_y(wx, wy) -> y` | World → screen. |

### Input (per-frame snapshot)

| Function | Description |
|---|---|
| `input_key_down(name) -> bool` | Key held this frame. |
| `input_key_pressed(name) -> bool` | Key went down **this** frame. |
| `input_key_released(name) -> bool` | Key came up this frame. |
| `input_mouse_x() / input_mouse_y()` | Cursor position in pixels. |
| `input_mouse_down(button) -> bool` / `input_mouse_pressed(button) -> bool` | Button: `0` left, `1` right, `2` middle. |
| `input_mouse_wheel()` | Wheel delta accumulated this frame. |

Key names: `"a"`–`"z"`, `"0"`–`"9"`, `"f1"`–`"f12"`, `"space"`, `"enter"`,
`"escape"`, `"tab"`, `"backspace"`, `"insert"`, `"delete"`, `"up"`, `"down"`,
`"left"`, `"right"`, `"page_up"`, `"page_down"`, `"home"`, `"end"`,
`"caps_lock"`, `"left_shift"`, `"right_shift"`, `"left_control"`,
`"right_control"`, `"left_alt"`, `"right_alt"`, `"left_super"`,
`"right_super"`, `"menu"`, and punctuation `"-" "=" "[" "]" "\\" ";" "'" "\`" "," "." "/"`.

### Time

| Function | Description |
|---|---|
| `time_delta()` | Seconds since the previous frame (same value as `dt`). |
| `time_elapsed()` | Seconds since `Engine::Init`. |
| `time_frame()` | Frame index (0-based counter). |

### Debugging

| Function | Description |
|---|---|
| `log_number(v)` | Print a number to stderr — the script-side printf for headless testing. |

---

## Performance best practices

1. **Allocate freely per frame, persist nothing per frame.** Tables built
   inside `update` cost arena bumps, not GC pressure. A per-frame table of
   50 entries is cheaper than trying to keep it alive across frames.
2. **Flatten persistent state into numbers.** A bitmask beats a table of
   flags (see `brick_mask`); parallel scalars beat an entity table.
3. **Group draws by texture.** The batch flushes when the texture changes:
   draw all bricks, then all effects, then HUD — not interleaved.
4. **Prefer `sprite_draw_tinted` over per-color textures.** One white shape
   texture + tint covers every palette need with a single texture bind.
5. **Cache sprite handles, not paths.** `sprite_load` is cached, but the
   lookup still costs a string compare; load once (first frame), store the
   handle in a file-level local.
6. **Edge-triggered input** (`input_key_pressed`) for one-shot actions,
   level-triggered (`input_key_down`) for movement.

---

## Known FakeLua pitfalls (engine version 2.0.0)

Collected while building the samples, so you don't have to rediscover them:

- The GCC JIT backend takes ~0.3–1 s per compile — hot reload trades save
  latency for runtime speed. The TCC backend is fastest but cannot find
  system headers on many hosts (fake2d defaults to GCC, falls back to the
  interpreter).
- The strict native-call typing bites in both directions: `int` vs `float`
  return types are not interchangeable (see rule 6 above), and a native
  function's registered signature must match the call exactly.
- Compile errors from the JIT point at generated temp files
  (`/tmp/.../fakelua_jit_*.c`); the accompanying `.gcc.log` name in the
  error message is the useful one.
- Semantics differ from stock Lua where listed above; when in doubt, test
  the pattern headlessly first:
  `./build/bin/fake2d_hello --headless --frames 100` plus `log_number`.
