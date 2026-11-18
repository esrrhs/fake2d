# Owned-library line coverage (Linux/GCC)

`tools/run_coverage.sh` builds Debug (`-O0 -g --coverage`) and runs all CTest
suites, both UI stress seeds, counter benchmark, catalog scripts (400 interpreted
AI frames), dynamic UI screenshot/metric acceptance, and extended GPU smoke checks. Existing release and three-platform
acceptance jobs remain required and unchanged. It then writes raw JSON, Cobertura
XML, detailed HTML, a source inventory, per-file uncovered-line hotspots,
and a `raw-gcov.tar.gz` archive of every original `.gcno`/`.gcda` with SHA-256
hashes and exact reporter/gcov versions,
and **fails below 95% executable line coverage** (not function/branch/change-line
coverage). Template/lambda instantiations are merged by physical file/line
(`--merge-lines`); raw per-instantiation JSON is preserved, and an independent
physical-line union is cross-checked against the summary. This target is work in progress until evidence shows it passes.

Requirements: Linux, GCC and matching gcov/gcov-dump, Python 3.9+, gcovr **8.4**, Ninja,
Xvfb, software Mesa, development dependencies from the Build workflow, and a
FakeLua installation containing NativeObject::GetVersion. Build and Release now
retain upstream's exact pin `e1f27d16d29aa72b79800d4b7beb8fe4d9fe9641`;
headers and library must come from the same checkout/install. The API was first
introduced at `237c48a1fcc6cc65a82378876e5f093dd0030bc8`; that older pin is
historical context, not the dependency selected for the integrated build.

```sh
python3 -m venv .deps/coverage-venv
.deps/coverage-venv/bin/pip install gcovr==8.4
export PATH="$PWD/.deps/coverage-venv/bin:$PATH"
export CMAKE_PREFIX_PATH=/path/to/fakelua/prefix
export LD_LIBRARY_PATH="$CMAKE_PREFIX_PATH/lib:${LD_LIBRARY_PATH:-}"
tools/run_coverage.sh
```

## Denominator

All active fake2d `src/**` translation units are instrumented, including objects
never extracted from the static library: gcovr scans **.gcno as well as .gcda**.
Every public header in `include/fake2d/` and private header in `src/` is compiled in a dedicated object with
`-fkeep-inline-functions`, so unused emitted inline functions remain zero-hit
rather than disappearing. Filters include only `src/` and `include/fake2d/`.
There are no business-code exclusions and exclusion markers are disabled.
Third-party dependencies, generated test drivers, and examples are not library
code and are not counted.

This is **Linux default-configuration executable source-line coverage**:
FakeLua ON, proprietary Steam SDK OFF. Inactive preprocessor branches (Windows,
macOS, proprietary Steam SDK, no-FakeLua stub) and uninstantiated template bodies
have no compiler executable-line mappings and cannot be measured by this build.
It is not a claim of 95% across every platform/configuration. The source inventory
lists the complete active implementation set and public headers; missing active
implementations are an error. `gl_loader.cpp` is Windows-only and has no Linux
executable lines; `image_write.cpp` only includes the third-party stb implementation
and has no owned executable lines. Both remain compiled and are listed in the
inventory. Other platform/configuration tests are not removed.

## Comparing test additions

Use the **same final product build and filters** for both reports. Finish all
compilation before measuring, reset only that build's `.gcda`, and run the existing
tests excluding **only the current round's newly added suite**, plus identical
script/GPU/dynamic-UI checks. Round 1 excluded `fake2d-coverage-regression`; round 2
includes that regression and excludes only `fake2d-coverage-gaps`. Then run the
new suite once and report cumulative coverage to a new output directory. Never
discard `.gcno` for an unused source. Preserve older reports instead of overwriting
baseline evidence. Product fixes can legitimately change executable line counts;
measure baseline and final on that same fixed code and explain denominator changes.

Do not run debugging binaries concurrently against measurement counters. Use a
separate `GCOV_PREFIX` for debug-only executions, or reset counters and rerun the
whole baseline afterwards. Never ignore profile stamp/checksum errors. The raw
archive and `physical-lines.json` allow an independent denominator/hit audit.

```sh
python3 tools/coverage_report.py --build-dir build-coverage --output reports/coverage/final --check
```

An earlier CI regression was not the old `<cstdint>` issue: Build 44083530251
(master 49d5654) compiled snapshot code calling `NativeObject::GetVersion`, but
Build/Release pinned FakeLua 9f7216e, predating that API. The earlier local repair
used its API-introduction revision. Upstream master 875b610e subsequently moved
to e1f27d16, which retains this API; integration keeps that newer upstream pin.
The snapshot regression still checks nested dirty versions, anchor/delta restores,
corrupted checksums, truncated-chain recovery and month re-anchoring.

The latest inspected Build 44231036582 instead failed the Linux documentation
gate: both READMEs advertised 5501 assertions while its actual 31 built-in suites
reported 5519. macOS/Windows and asset jobs passed. Documentation counts must be
updated from the **integrated build's actual complete suite PASS records**, not
copied from a different checkout or confused with library line coverage. New
native/UI coverage regressions remain separate from that 31-suite headline.

The independent Linux coverage job explicitly selects GCC/gcov 14 and gcovr 8.4. `GCOV` selects the matching gcov executable locally; the reporter records its exact version. All CTest targets, including coverage-deep, run through the same unfiltered CI CTest invocation.

## GCC block-location union (ROUND6)

GCC 14 can emit a block's actual hit count only at its final source location,
leaving another location in that same compiler block with only a zero-count
exception-cleanup record. A lambda opening line is one observed example; this is
not fixed by summing gcovr's existing per-line instances. The original gcov JSON
and native text agree on that zero, but `.gcno` `LINES` maps the *actually hit*
body block to both the opening and body locations.

The reporter preserves `gcovr-original.json` and its original summary. It uses
matching `gcov-dump -l` block location tables plus native `gcov --all-blocks`
block visit counters. An observation is usable only when source file, displayed
source line and block id identify one unique function/block in that translation
unit. Ambiguous matches are skipped. Only existing executable zero-count lines
with such a positive block/location witness are corrected; no lines are added,
removed, excluded or inferred from lambda syntax, neighbouring lines, or function
execution counts. `block-location-proof.json` records the witnesses and changes;
`block-union-input.json`, `coverage.json`, HTML, XML, physical union and the 95%
gate use the same corrected data. Original gcno/gcda remain independently archived.

`fake2d-coverage-reporter` is an additional mandatory Linux coverage CTest. It
compiles/runs a real registered-std::function reproduction, checks a genuinely
executed lambda opening is hit, and checks registered-but-uncalled bodies and
never-constructed lambdas stay zero. The merge must preserve every executable
line and be idempotent. Existing tests are retained unfiltered.

Historical clean counters must be transparently recomputed with this same rule
before comparing test improvements; never attribute a meter correction to new
test execution. ROUND4 and ROUND5 untouched raw archives are retained alongside
their independently recomputed reports. The denominator remains unchanged.
