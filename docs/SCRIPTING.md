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
PollEvents → physics/particle/animation update → BeginFrame(clear) → update(dt) → arena Reset → [C++ frame hook] → flush → SwapBuffers
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
   function is rejected by semantic analysis with
   `cannot reassign file-level constant 'score'`. Initialize from an
   expression instead — the `+ 0` idiom used throughout `scripts/game.lua`:

   ```lua
   local score = 0 + 0      -- mutable (expression initializer)
   local lives = 3 + 0      -- mutable
   local MAX  = 640         -- never reassigned: a literal is fine
   ```

   This is **current FakeLua 2.0 semantics, not a stale bug workaround** —
   re-verified against `a55a4bf`. Booleans are safe as literals
   (`local done = false`, then `done = true` works).

4. **File-level tables are runtime-const.** `local S = {}` followed by
   `S.hp = 10` inside a function throws "attempt to modify a const table".
   Persistent structured state must be flattened into scalars (e.g. a bitmask
   number, as `brick_mask` does) or live on the C++ side.
   ⚠️ The const-ness depends on the table's *shape*, not just its scope — an
   empty constructor followed by a **field** write is const even inside
   `update`, while an **index** write (`t[i] = v`) or a constructor with
   contents is mutable. Rule 7's "per-frame tables are free" only applies to the
   mutable forms. See "Not a bug" in Known FakeLua pitfalls for the full matrix.

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
| `sprite_draw_flip(handle, sx, sy, sw, sh, dx, dy, dw, dh, flip_x, flip_y)` | Region mirrored horizontally/vertically (pass `1`/`0`). Mirroring only swaps UVs — one right-facing asset covers both walk directions without breaking the batch. |

Draw calls are batched into one vertex stream; texture switches flush the
batch, so grouping draws by texture keeps draw calls low.

### Batch control (draw-call optimization)

By default the batch flushes in **submission order** whenever the texture or
blend mode changes — draw order is exactly script order, but a scene that
hops between textures pays one draw call per hop. Sorted mode instead
collects the whole frame and reorders by

```
layer  →  z  →  blend mode  →  texture
```

merging every quad that shares a texture into one draw call. Contract:
inside one `(layer, z)` bucket you may not rely on submission order *across
different textures* (overlapping same-texture quads keep their order). Split
overlapping content into different layers instead.

| Function | Description |
|---|---|
| `batch_set_sorted(bool)` | Enable/disable deferred sorted batching for subsequent frames (stays set across frames). |
| `draw_set_layer(n)` | Integer layer of subsequent draws; lower layers draw first. Default 0. |
| `draw_set_z(z)` | Float depth inside the layer; smaller z draws first. Default 0. |
| `draw_set_blend(mode)` | `0` = normal alpha blending, `1` = additive (`SRC_ALPHA, ONE`) for glow/fire/particles. |

Layer, z and blend reset at the start of each frame. The C++ scene graph
already sorts itself automatically; these functions are for script-driven
immediate drawing.

### Debug lines

Thin 1px outlines, accumulated in a separate line stream and drawn after
all quads:

| Function | Description |
|---|---|
| `draw_line(x1, y1, x2, y2, r, g, b, a)` | One line segment. |
| `draw_rect_outline(x, y, w, h, ...)` | Rectangle outline (4 segments). |
| `draw_circle_outline(cx, cy, radius, segments, ...)` | Polygonal circle. |

`camera_shake(trauma)` adds trauma in 0..1 to the camera: screen shake
scales with trauma squared and decays automatically (small rotation +
offset, deterministic).

### Text

The engine rasterizes one default font at startup: a system TTF (DejaVu /
Arial / Helvetica / Menlo candidates) at 28px, falling back to the embedded
public-domain 8x8 bitmap font on fontless hosts (e.g. minimal CI containers).
Glyph quads go through the same `SpriteBatch` as everything else.

| Function | Description |
|---|---|
| `draw_text(text, x, y, scale, r, g, b, a)` | Draw `text` with top-left at `(x, y)`; `scale` multiplies the 28px raster size (`1.0` ≈ 28px tall). Returns the pen x after the last glyph. Channels 0–1. |
| `text_width(text, scale) -> w` | Measured string width, for centering/right-align. |

```lua
local w = text_width("YOU WIN!", 1.6)
draw_text("YOU WIN!", W * 0.5 - w * 0.5, H * 0.5 - 30, 1.6, 1, 0.85, 0.35, 1)
```

Only printable ASCII (space..tilde) is covered; unsupported glyphs are
skipped. Number concatenation works directly: `"SCORE " .. score`.

### Audio

One-shot sound effects, fire-and-forget. Clips are mono float PCM registered
from C++ (`AudioEngine::AddClip`, see the `MakeTone` synthesizer in
`examples/hello/main.cpp` for an asset-free example). Playback is optional:
on hosts without an audio device every call below is a silent no-op, so the
same script runs unchanged in headless CI.

| Function | Description |
|---|---|
| `audio_play(name, volume)` | Play a registered clip once (`volume` 0–1). Overflow beyond the 32-voice pool drops the newest request. |
| `audio_enabled() -> bool` | Whether a playback device was opened. |
| `audio_music(name, volume)` / `audio_music_stop()` | Start/restart or stop the single looping music track (C++ registers WAV clips via `AudioEngine::AddClipWav`). |

### Particles

A fixed pool of 16 emitters (1-based integer ids) and 8192 particles lives in
C++; there is **no per-frame allocation** regardless of burst size, and
emitting past the cap recycles the oldest particle. A config snapshot is
copied into each particle at emission, so changing an emitter only affects
later bursts.

| Function | Description |
|---|---|
| `part_create() -> id` | Allocate an emitter (`0` if the pool is full). |
| `part_set_lifetime(id, min, max)` | Life in seconds, uniform random. |
| `part_set_speed(id, min, max)` | Initial speed px/s, uniform random. |
| `part_set_direction(id, angle, spread)` | Center angle radians (0 = right, `-pi/2` = up) ± half-angle. |
| `part_set_size(id, start, end)` | Quad side at birth/death, linear interpolation. |
| `part_set_color(id, r, g, b, a_start, a_end)` | Tint and alpha fade over life. |
| `part_set_gravity(id, gx, gy)` | Constant acceleration px/s². |
| `part_set_drag(id, drag)` | Exponential velocity damping per second (0 = none). |
| `part_set_spin(id, spin)` | Magnitude of randomized angular velocity rad/s. |
| `part_emit(id, count, x, y)` | Spawn a burst at a position. |
| `part_draw()` | Submit every live particle to the batch at this point in the draw order. |

The engine advances all particles once per frame before `update`; scripts
only configure, emit, and draw. Typical pattern — one shared emitter whose
color is reconfigured per burst:

```lua
fx = part_create()
part_set_lifetime(fx, 0.25, 0.6)
part_set_speed(fx, 80, 260)
part_set_direction(fx, -1.57, 3.14)
part_set_size(fx, 7, 0)
part_set_gravity(fx, 0, 420)
part_set_color(fx, 1, 0.4, 0.3, 1, 0)
-- on an event:
part_emit(fx, 10, x, y)
-- once per frame, at the desired draw depth:
part_draw()
```

Particles are white-texture quads, so drawing them flushes the previous
textured batch once; group bursts and call `part_draw()` at one point.

### Camera

| Function | Description |
|---|---|
| `camera_set_position(x, y)` / `camera_move(dx, dy)` | Pan the world. |
| `camera_set_zoom(z)` | `> 1` zooms in, `< 1` out. |
| `camera_set_rotation(rad)` | Rotate the view around its center. |
| `camera_x() / camera_y() / camera_zoom() / camera_rotation()` | Current values. |
| `camera_screen_to_world_x(sx, sy) -> x` / `camera_screen_to_world_y(sx, sy) -> y` | Inverse transforms for input handling and HUD anchoring. |
| `camera_world_to_screen_x(wx, wy) -> x` / `camera_world_to_screen_y(wx, wy) -> y` | World → screen. |
| `camera_viewport_w() / camera_viewport_h()` | Viewport size in pixels (for clamping follow cameras / world HUDs). |
| `camera_shake(trauma)` | Add trauma 0..1; shake scales with trauma² and decays automatically. |

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

#### Gamepad

Polled every frame through GLFW's standard gamepad mapping (Xbox layout);
`pad` is a 0-based slot (`0` is the first controller). A disconnected pad
reads as fully neutral, so no guard beyond `input_pad_connected` is needed.

| Function | Description |
|---|---|
| `input_pad_connected(pad) -> bool` | A gamepad with a recognized mapping is present. |
| `input_pad_down(pad, button) -> bool` / `input_pad_pressed(...)` / `input_pad_released(...)` | Button held / edge-down / edge-up. |
| `input_pad_axis(pad, axis) -> number` | Stick axes return `-1..1` (radial dead-zone 0.20 removed, rescaled); triggers return `0..1`. |
| `input_pad_name(pad) -> string` | OS/driver-reported controller name (`""` when absent). |

Buttons: `"a"`, `"b"`, `"x"`, `"y"`, `"left_bumper"`, `"right_bumper"`,
`"back"`, `"start"`, `"guide"`, `"left_thumb"`, `"right_thumb"`,
`"dpad_up"`, `"dpad_right"`, `"dpad_down"`, `"dpad_left"`.
Axes: `"left_x"`, `"left_y"`, `"right_x"`, `"right_y"`,
`"left_trigger"`, `"right_trigger"`. Hot-unplug mid-frame simply reads
neutral; pressed/released edges are produced against the previous snapshot.

### Window

| Function | Description |
|---|---|
| `window_quit()` | Ask the host window to close after the current frame; ends `Run()`. |
| `window_set_title(text)` | Update the OS title bar text. |
| `window_set_fullscreen(flag)` | Enter/leave borderless fullscreen on the primary monitor; the windowed geometry is saved and restored on exit. |
| `window_fullscreen() -> bool` | Current fullscreen state. |

All four are safe no-ops in headless/CI runs (no window surface exists).

### Time

| Function | Description |
|---|---|
| `time_delta()` | Seconds since the previous frame (same value as `dt`). |
| `time_elapsed()` | Seconds since `Engine::Init`. |
| `time_frame()` | Frame index (0-based counter). |

### Physics

A tiny built-in arcade physics world, auto-stepped once per frame before
`update` (4 substeps, semi-implicit Euler). Bodies are circles or AABBs,
static or dynamic, with gravity scale and restitution. This is not Box2D:
there are no joints or continuous collision, but zero external dependencies
and zero per-frame allocations.

| Function | Description |
|---|---|
| `phys_create_box(x, y, hw, hh) -> id` / `phys_create_circle(x, y, r) -> id` | Dynamic body; ids are integers, 0 invalid. |
| `phys_set_static(id, bool)` | Static bodies have infinite mass and don't move (set before play). |
| `phys_set_sensor(id, bool)` | Sensors report contacts but never collide/respond. |
| `phys_set_restitution(id, e)` | Bounciness 0–1 (the pair uses the smaller value). |
| `phys_set_gravity_scale(id, s)` | 0 = weightless. |
| `phys_set_pos / phys_set_vel(id, x, y)` | Teleport / set velocity. |
| `phys_set_user(id, n)` | Game tag (e.g. a brick index) surfaced on contacts. |
| `phys_x / phys_y / phys_vx / phys_vy(id) -> n` | Current state, read after the engine stepped. |
| `phys_destroy(id)` | Remove a body. |
| `phys_gravity(gx, gy)` | World gravity (default `0, 900`). |
| `phys_contact_count() -> n` | Pairs that **began** touching this frame. |
| `phys_contact_user_a / phys_contact_user_b(i) -> n` | User tags of pair `i`. |

```lua
ball = phys_create_circle(100, 100, 10)
phys_set_restitution(ball, 0.7)
ground = phys_create_box(100, 300, 200, 10); phys_set_static(ground, true)
-- every frame after the auto-step:
sprite_draw(ball_tex, phys_x(ball) - 10, phys_y(ball) - 10, 20, 20)
for i = 0, phys_contact_count() - 1 do ... end
```

### Frame animation

| Function | Description |
|---|---|
| `anim_create() -> id` / `anim_destroy(id)` | Allocate/free a clip (pool of 32, 64 frames each). |
| `anim_frame(id, tex, sx, sy, sw, sh)` | Append a texture sub-rectangle frame. |
| `anim_fps(id, fps)` | Playback rate. |
| `anim_loop(id, bool)` | Loop, or freeze on the last frame. |
| `anim_play(id)` / `anim_stop(id)` | Restart at frame 0 / halt. |
| `anim_finished(id) -> bool` | One-shot clip ran to completion. |
| `anim_draw(id, x, y, w, h)` | Draw the current frame; the engine advances time itself. |

### Tilemap

Load an orthogonal map exported by [Tiled](https://www.mapeditor.org/) as JSON.
Tile custom properties with `solid = true` (boolean) mark collidable tiles.

| Function | Description |
|---|---|
| `map_load(path) -> id` | Loads tileset images relative to the JSON (cached); 0 on failure. |
| `map_draw(id)` | Draw all visible layers (internally sorted, one draw per tilesheet). |
| `map_solid(id, col, row) -> bool` | Solid cell test. |
| `map_oneway(id, col, row) -> bool` | One-way platform cell (solid when landing from above). |
| `map_slope_dir(id, col, row) -> int` | Slope cell: 0 none, 1 rises toward +x, 2 lowers toward +x. |
| `map_ground_y(id, world_x, reach_y) -> float` | Highest walkable surface y in the ±1-tile window around `reach_y` (solid tops + interpolated slopes), or -1. |
| `map_tile(id, col, row) -> gid` | Raw tile gid at a cell (0 = empty); rewrite gameplay with `map_set_tile`. |
| `map_set_tile(id, col, row, gid)` | Overwrite one cell (coin pickups, breakable blocks). |
| `map_cols(id) / map_rows(id) -> n` | Map dimensions in cells. |
| `map_tilew(id) / map_tileh(id) -> n` | Cell size in pixels. |
| `map_layer_count(id) -> n` | Number of parsed layers (tile + image, in Tiled order). |
| `map_layer_parallax_x/y(id, idx) -> f` | Layer parallax factors (defaults 1.0). |

`col`/`row` are accepted as floats and truncated to int at the native
boundary, so passing `math.floor(x / map_tilew(id))` directly is safe
(FakeLua's `math.floor` returns a float).

#### Slope tiles (kinematic controllers)

Mark a Tiled tile with the custom properties `slope` (string `"up"`/`"down"`,
relative to +x) and optional `slope_rise` (int pixels, default = tile height:
full-height 45° triangle; e.g. `slope_rise = 16` makes a shallow bump).
Slope tiles are **not** solid: `map_solid` returns false for them, so they
neither fill the physics world nor block horizontal movement. Walk on them
with two queries:

- `map_slope_dir` lets the horizontal sweep skip slope cells (never treat a
  slope as a wall).
- `map_ground_y(x, reach_y)` returns the interpolated surface height, taking
  both solid cell tops and slopes; probe the box's back/center/front and snap
  the feet to the highest reachable surface. Step up at most ~8px per frame
  when walking uphill, and snap down while falling onto a downhill surface.

Slope tiles sit in the cell **above** the ground whose surface they meet:
an `up` slope placed on row r rises from the top of row r+1 to the top of
row r; pair it with a `down` tile at the next cell for a pyramid bump. The
platformer demo (`scripts/platformer_demo.lua`) ships a complete, tested
implementation of this controller (`ground_hint`, `move_x`, `move_y`).

##### Building a multi-cell hill

A slope's surface spans `slope_rise` pixels, anchored at
`cell_bottom + rise - tile`. With the default rise (one cell) that is just the
cell bottom, so a ramp's surface stays inside its own cell, and a run of ramps
chains only if each column starts where the previous one ended: step the ramps
**up one row per column** (col 3 on row 12 runs 416 → 384, col 4 on row 11 runs
384 → 352, ...). The walkable surface is then a clean continuous diagonal.
Ramps in the **same row** never chain: they all start at the same height and
repeat one identical incline, whatever `slope_rise` is.

`slope_rise` taller than a cell steepens the ramp and stretches its sprite
(`Tilemap::Draw` draws it over `rise` pixels so art and physics agree), but
chaining scales with it: consecutive columns must step up `rise / tile_height`
rows (two rows per column for `slope_rise = 64`). `map_slope_rise(id, col, row)`
returns the span so a script can check it. For a plain 45° hill the default rise
with one-row-per-column is the simple, correct choice.

**Do not cap the crest with a solid cell.** An `up` ramp's upper end and the
`down` ramp's lower end already meet exactly across a column boundary (col 5
ends at 320, col 6 starts at 320), so no cap is needed. A cap is actively
harmful: the player's box is 28px tall and the ramp only lifts it 1px per px, so
the body still covers the crest's row when the feet are at ramp height, and the
horizontal sweep is blocked by the very cell it is standing on. On a 45° hill
the resulting step is ~10px and a step-up assist can carry it; on a `rise = 64`
hill it is 20px, which a 16px assist cannot, and the player stops dead halfway
up. (A cap *within one cell* — `up` then `down` in the same column — is the
V-notched pyramid and does need a flat top.)

**Never mark a slope cell `solid`, and never mark the fill under one either.**
The query keeps the *lowest* candidate in its window, so a solid cell's top
(`row * tile_height`) both pins `map_ground_y` to flat ground and — because
`box_hits_solid` sees it too — blocks the player's horizontal sweep at the first
column boundary. A hill whose body is packed with solid dirt is unwalkable: the
player walks two steps and stalls. Pack the body with **decorative** dirt (no
`solid` property at all) and let `map_ground_y` carry the player's weight.

Two more things that decide how the hill looks:

- **Pack the body with unpatterned dirt, starting below the ramp's whole span.**
  A ramp only covers its own span, so the cells it passes through need a body,
  and the pack must start `rise / tile_height` rows below the ramp's row — a
  `rise = 64` ramp is drawn over two rows, so packing from one row below
  overdraws its lower half and cuts a horizontal seam across the hill. Do not
  use a brick-patterned fill either: a mortar course directly beneath the lit
  edge draws a square shoulder right beside the diagonal, which is most of what
  makes a hill look like steps.
- **Fill the sprite on the underside of the diagonal.** For `up`, the surface
  runs from the cell's bottom edge at `t=0` to its top edge at `t=1`, so the
  material is the lower-right triangle (larger tile-local y). Filling the sky
  side instead leaves a see-through notch right under the walked surface and
  makes the hill read as floating steps — this, not the geometry, is what
  turned the first hill into a staircase.

`GroundYAt` also only scans the rows within one tile of `reach_y` and rejects
candidates outside `[reach_y - tile, reach_y + tile]`. A single query with
`reach_y` at the feet therefore misses a ramp cell one row up — which is
exactly the cell you are climbing onto. Probe both sides and keep the nearer
answer:

```lua
local function ground_probe(wx, feet)
    local lo = map_ground_y(map_id, wx, feet - TILE)   -- ramp above the feet
    local hi = map_ground_y(map_id, wx, feet + TILE)   -- ground below
    if lo >= 0.0 and hi >= 0.0 then
        if hi < lo then return hi end
        return lo
    end
    if lo >= 0.0 then return lo end
    return hi
end
```

A reach far from the feet (e.g. `feet + 96`) puts the real surface outside the
window and returns -1 for every column, which reads as "no ground" rather than
as a bug. Keep the reach within a tile or two. The snap that consumes this probe
should be gated on the **window only**, not on "is there a slope cell in the
feet's row": a `rise = 64` ramp's cell sits two rows *above* the feet while the
player is still at the bottom of the climb, so a slope-cell check silently
disables the snap and the hill cannot be climbed at all.

`--slope-demo` (`scripts/slope_demo.lua`) is the reference implementation and a
walkthrough: three hills (45°, `rise = 64`, and a long 45°), a HUD that shows
the live `col / feet / slope / rise` readout, and **V** to overlay the surface
the engine reports — red dots sampled straight from `map_ground_y`, which makes
the interpolation visible along each diagonal. `SLOPE_TEST_RIGHT=1` drives it
headless and logs the feet height every 30 frames so a climb can be verified
without a keyboard. The platformer demo
(`scripts/platformer_demo.lua`, `ground_hint`) is the reference controller.

#### Parallax and image layers

Tiled layer properties are honored automatically by `map_draw`:

- Tile layers read `parallaxx`/`parallaxy` (default 1.0) and
  `offsetx`/`offsety`; factors below 1 scroll slower than the camera (far
  background), above 1 faster (foreground).
- Image layers (`type: "imagelayer"`) are drawn interleaved in the layer
  order of the Tiled JSON; `repeatx`/`repeaty` tiles the image across the
  viewport. Missing images drop the layer without breaking the map.

Parallax is a 2D translate effect: it is exact at zoom 1 with no camera
rotation or trauma shake; non-unit zoom scales the effective rate, rotated
cameras rotate parallax layers, and shake does not move them. Keep camera
zoom/rotation at identity for levels that rely on pixel-exact parallax.
Image layers are visual only — cell queries (`map_solid`, `map_ground_y`,
…) skip them.

### Save storage

Persistent number/string key/value slots, written as JSON under the engine
save directory (default `saves/`, configurable via `EngineConfig.save_dir`).
Slot names are restricted to `[A-Za-z0-9_-]{1,32}` — saves cannot escape the
save directory, and illegal names return false.

| Function | Description |
|---|---|
| `storage_load(slot) -> bool` | Replace memory with `<save_dir>/<slot>.json`; a missing file loads as an empty store. |
| `storage_save(slot) -> bool` | Atomically write memory to the slot file. |
| `storage_set_num(key, v)` / `storage_set_str(key, s)` | Write a value (overwrites type). |
| `storage_get_num(key, default) -> n` | Read; missing/wrong-type keys return `default`. |
| `storage_get_str(key, default) -> s` | Same for strings. |
| `storage_has(key) -> bool`, `storage_delete(key)`, `storage_reset()` | Presence, erase key, clear memory. |

Typical use: call `storage_load("slot1")` once at first frame, read settings
throughout play, and `storage_save("slot1")` on level/score changes.

### Entity store

A C++-owned fixed-slot database that survives FakeLua's per-frame arena
reset: it is the recommended storage for dynamic game objects (enemies,
pickups, moving platforms) — 256 slots, each with an integer tag, 8 number
slots and 4 string slots. Ids are 64-bit packed (slot + generation); 0 is
invalid, and destroyed generations report inactive.

| Function | Description |
|---|---|
| `ent_create(tag) -> id` | Allocate (returns 0 when full). |
| `ent_destroy(id)`, `ent_clear()` | Free one / all (generation bumped). |
| `ent_active(id) -> bool` | False for stale/destroyed handles. |
| `ent_tag(id) -> t`, `ent_set_tag(id, t)` | Integer group label. |
| `ent_num(id, slot) -> n`, `ent_set_num(id, slot, v)` | 8 double fields (0..7); out of range reads 0. |
| `ent_str(id, slot) -> s`, `ent_set_str(id, slot, s)` | 4 string fields (0..3); out of range reads `""`. |
| `ent_count([tag]) -> n` | Active entities, optionally filtered by tag; pass -1 for all. |
| `ent_at(index[, tag]) -> id` | Stable slot-order enumeration (0-based); pass tag -1 for all. |

Pattern: on the first frame `ent_create` your level population once; every
frame enumerate with `ent_count`/`ent_at` (allocation-free), run AI/phys on
the numeric fields, and draw per entity. Do **not** cache derived tables
across frames — cache the ids (numbers) only.

### Custom shaders

Per-draw GLSL shaders for tint/effect work. Eight slots; 0 means the default
shader. A custom shader must match the built-in vertex contract: vertex
attributes `a_pos` (vec2, loc 0), `a_uv` (vec2, loc 1), `a_color` (vec4,
loc 2); uniforms `u_view_projection` (mat4) and `u_texture` (sampler2D,
bound to unit 0 automatically). Declare your own uniforms on top.

| Function | Description |
|---|---|
| `shader_load(vert_path, frag_path) -> id` | Compile from files; returns 0 on failure (error printed to stderr). |
| `shader_destroy(id)` | Free the slot. |
| `shader_set_float(id, name, v)`, `shader_set_int(...)`, `shader_set_vec2(id, name, x, y)`, `shader_set_vec4(id, name, r, g, b, a)` | Set uniforms; unknown names are silently ignored. |
| `draw_use_shader(id)` | Route subsequent draws through the shader; pass 0 to restore the default. |

`draw_use_shader` flushes the current batch immediately, so scope it tightly
around the objects that need the effect and avoid calling it per sprite.
Switching restarts the batch, which resets blend/layer/z draw state — set
`draw_set_blend` again afterwards if needed. The override never carries
across frames (each frame starts on the default shader). There is no
fullscreen post-processing (no render targets); apply effects per object.

A worked example lives in `assets/flag_shimmer.{vert,frag}` and is used by
`scripts/mario.lua` to make the goal pole shimmer: the fragment stage computes
a wrapped distance to a band that travels along the quad diagonal, driven by
`u_time` / `u_speed` / `u_width`, and re-tintable per level via `u_tint`.

```lua
local fx = shader_load("assets/flag_shimmer.vert", "assets/flag_shimmer.frag")
shader_set_float(fx, "u_speed", 0.35)
shader_set_float(fx, "u_width", 0.14)
shader_set_vec4(fx, "u_tint", 1.0, 0.96, 0.78, 1.0)

function update(dt)
    shader_set_float(fx, "u_time", time_frame())
    draw_use_shader(fx)
    draw_quad(px, py, 6.0, 288.0, 1.0, 1.0, 1.0, 0.55)
    draw_use_shader(0)   -- back to the default program
end
```

### UI

Anchored immediate widgets. Anchor ids: `0` top-left, `1` top-right,
`2` bottom-left, `3` bottom-right, `4` center; offsets are logical points
from the anchored corner.

| Function | Description |
|---|---|
| `ui_panel(anchor, ox, oy, w, h, r, g, b, a)` | Solid panel quad. |
| `ui_label(anchor, ox, oy, text, scale, r, g, b, a)` | Auto-measured text. |
| `ui_button(key, anchor, ox, oy, w, h, text, scale) -> bool` | Returns true exactly on the click-release frame; `key` keeps hover/press state. |
| `ui_set_origin(x, y)` | Offset added to mouse positions during button hit-testing (set to the camera top-left for a world-anchored HUD). |

Widgets draw through the camera transform, so passing world coordinates as
offsets (e.g. `cam_x + 16`) anchors a HUD to the world. For clickable
world-space buttons, call `ui_set_origin(cam_x, cam_y)` before the widgets
so mouse hit-testing shifts with the camera — the origin is **not** reset
automatically; screen-space HUDs leave it at `0, 0`.

### Debugging

| Function | Description |
|---|---|
| `log_number(v)` | Print a number to stderr — the script-side printf for headless testing. |

The host binary also accepts `--entry <relpath>` to run any script instead
of a shipped demo (dev/test hook), e.g.
`./fake2d_hello --headless --entry scripts/my_smoke.lua --frames 300`.

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
7. **Enable sorted batching for texture-hopping scenes** with
   `batch_set_sorted(true)` and separate overlapping elements via
   `draw_set_layer`; the benchmark shows ~674 → 8 draw calls for 768 sprites
   across 8 textures. Keep it off when a frame deliberately interleaves
   opaque overlays across textures (e.g. full-screen dim panels).
8. **Use additive blending for glow** — `draw_set_blend(1)` around fire,
   sparks and explosions, then back to `0`; remember blend state resets to
   alpha on every frame.

---

## Known FakeLua pitfalls (engine version 2.0.0)

> **Status (2026-10-04, fakelua `a55a4bf` installed):** all five previously
> recorded codegen bugs are fixed upstream, and the two workarounds that existed
> only to dodge them have been **reverted** in `scripts/platformer_demo.lua`:
> `build_intent(frame)` now takes its frame argument again (the multi-return
> type-specialization bug is gone), and the hoisted `shake_amt` / `up_row` /
> `down_row` temporaries are gone (the trailing-argument codegen bug is gone).
> The `bool`/`va_start` UB and the `main` symbol clash were fixed earlier.
> Full history and verification in
> [FAKELUA_JIT_BUG_REPORT.md](FAKELUA_JIT_BUG_REPORT.md).
>
> The persistence rules 1–4 below are **not** bug workarounds — they are current
> FakeLua 2.0 semantics and still required. In particular the `+ 0` idiom
> remains necessary: `local x = 0` compiles to a `static const` and reassigning
> it is rejected with "cannot reassign file-level constant". See
> "Not a bug" below for the full classification.

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
- **`math.floor`/`math.ceil` return floats.** Tile indices derived from
  pixel coordinates are therefore doubles; every `map_*` cell binding
  accepts floats and truncates natively, but a Lua-side `for r = r0, r1 do`
  over them works too because the bounds are whole numbers.
- Semantics differ from stock Lua where listed above; when in doubt, test
  the pattern headlessly first:
  `./build/bin/fake2d_hello --headless --frames 100` plus `log_number`.

### Not a bug: current FakeLua 2.0 semantics

These look like workarounds and get mistaken for stale bug fixes, so they carry
their own heading. **All verified against `a55a4bf` on 2026-10-04 — do not
"clean them up".**

- **A literal `local x` at file level is a read-only constant.** Reassigning it
  fails with `cannot reassign file-level constant 'x'`, *not* the older GCC
  `const-qualified type` error. Use an expression initializer (`0 + 0`) for
  anything mutable. This is why the `+ 0` idiom appears on every mutable
  numeric in the samples — 60 occurrences across the 5 scripts.
- **A table built from an empty constructor and then field-assigned is
  const.** `local t = {}` followed by `t.hp = 10` throws
  `attempt to modify a const table`, *including inside* `update` — so this is
  not the "per-frame tables are free" case (rule 7). It is a type-inference
  artifact, and the shape matters:

  ```lua
  local t = {}          ; t.hp = 10     -- ERROR: const table
  local t = {a = 1}     ; t.b = 2       -- ok
  local t = {}          ; t[1] = 5      -- ok (index write)
  local t = {}          ; table.insert(t, 7)  -- ok (native write)
  ```

  `scripts/game.lua:build_powers()` relies on the `t[i] = v` form and is
  correct — do not "fix" it to a field write.

  Additional file-level findings (GCC JIT, verified 2026-10-06 while building
  a native-API test harness): when a file-level empty table is mutated from
  another function (an upvalue), only a **dynamic key** write is allowed:

  ```lua
  local t = {}
  function set(k, v) t[k] = v end     -- ok: k is a variable
  set("a", 1)                         -- ok
  function bad()  t["a"] = 1  end     -- ERROR: string-literal bracket key
                                      -- is the same as t.a = 1 (field write)
  function del(k) t[k] = nil end     -- ERROR: deleting also modifies const
  ```

  Use numeric/boolean marker values (e.g. store a frame id, compare later)
  instead of inserting/deleting keys, or keep the state in C++ slots
  (`ent_set_num` / `SaveStore`).
- **A file-level `local` with no initializer is rejected**
  ("global constant must be initialized").
- **Undeclared names are a hard compile error** now
  (`unknown variable 'x'; fakelua has no implicit globals`) rather than
  silently miscompiling to `kNil`. This is the intended fix for the old
  bug 5, so the sample scripts' universal `local` discipline is required
  style, not a workaround.
- **The `+ 0` idiom is not needed for bools** — `local done = false` and
  then `done = true` works.

### Reverted (were bug workarounds, no longer needed)

Kept for reference so the old code is recognizable in review or in older
branches:

- ~~Multi-return helpers must take no typed parameters.~~ **Reverted** —
  `598632f` added return-shape eligibility checks, so a multi-return local
  function with typed parameters is safe again. `build_intent(frame)` in
  `scripts/platformer_demo.lua` now takes the argument.
- ~~Hoist arithmetic out of a call's trailing-argument slot.~~ **Reverted** —
  `ef43eae` made `CompileExp` evaluate into a local before emitting, so
  `camera_shake(math.min(0.5, impact / 1200.0))` and
  `map_solid(map_id, ahead, feet_row - 1)` both compile fine again.

