# UI benchmark — 1.49.0 measured host sample

This is one actual **Debug** sample, not a portable speed guarantee or a claimed
improvement over an unmeasured baseline. Measured **2026-11-10 20:45:19 Asia/Shanghai**, separately
after this task's parallel verification workloads had finished.

## Environment and reproduction

- Linux 6.12.113+, x86_64, KVM; 8 reported vCPUs, Intel Xeon Processor (virtual
  model string; not a measurement of dedicated physical cores).
- GCC 14.2.0, CMake Debug/assertions; Mesa 25.0.7 llvmpipe / LLVM 19.1.7 software
  renderer, actual GL 4.5 core context. This is **not hardware GPU performance**.
- Offscreen target 640×480; Xvfb screen 1024×768×24; real bundled OFL CJK fallback
  glyphs. Eight warmup frames, thirty measured frames **per workload**.

```sh
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1024x768x24' \
  build/bin/fake2d_hello --headless --ui-bench
```

CPU-side timings use `steady_clock` elapsed scopes (including scheduling), **not
process CPU counters**. Layout includes declaration/measurement and layout
finalization. Interaction includes current-ring discovery/input and native widget
paint-command generation. Total includes NewFrame, renderer/target setup, both UI
phases and draw submission/EndFrame; drivers can block. There are no GPU timestamp
queries. Allocations are **not instrumented**; no zero-allocation claim is made.
Initial 1200-ID configuration is outside timed normal frames. Mutation/width/font
invalidation can rebuild O(N) metadata; that is not a claim of constant-time rebuilds.

## Measured results

All times below are milliseconds; submitted counts are min–max per sample.
`visible` means eligible clipped **interaction nodes**, not the count of text rows.
Rich/nested-scroll workloads deliberately submit all 64 units; they are not virtual.

| Workload | Layout mean / p50 / p95 | Interaction mean | Total mean | Submitted | Eligible nodes max | Draw calls mean | Quads mean |
|---|---:|---:|---:|---:|---:|---:|---:|
| fixed_1000 | 0.066538 / 0.065836 / 0.072784 | 0.452150 | 0.886683 | 15–16 | 16 | 49.9 | 256.8 |
| variable_1200 | 0.328613 / 0.307859 / 0.346828 | 0.191896 | 1.313433 | 11–12 | 11 | 46.5 | 666.5 |
| rich_64_links | 5.443453 / 5.403574 / 5.630846 | 0.308860 | 12.058795 | 64–64 | 88 | 1973.0 | 1973.0 |
| nested_scroll_64 | 2.379590 / 2.363542 / 2.479558 | 0.011035 | 7.717134 | 64–64 | 0 | 68.0 | 4561.0 |

The fixed 1000-row view submits only 15–16 rows; the variable 1200-row view submits
11–12, not all IDs. Variable-row maximum lookup/update tree steps were **296**;
this counter covers binary range searches and height updates, **not prefix-sum
traversals**. Maximum queued commands were 50/50/2293/68 respectively. Renderer
draw calls are actual batch submissions, not inferred from widget counts. In
particular, the rich-text workload exposes many small submissions; this release
does not claim that those are optimally batched.

CTest/CI gate balanced layout, zero unintended overflow, fixed visible counts,
bounded variable submissions, measured-row consistency and lookup/update-step
bounds. They **do not gate absolute wall-clock milliseconds**. Timings vary with
build flags, host load, software/hardware drivers and text/viewport choices.

## Raw output

```text
UI_BENCH_CONTEXT renderer="llvmpipe (LLVM 19.1.7, 256 bits)" vendor="Mesa" gl_version="4.5 (Core Profile) Mesa 25.0.7-2+deb13u1" compiler="14.2.0" build="Debug/assertions"
UI_BENCH_CONFIG version=1.49.0 target=640x480 warmup=8 samples=30 timer=steady_clock_cpu gpu_time=not_measured allocations=not_instrumented
UI_BENCH scenario=fixed_1000 layout_ms_mean=0.066538 layout_ms_p50=0.065836 layout_ms_p95=0.072784 interaction_ms_mean=0.452150 total_cpu_ms_mean=0.886683 submitted_rows_min=15 submitted_rows_max=16 nodes_max=16 visible_max=16 draw_commands_max=50 draw_calls_mean=49.9 quads_mean=256.8 tree_steps_max=0
UI_BENCH scenario=variable_1200 layout_ms_mean=0.328613 layout_ms_p50=0.307859 layout_ms_p95=0.346828 interaction_ms_mean=0.191896 total_cpu_ms_mean=1.313433 submitted_rows_min=11 submitted_rows_max=12 nodes_max=12 visible_max=11 draw_commands_max=50 draw_calls_mean=46.5 quads_mean=666.5 tree_steps_max=296
UI_BENCH scenario=rich_64_links layout_ms_mean=5.443453 layout_ms_p50=5.403574 layout_ms_p95=5.630846 interaction_ms_mean=0.308860 total_cpu_ms_mean=12.058795 submitted_rows_min=64 submitted_rows_max=64 nodes_max=256 visible_max=88 draw_commands_max=2293 draw_calls_mean=1973.0 quads_mean=1973.0 tree_steps_max=0
UI_BENCH scenario=nested_scroll_64 layout_ms_mean=2.379590 layout_ms_p50=2.363542 layout_ms_p95=2.479558 interaction_ms_mean=0.011035 total_cpu_ms_mean=7.717134 submitted_rows_min=64 submitted_rows_max=64 nodes_max=0 visible_max=0 draw_commands_max=68 draw_calls_mean=68.0 quads_mean=4561.0 tree_steps_max=0
[ui-bench] PASS: 4 workloads, 0 counter failures
```

## Release verification on this Linux host

- `cmake --build build -j4`: pass (Ninja; configured Debug build).
- `build/bin/fake2d_hello --headless --ui-test`: **1375 checks, 0 failures**.
- `ctest --test-dir build --output-on-failure --parallel 4`: **33/33**.
  Includes two 300-state stress runs: seeds 42 and 1492026, **2110 checks each**,
  and all four benchmark workload/counter gates. UI tests also run 90 seeded states.
- `env -u DISPLAY python3 tools/check-docs.py`: **35 demos, 30 counted suites,
  512 slots, 2858 assertions**. Four real FakeLua UI demos run 240 frames with
  screenshots, no unintended overflow, transition markers and grow/shrink/range
  metric assertions; all 13 other migrated callers compile/run three-frame smokes.
- `tools/package-linux.sh build/bin/fake2d_hello
  /workspace/deps/fake2d-147/fakelua/lib /tmp/fake2d-149-package-verified`, then
  `env -u DISPLAY tools/verify-package.sh /tmp/fake2d-149-package-verified`: **PASS**,
  including strict assets, four UI demos, stress, benchmark and existing art/game gates.
- Installed-package consumer: exact CI source, `find_package(fake2d 1.49 REQUIRED
  CONFIG)`, no caller C++ standard, configure/build/link/runtime **PASS**; checks
  variable-list configuration, UTF-8 storage and Begin/End, prints **consumer: 1.49.0**.
- Existing gameplay fixture gate: **24/24** TCC/GCC/interpreter cases pass:
  `LD_LIBRARY_PATH=/workspace/deps/fake2d-147/fakelua/lib python3
  tests/headless/run_headless.py --flua /workspace/github/fakelua/build/bin/flua`.
  The already-built dependency CLI was used read-only, with no dependency-repo
  edits. Generation validates **152 custom / 479 automatic** signatures. Fixed
  the latent exact-arity fixture versus optional native UI mismatch by padding
  only generated script copies; lexer assertions preserve nested calls, strings
  and comments and still reject missing required arguments.
- Real launcher: `--headless --launcher --frames 3 --strict-assets`, pass.
- `git diff --check`, Python byte compile and shell syntax checks: pass.

Native macOS/Windows execution was not performed on this Linux host; CI is wired
for the new tests/demos. Headless clipboard uses the existing in-memory adapter,
not proof of a real desktop clipboard. GLFW 3.4 public APIs expose no portable
preedit callback: composition preview/candidate positioning remain unsupported.

The explicit interaction boundary never replays caller code: known content lays
out now; effects consumed after End alter dependent layout at the next declaration.
Legacy unscoped calls retain their old discovery semantics. See SCRIPTING.md for
the finalized-view barrier, estimated-height convergence and anchor contract.
