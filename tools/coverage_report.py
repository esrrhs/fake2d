#!/usr/bin/env python3
"""Report all Linux/GCC-owned implementation and public inline lines.

Run after a FAKE2D_COVERAGE=ON build and test run. gcovr scans .gcno as well
as .gcda: never-linked/unexecuted library objects stay in the denominator.
The generated coverage_headers.cpp emits otherwise unused public inline bodies.
"""
import argparse
import hashlib
import tarfile
import json
import os
from pathlib import Path
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from coverage_blocks import block_hits, merge_observed

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir', default='build-coverage')
    p.add_argument('--output', default='reports/coverage/current')
    p.add_argument('--gcovr', default='gcovr')
    p.add_argument('--gcov', default=os.environ.get('GCOV', 'gcov'))
    p.add_argument('--check', action='store_true', help='fail below the requested 95% line target')
    a = p.parse_args()
    build = (ROOT / a.build_dir).resolve()
    out = (ROOT / a.output).resolve()
    out.mkdir(parents=True, exist_ok=True)
    cache = (build / 'CMakeCache.txt').read_text()
    for setting in ('FAKE2D_COVERAGE:BOOL=ON', 'FAKE2D_WITH_FAKELUA:BOOL=ON',
                    'FAKE2D_WITH_STEAM:BOOL=OFF', 'FAKE2D_BUILD_EXAMPLES:BOOL=ON',
                    'FAKE2D_BUILD_TESTS:BOOL=ON'):
        if setting not in cache.splitlines():
            raise SystemExit('Coverage scope requires ' + setting)
    commands = json.loads((build / 'compile_commands.json').read_text())
    owned = sorted({str(Path(c['file']).resolve().relative_to(ROOT)) for c in commands
                    if Path(c['file']).resolve().is_relative_to(ROOT / 'src')})
    inactive_sources = {'src/script/script_host_stub.cpp': 'FakeLua is enabled'}
    all_implementation = {str(p.relative_to(ROOT)) for p in (ROOT/'src').rglob('*.cpp')}
    omitted = all_implementation - set(owned) - set(inactive_sources)
    if omitted:
        raise SystemExit('Owned implementations omitted from build: ' + ', '.join(sorted(omitted)))
    if not owned or not (build / 'coverage_headers.cpp').exists():
        raise SystemExit('Use a complete FAKE2D_COVERAGE=ON build first')
    for c in commands:
        source = Path(c['file']).resolve()
        if source.is_relative_to(ROOT / 'src') and '--coverage' not in c['command']:
            raise SystemExit('Uninstrumented owned source: ' + c['file'])
    # No function/line exclusion markers, no source-code or unreachable-line
    # exclusions. Third-party, generated drivers and tests fail these filters.
    cmd = [a.gcovr, '--root', str(ROOT), '--gcov-executable', a.gcov,
           '--filter', r'^src/', '--filter', r'^include/fake2d/', '--merge-lines',
           '--exclude-pattern-prefix', 'NO_COVERAGE_EXCLUSION_MARKERS_ALLOWED',
           '--json', str(out / 'gcovr-original.json'),
           '--json-summary', str(out / 'summary.json'),
           '--xml', str(out / 'cobertura.xml'),
           '--html-details', str(out / 'index.html'),
           '--txt', str(out / 'coverage.txt'), str(build)]
    (out / 'command.json').write_text(json.dumps(cmd, indent=2) + '\n')
    subprocess.run(cmd, cwd=ROOT, check=True)
    (out / 'gcovr-original-summary.json').write_bytes((out / 'summary.json').read_bytes())
    report = json.loads((out / 'gcovr-original.json').read_text())
    # GCC's block line table can associate a hit block with several source
    # locations while gcov renders its count only at the final location.
    # Recover only unambiguous actual block/location hits; never function hits.
    observations, proofs, ambiguous = {}, [], 0
    notes = sorted(build.rglob('*.gcno'))
    def read_blocks(path):
        hits, evidence, skipped = block_hits(path, a.gcov, ROOT)
        for row in evidence:
            row['notes'] = str(path.relative_to(build))
        return hits, evidence, skipped
    with ThreadPoolExecutor(max_workers=2) as pool:
        for hits, evidence, skipped in pool.map(read_blocks, notes):
            for key, count in hits.items():
                observations[key] = max(observations.get(key, 0), count)
            proofs.extend(evidence)
            ambiguous += skipped
    changes = merge_observed(report, observations)
    (out / 'block-location-proof.json').write_text(json.dumps(
        {'changed_zero_lines': changes, 'observed_blocks': proofs,
         'ambiguous_block_observations_skipped': ambiguous}, indent=2) + '\n')
    normalized = out / 'block-union-input.json'
    normalized.write_text(json.dumps(report) + '\n')
    render = cmd[:cmd.index(str(build))]
    render[render.index(str(out / 'gcovr-original.json'))] = str(out / 'coverage.json')
    render += ['--json-add-tracefile', str(normalized)]
    (out / 'render-command.json').write_text(json.dumps(render, indent=2) + '\n')
    subprocess.run(render, cwd=ROOT, check=True)
    report = json.loads((out / 'coverage.json').read_text())
    reported = {f['file'] for f in report['files']}
    # gl_loader.cpp is Windows-only; image_write.cpp only includes vendored
    # stb_image_write implementation. Neither has owned executable lines.
    # Every other active implementation must appear, even if never linked.
    non_executable = {'src/render/gl_loader.cpp', 'src/render/image_write.cpp'}
    missing = set(owned) - reported - non_executable
    inventory = {'active_sources': owned, 'inactive_configuration_sources': inactive_sources, 'non_executable_on_linux': sorted(non_executable),
                 'missing_sources': sorted(missing),
                 'public_headers': sorted(str(h.relative_to(ROOT)) for h in (ROOT/'include/fake2d').glob('*.h')),
                 'private_headers': sorted(str(h.relative_to(ROOT)) for h in (ROOT/'src').rglob('*.h')),
                 'scope': 'Linux GCC; FakeLua ON; proprietary Steam SDK OFF; src/** plus emitted public inline bodies; inactive platform preprocessor branches are not executable lines'}
    (out / 'inventory.json').write_text(json.dumps(inventory, indent=2) + '\n')
    if missing:
        raise SystemExit('Incomplete denominator: ' + ', '.join(sorted(missing)))
    # gcovr 8.4 JSON intentionally retains per-function/instantiation entries
    # even with --merge-lines. Count each physical source line exactly once,
    # with a hit if any instantiation executed, and cross-check its summary.
    physical = {}
    for f in report['files']:
        per_line = {}
        for line in f['lines']:
            if line.get('gcovr/noncode', False):
                continue
            number = str(line['line_number'])
            per_line[number] = per_line.get(number, 0) + line['count']
        physical[f['file']] = per_line
    (out / 'physical-lines.json').write_text(json.dumps(physical, indent=2) + '\n')
    total = sum(len(lines) for lines in physical.values())
    covered = sum(count > 0 for lines in physical.values() for count in lines.values())
    summary = json.loads((out / 'summary.json').read_text())
    if (covered, total) != (summary['line_covered'], summary['line_total']):
        raise SystemExit('Raw JSON physical-line union and gcovr summary disagree')
    print(f'Owned executable lines: {covered}/{total} = {100*covered/total:.4f}%')
    hotspots = sorted(((sum(count == 0 for count in lines.values()), name)
                       for name, lines in physical.items()), reverse=True)
    (out/'hotspots.json').write_text(json.dumps(hotspots,indent=2)+'\n')
    # Keep the compiler's original notes/counts, including never-executed
    # translation units, so the report can be independently regenerated.
    raw_files = sorted([*build.rglob('*.gcno'), *build.rglob('*.gcda')])
    hashes = {}
    with tarfile.open(out / 'raw-gcov.tar.gz', 'w:gz') as archive:
        for path in raw_files:
            name = str(path.relative_to(build))
            archive.add(path, arcname=name, recursive=False)
            hashes[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    (out / 'raw-gcov-sha256.json').write_text(json.dumps(hashes, indent=2) + '\n')
    versions = {}
    for name, executable in [('gcovr', a.gcovr), ('gcov', a.gcov)]:
        versions[name] = subprocess.check_output([executable, '--version'], text=True)
    (out / 'tool-versions.json').write_text(json.dumps(versions, indent=2) + '\n')
    if a.check and covered * 100 < total * 95:
        print('95% line coverage target NOT achieved', file=sys.stderr)
        return 2
    return 0

if __name__ == '__main__':
    sys.exit(main())
