#!/usr/bin/env python3
"""Data-only portable subprocess adapter. No test assertions live here."""
import json
import os
from pathlib import Path
import subprocess
import sys
from ci_common import ROOT, xvfb_prefix


def execute(job):
    if job.get('transport_mode') == 'reporter':
        from coverage_probe import collect
        return collect(job['compiler'], job['gcov'], job.get('evidence_dir') or str(Path(job['scratch'])/'coverage-evidence'))
    if job.get('transport_mode') == 'data':
        return job['data']
    if job.get('hide_file'):
        source=Path(job['hide_file']);backup=Path(job['scratch'])/'hidden-texture'
        source.rename(backup)
        try:
            return execute({k:v for k,v in job.items() if k!='hide_file'})
        finally:
            backup.rename(source)
    env = os.environ.copy()
    for name in job.get('unset_env', []):
        env.pop(name, None)
    env.update(job.get('env', {}))
    command = [job['program'], *job['args']]
    if job.get('display', True):
        prefix = xvfb_prefix()
        if prefix:
            env.setdefault('LIBGL_ALWAYS_SOFTWARE', '1')
        command = [*prefix, *command]
    process = subprocess.run(command, cwd=job.get('cwd', str(ROOT)), env=env,
                             capture_output=True, text=True,
                             encoding=job.get('encoding'), errors=job.get('errors'))
    return {'returncode': process.returncode, 'stdout': process.stdout or '',
            'stderr': process.stderr or '',
            'log': (process.stdout or '') + (process.stderr or '')}


if __name__ == '__main__':
    job = json.loads(Path(sys.argv[1]).read_text(encoding='utf-8'))
    result = execute(job)
    Path(sys.argv[2]).write_text(json.dumps(result, ensure_ascii=False), encoding='utf-8')
