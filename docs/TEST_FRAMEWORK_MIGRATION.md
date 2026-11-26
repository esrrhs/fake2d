# Test-framework migration

Scope: **framework migration integrated with current master**. Existing engine/UI
assertion predicates, stress seeds, simulation frame budgets and complexity gates
are preserved, including the upstream two-pass/modern-UI regression additions. No
product `src/` or public `include/` implementation changes are part of this migration.
The explicitly repaired launcher capture timing is documented below.

## Implemented first group

| Existing checks | Native destination | Preserved execution |
|---|---|---|
| `coverage_regression.cpp` | 4 Google Test cases | snapshot, GL loader, procedural/overlay, Lua UI |
| `coverage_gap_tests.cpp` | 7 Google Test cases | RuleHost/character, shared renderer, original asset/skeleton/layout checks |
| `coverage_deep_tests.cpp` | 29 Google Test cases | core + crash/fork before graphics, shared renderer, then Engine/Lua |
| `win_macro_compat.cpp` | 1 Google Test case | unchanged macro injection/include order and runtime predicate |
| original four UI benchmark scenarios | Google Benchmark `UI/*` | eight warmup + 30 measured frames each; original frame operations and all counter gates |

Each original throwing check now contains a native `EXPECT_TRUE` at the original
assertion site. A standard Google Test `AssertionException` listener unwinds
non-void helpers/Lua callbacks on failure; it does not recursively throw on
framework exception diagnostics. Original assertion evaluation counters remain
for migration-integrity verification, not as a coverage measure.

The existing ordered resource phases are preserved. Google Test suite fixtures
own the window before renderer/textures and destroy graphics resources before
the window. Temporary files live through the original phases. Normal environment
tear-down removes them; a forked child calling `std::exit` must not remove the
parent's fixture directory via a global destructor. Default CTest execution
retains the original phase order; shuffled/individually filtered shared-data
phases are not a completed migration guarantee yet.

Google Benchmark receives the original steady-clock frame-submission interval
through `SetIterationTime` / `UseManualTime`, with real `benchmark::State`
iterations and optimizer barriers. This is **not GPU execution time**. Counter
failures are reported with `SkipWithError` and a nonzero process exit. The UI
workload has no timing threshold. The migrated lighting trace retains its original
six gates, including its existing capped/full relative-time check. CTest `fake2d-ui-bench` runs only the new
benchmark binary, not both old and new workloads.

```
ctest --test-dir build -R 'fake2d-(coverage-|win-macro|ui-bench)' -V
./build/bin/fake2d_ui_benchmark --benchmark_out=ui-bench.json --benchmark_out_format=json
```

## Dependencies and remaining cut-over

Test-only immutable official pins: Google Test v1.17.0
`52eb8108c5bdec04579160ae17225d66034bd723`; Google Benchmark v1.9.4
`31c66f47f60d06790a4885a261d682ba339bd100`. Neither framework is fetched when
`FAKE2D_BUILD_TESTS=OFF`, installed/exported with fake2d, or required by consumers.

## Second group (native engine, stress and performance cut-over)

| Existing entry | Native destination | Same workload / predicates |
|---|---|---|
| 31 `fake2d_hello --*-test` suites | `fake2d_engine_tests`, `EngineContracts.*` / `Launcher.catalog` | 5592 current-master check evaluations |
| `--ui-stress` seeds 42 / 1492026 | `Seeds/UIStress.states/Seed*` | 300 frames and 2110 checks for each seed |
| `--bench` | `fake2d_benchmarks --benchmark_filter=SpriteBatchTrace` | 768 sprites, 8 textures; 30 warmup + 90 measured frames per phase |
| `--phys-bench` | `fake2d_benchmarks --benchmark_filter=PhysicsTrace` | 600 dynamic + 9 static bodies; 60 warmup + 240 measured frames per phase |
| `--light-bench` | `fake2d_benchmarks --benchmark_filter=LightingTrace` | original single/full/capped scenes; 20 warmup + 120 measured frames, six gates |
| installed consumer's five return-on-error predicates | `InstalledConsumer.PublicAPI` | identical inputs and short-circuit order; test-only Google Test |

Each engine check callback now executes a native `EXPECT_TRUE` at its actual
call site, with `source_location` traces, not a subprocess wrapper. Per-case
fixtures own and destroy their Engine/graphics context. Stress keeps the original
vsync setting. Original post-argument default game script / JIT initialization is
retained (option branches clearing the script did not bypass the original later
headless fallback). CTest keeps the original suite names and executes each case
once, with unique XML files. Docs reuse that log rather than rerunning suites.

The three legacy performance traces use one `benchmark::State` iteration for
one complete original fixed trace, preserving all original warmup/sample counts.
Manual time is the sum of original timed submission/physics intervals; the old
per-frame reports and non-time counters remain available. Initialization and
framework adapter bookkeeping do not become benchmark work. Exceptions/errors
produce nonzero status. There is no custom second performance runner.

All old test/stress/benchmark CLI parsing and execution paths have been removed
from the demo executable. Shared demo helpers remain a private, noninstalled
header; no product `src/` or public `include/` implementation changed. Staged
verification uses framework binaries against staged assets/libraries, while
packaged demo launchers retain relocation/strict-asset verification. Consumer
verification alone fetches its test-only framework; normal installed consumers
have no new dependency.

## Third group: native external/integration checks

All external predicates now belong to `fake2d_integration_tests`: real Google
Test cases with individually located exit/status, positive/negative log,
framebuffer-sample colour, screenshot-file, layout-metric and raw-state assertions.
Python scripts only prepare declarative inputs, launch processes and transport raw
results. Windows uses Unicode `CreateProcessW` and the correct native argument
quoting; POSIX uses fork/exec/wait, never a shell. Child execution/log collection
continues through the original Python platform wrapper (Xvfb/CGL/llvmpipe).
Fixtures isolate scratch/capture files, then publish the original CI artifacts.

| Original gate | Native cases / owner |
|---|---|
| GPU core/extended workloads | 21/33 `GpuSmoke` cases, same three Google Benchmark invocations |
| Dynamic UI + migrated callers | 4 `DynamicUI` +13 `MigratedUI`; original markers and all metric predicates |
| Catalog script runs | 35 `CatalogDemo`, same caller frames/interp/AI parameters |
| Linux long milestones / inverse script error | 19 `LinuxMilestone`, original workloads/environment/markers; launcher capture repaired below |
| Headless gameplay | 24 `HeadlessGameplay`, same 8 scenarios ×3 backends; **27 original membership/range/absence predicates per backend** now native C++ |
| Optional UI padding / generated update shapes | 1 `HeadlessFixtures` case, all original six adapter regressions and script-shape checks |
| Actual GCC block-counter regressions | 1 `CoverageReporter`, all 13 original gate groups; production meter unchanged |
| Staged package | 15 `StagedPackage`, asset/launcher checks, original demos, strict missing-texture inverse and unchanged asset tree |
| Documentation | 1 `Documentation`, six symbols, original 12 count patterns and per-suite PASS ledgers; cached CTest log reused |
| Installed consumer | 2 `InstalledConsumer`, original five API predicates plus the three former shell metadata/version checks |

The old Lua headless assertion runner is removed: it now emits only raw unique
numeric observations and completion telemetry after the same simulation frames.
Google Test itself checks the original exact codes/ranges/forbidden codes. The
demo colour option now emits the same actual `DistinctFramebufferColors()` sample
count; Google Test owns the unchanged threshold. This preserves the physical GL
readback/sample grid, rather than substituting a different PNG sample algorithm.

## Integrated Linux verification and launcher capture repair

The old launcher command exited on its twentieth script update, before the default
frame-30 screenshot. Its former shell check ran under `set +e` and did not propagate
the absent image into the final exit code. Native Google Test correctly exposed it.
The launcher milestone now explicitly uses `--shot-frame 19`: engine frames are
zero-based, so this captures after script update20 has selected and drawn the
Platformer launcher screen, but before the next loop observes the close flag.
The40-frame budget, selection input, colour threshold12, screenshot and launch-log
assertions remain unchanged. No business exit delay, dummy frame, skip or fake image.
The actual PNG was decoded as960x540 with8343 distinct RGB colours; target and full
suite images were identical. The original case failed and the repaired case passed.

On2026-11-24 (Asia/Shanghai), the complete local serial chain exited0:
- Release CTest38/38; coverage CTest39/39, including the conservative gcno/block
  reporter counterexamples; both stress seeds and original benchmark gates.
- Native docs1/1, dynamicUI17/17, installed consumer2/2, standalone headless25/25.
-35 catalog demos at400frames,33 extended GPU jobs and19/19 Linux milestones.
- Counter-clean strict owned-library line coverage23950/25195 =95.05854%, gate95%.

Scope remains Linux/GCC with FakeLua enabled and the proprietary Steam SDK disabled;
all active owned sources and emitted owned inline bodies remain in the denominator.
macOS/Windows and full staged-package verification were **not rerun on this
integrated snapshot**. Local Linux success is not a remote CI result. Publication
is on a feature branch/PR, with no automatic merge.

Local first-group mapping, reversible assertion-body comparisons, original/new
runtime counts, compiler/artifact hashes, raw XML/JSON and test logs are under
`reports/framework-migration/round1/`. The library denominator, conservative
actual-block correction/counterexamples, full test/demo/GPU chain and strict 95%
line gate are unchanged. Linux results are not macOS/Windows validation.

Second-group reversible source/body proofs, 31-suite mapping, exact runtime
count parity, benchmark before/after logs and native XML are under
`reports/framework-migration/round2/`. Current Linux results do not establish
macOS/Windows execution, and a previous snapshot coverage percentage is not a
new-tree measurement.
