"""Conservative physical-line union from original GCC block locations and hits.

GCC 14 gcov emits one block count at its final source location; intermediate
locations can otherwise retain only a zero-count exception-cleanup block.
Never infer hits from function counts, source syntax, or neighbouring lines.
"""
import re
import subprocess
from collections import defaultdict
from pathlib import Path


def gcov_dump_executable(gcov):
    # CI deliberately uses gcov-14 while the host default may be GCC13.
    # Preserve both cross-toolchain prefixes and the explicit version suffix.
    path = Path(gcov)
    if 'gcov' not in path.name:
        raise ValueError('Expected a GCC gcov executable name: ' + str(path))
    return str(path.with_name(path.name.replace('gcov', 'gcov-dump', 1)))


def block_hits(notes, gcov, root):
    notes, root = Path(notes).resolve(), Path(root).resolve()
    dump = subprocess.check_output([gcov_dump_executable(gcov), '-l', str(notes)], text=True)
    locations = defaultdict(set)
    function = None
    for line in dump.splitlines():
        match = re.search(r'FUNCTION ident=(\d+)', line)
        if match:
            function = match.group(1)
        match = re.search(r'block (\d+):`', line)
        if match and function is not None:
            block = (function, int(match.group(1)))
            for name, numbers in re.findall(r"`([^']+)':([0-9, ]+)", line):
                for number in re.findall(r'\d+', numbers):
                    locations[block].add((str(Path(name).resolve()), int(number)))
    candidates = defaultdict(set)
    for block, places in locations.items():
        for name, line in places:
            candidates[(name, line, block[1])].add(block)
    # --all-blocks exposes actual block visit counters, not function counters.
    text = subprocess.check_output([gcov, '--all-blocks', '--branch-counts', '--stdout', str(notes)], text=True)
    current = None
    hits = {}
    evidence = []
    ambiguous = 0
    for line in text.splitlines():
        if ':Source:' in line:
            current = str(Path(line.split(':Source:', 1)[1]).resolve())
        match = re.match(r'\s*([0-9]+)\*?:\s*(\d+)-block\s+(\d+)\s*$', line)
        if not match or current is None:
            continue
        count, source_line, block_id = map(int, match.groups())
        if count <= 0:
            continue
        choices = candidates[(current, source_line, block_id)]
        if len(choices) != 1:
            ambiguous += 1
            continue
        block = next(iter(choices))
        for name, number in locations[block]:
            path = Path(name)
            if not path.is_relative_to(root):
                continue
            relative = str(path.relative_to(root))
            if not (relative.startswith('src/') or relative.startswith('include/fake2d/')):
                continue
            key = (relative, number)
            if count > hits.get(key, 0):
                hits[key] = count
                evidence.append({'file': relative, 'line': number, 'count': count,
                                 'function_ident': block[0], 'block': block[1],
                                 'observed_file': current, 'observed_line': source_line})
    return hits, evidence, ambiguous


def merge_observed(report, observations):
    """Only change existing executable zero-count physical lines with proof.

    No added/removed line, function-counter inference, or blanket lambda rule.
    Noncode entries and lines already hit by any instance remain untouched.
    """
    changes = []
    for file in report['files']:
        groups = defaultdict(list)
        for line in file['lines']:
            if not line.get('gcovr/noncode', False):
                groups[line['line_number']].append(line)
        for number, instances in groups.items():
            count = observations.get((file['file'], number), 0)
            if count > 0 and not any(line['count'] > 0 for line in instances):
                instances[0]['count'] = count
                changes.append({'file': file['file'], 'line': number, 'count': count})
    return changes
