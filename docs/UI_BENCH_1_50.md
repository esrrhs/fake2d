# UI benchmark — 1.50.0 measured host sample

Measured on 2026-11-10 (Asia/Shanghai), same persistent Linux x86_64 box,
8 visible CPUs, Intel Xeon processor; Debug/assertions, GCC 14.2.0,
Mesa 25.0.7 llvmpipe (LLVM 19.1.7, 256 bits), OpenGL 4.5 core, Xvfb.
Baseline was the already-built 1.49.0 at 7657f354; after was rebuilt 1.50.0.
No native GPU timing and no allocator instrumentation. Shared-host scheduling
noise affects timing: this is a sample, not a universal speed guarantee.

Same command before and after (from repository root):

```sh
xvfb-run -a env LIBGL_ALWAYS_SOFTWARE=1 ./build/bin/fake2d_hello --headless --ui-bench
```

640x480 offscreen target, 8 warmup frames, 30 measured frames. `rich_64_links`
is the identical 64 paragraphs and markup, not a reduced workload.

| rich_64_links | 1.49.0 baseline | 1.50.0 |
|---|---:|---:|
| layout CPU mean, ms | 6.236186 | 2.024679 |
| total CPU mean, ms | 13.376798 | 3.693106 |
| draw calls/frame | 1973 | 322 |
| quads/frame | 1973 | 1781 |
| deferred draw commands max | 2293 | 386 |
| link interaction rectangles max | 256 | 64 |
| glyphs / logical style-line runs | not instrumented | 1587 / 256 |

Draw calls drop 83.7%; the lower quad count comes from one underline per link
run rather than four per-word characters. Glyphs, bold extra pass, colors,
image textures, clip intersections and audit remain. Adjacent glyphs are passed
to DrawText together; the existing SpriteBatch still obeys texture/clip/shader
boundaries. A bounded font-revision/scale-aware advance cache avoids repeatedly
measuring the same glyph. Markup parsing and layout are **not** globally cached;
geometry still recomputes from this frame's width. Rich runs retain the original
unkerned glyph positions; a pixel fixture compares batched bold/color/link output
to individually positioned glyphs. Normal text keeps kerning enabled by default. Image/clip changes may still
split batches. There is no claim of one draw call for the entire paragraph.

The regression gate uses complexity, not CPU time: rich fixture <=320 logical
runs, <=400 draw calls, exactly 64 link rectangles. The Linux/macOS/Windows test
commands run this same gate. Counter output includes quads and logical spans;
GPU driver batching is not inferred from the span count.

## Raw baseline

```
UI_BENCH_CONTEXT renderer="llvmpipe (LLVM 19.1.7, 256 bits)" vendor="Mesa" gl_version="4.5 (Core Profile) Mesa 25.0.7-2+deb13u1" compiler="14.2.0" build="Debug/assertions"
UI_BENCH_CONFIG version=1.49.0 target=640x480 warmup=8 samples=30 timer=steady_clock_cpu gpu_time=not_measured allocations=not_instrumented
UI_BENCH scenario=fixed_1000 layout_ms_mean=0.075690 layout_ms_p50=0.072759 layout_ms_p95=0.098951 interaction_ms_mean=0.504436 total_cpu_ms_mean=1.383395 submitted_rows_min=15 submitted_rows_max=16 nodes_max=16 visible_max=16 draw_commands_max=50 draw_calls_mean=49.9 quads_mean=256.8 tree_steps_max=0
UI_BENCH scenario=variable_1200 layout_ms_mean=0.396240 layout_ms_p50=0.338611 layout_ms_p95=0.570835 interaction_ms_mean=0.474756 total_cpu_ms_mean=2.117982 submitted_rows_min=11 submitted_rows_max=12 nodes_max=12 visible_max=11 draw_commands_max=50 draw_calls_mean=46.5 quads_mean=666.5 tree_steps_max=296
UI_BENCH scenario=rich_64_links layout_ms_mean=6.236186 layout_ms_p50=5.932764 layout_ms_p95=7.199875 interaction_ms_mean=0.379381 total_cpu_ms_mean=13.376798 submitted_rows_min=64 submitted_rows_max=64 nodes_max=256 visible_max=88 draw_commands_max=2293 draw_calls_mean=1973.0 quads_mean=1973.0 tree_steps_max=0
UI_BENCH scenario=nested_scroll_64 layout_ms_mean=2.517047 layout_ms_p50=2.342183 layout_ms_p95=3.121816 interaction_ms_mean=0.012974 total_cpu_ms_mean=8.284188 submitted_rows_min=64 submitted_rows_max=64 nodes_max=0 visible_max=0 draw_commands_max=68 draw_calls_mean=68.0 quads_mean=4561.0 tree_steps_max=0
[ui-bench] PASS: 4 workloads, 0 counter failures
```

## Raw after

```
UI_BENCH_CONTEXT renderer="llvmpipe (LLVM 19.1.7, 256 bits)" vendor="Mesa" gl_version="4.5 (Core Profile) Mesa 25.0.7-2+deb13u1" compiler="14.2.0" build="Debug/assertions"
UI_BENCH_CONFIG version=1.50.0 target=640x480 warmup=8 samples=30 timer=steady_clock_cpu gpu_time=not_measured allocations=not_instrumented
UI_BENCH scenario=fixed_1000 layout_ms_mean=0.090545 layout_ms_p50=0.089627 layout_ms_p95=0.100959 interaction_ms_mean=0.557997 total_cpu_ms_mean=1.114680 submitted_rows_min=15 submitted_rows_max=16 nodes_max=16 visible_max=16 draw_commands_max=50 draw_calls_mean=49.9 quads_mean=256.8 tree_steps_max=0 rich_batches_max=0 rich_glyphs_max=0
UI_BENCH scenario=variable_1200 layout_ms_mean=0.364420 layout_ms_p50=0.366019 layout_ms_p95=0.416521 interaction_ms_mean=0.192751 total_cpu_ms_mean=1.431667 submitted_rows_min=11 submitted_rows_max=12 nodes_max=12 visible_max=11 draw_commands_max=50 draw_calls_mean=46.5 quads_mean=666.5 tree_steps_max=296 rich_batches_max=0 rich_glyphs_max=0
UI_BENCH scenario=rich_64_links layout_ms_mean=2.024679 layout_ms_p50=1.922611 layout_ms_p95=2.584999 interaction_ms_mean=0.103132 total_cpu_ms_mean=3.693106 submitted_rows_min=64 submitted_rows_max=64 nodes_max=64 visible_max=22 draw_commands_max=386 draw_calls_mean=322.0 quads_mean=1781.0 tree_steps_max=0 rich_batches_max=256 rich_glyphs_max=1587
UI_BENCH scenario=nested_scroll_64 layout_ms_mean=2.507452 layout_ms_p50=2.486395 layout_ms_p95=2.704698 interaction_ms_mean=0.011439 total_cpu_ms_mean=8.172465 submitted_rows_min=64 submitted_rows_max=64 nodes_max=0 visible_max=0 draw_commands_max=68 draw_calls_mean=68.0 quads_mean=4561.0 tree_steps_max=0 rich_batches_max=0 rich_glyphs_max=0
[ui-bench] PASS: 4 workloads, 0 counter failures
```

## Convergence / platform record

`--ui-test`: 1200 mixed Chinese/English rows, 64 candidate scans/list/frame,
all exact text metadata measured after the bounded background passes. Scroll into
an unseen middle region and back preserved the visible ID; measured maximum
intra-row anchor displacement was **0.022461 logical pixels** (float-prefix
rounding). Width/theme/font invalidation, content short→long→short, insert/delete,
pin-bottom and a three-row shrink are covered. Initial estimates remain until
the cursor reaches a row, unless visible measurement resolves it sooner. Custom
rows lacking text metadata still use estimates. A fractional scrollbar jump
before convergence is not a semantic-row positioning API.

1.50.0 is at `2c3f6a8`, with the Windows `near`/`far` compile fix at `b35f8aa`.
The Build workflow now requires the shared CTest set (including both stress
seeds and `win_macro_compat`), `tools/check_ui_demos.py`, the catalog
script-error scan, JIT/GL smoke and the 1.50 `find_package` consumer on
Linux, macOS and Windows. macOS CGL UI acceptance is a blocking gate, not
`continue-on-error`. In-memory clipboard UTF-8 and password copy/cut/paste
are tested, not a real Mac/Windows desktop clipboard or physical gamepad.
Those interactions remain unverified here.

IME: pinned GLFW 3.4 public/native headers have no preedit callback/caret
position API. Cocoa's internal markedText does not reach fake2d; X11 requests
XIMPreeditNothing; Win32 exposes committed WM_CHAR/WM_UNICHAR in this code.
Preview/candidate placement stays unsupported; see SCRIPTING.md Phase 72 evidence.


Verification uses `env -u DISPLAY python3 tools/check-docs.py` so the helper starts
its isolated Xvfb. On the inherited box desktop display the unrelated gameplay
`window resizes` assertion fails (also reproduced directly); the Xvfb run tests
that assertion rather than skipping it. This is not evidence of real-desktop
window/clipboard behavior. No GUI behavior was claimed from the headless pass.


### 1.50.0 Linux verification record

- `cmake --build build -j 4`: passes (Debug).
- `ctest --test-dir build --output-on-failure`: **33/33**; UI **3955** checks,
  stress seeds 42 / 1492026 **2110** checks each, rich benchmark 4 workloads /
  zero complexity failures. Includes the bold/color/link pixel-parity fixture.
- `env -u DISPLAY python3 tools/check-docs.py`: **35 demos**, 30 self-test suites,
  **5438 assertions**, all dynamic transitions and migrated caller smokes pass.
- `cmake --install build --prefix ...` + exact workflow consumer: builds/links/runs
  against exported 1.50 APIs, prints **consumer: 1.50.0**.
- `tools/package-linux.sh ...` and `tools/verify-package.sh ...`: **PASS**, strict
  assets, UI/dynamic demo screenshots, stress/benchmark and negative asset proof.
- Physical desktop clipboard/gamepad and IME composition remain
  unverified/unsupported as above. This record is not a real-desktop
  manual acceptance pass.
- The inherited box desktop rejects the gameplay resize assertion; the complete
  docs gate above uses isolated Xvfb and exercises it successfully (no skip).
- Full staged-package `verify-package.sh` remains a Linux Build gate;
  macOS/Windows Build run the shared `find_package` consumer instead.
