"""Manifest/launch plumbing only; native Google Test owns every predicate."""
import json
import os
from pathlib import Path
import subprocess
import shutil
import tempfile
from ci_common import ROOT


def case(suite, name, program, args, **extra):
    return dict(suite=suite, name=name.replace('-', '_'), program=str(Path(program).resolve()),
                args=args, **extra)


def run_cases(hello, cases):
    hello = Path(hello).resolve()
    executable = hello.with_name('fake2d_integration_tests' + hello.suffix)
    with tempfile.TemporaryDirectory(prefix='fake2d-gtest-integration-') as tmp:
        root = Path(tmp)
        for i, item in enumerate(cases):
            item['scratch'] = str(root / str(i))
            if item.get('screenshot') and not Path(item['screenshot']).is_absolute():
                old = item['screenshot']
                item['publish_screenshot'] = str(ROOT / old)
                item['screenshot'] = str(root / str(i) / Path(old).name)
                item['args'] = [item['screenshot'] if arg == old else arg for arg in item['args']]
        manifest = root / 'manifest.json'
        manifest.write_text(json.dumps(cases, ensure_ascii=False), encoding='utf-8')
        rc = subprocess.run([str(executable), '--manifest=' + str(manifest),
                             '--gtest_output=xml:' + str(root / 'results.xml')]).returncode
        for item in cases:
            result = Path(item['scratch']) / 'result.json'
            if item.get('publish_log') and result.is_file():
                (ROOT/item['publish_log']).write_text(json.loads(result.read_text(encoding='utf-8')).get('log',''),encoding='utf-8')
            if item.get('publish_screenshot') and Path(item['screenshot']).is_file():
                shutil.copy2(item['screenshot'], item['publish_screenshot'])
        evidence = os.environ.get('FAKE2D_INTEGRATION_EVIDENCE')
        if evidence:
            shutil.copytree(root, Path(evidence) / (cases[0]['suite'] + '-' + root.name))
        return rc


def gpu_cases(hello, jobs):
    hello = Path(hello).resolve()
    return [case('GpuSmoke', item['label'],
                 hello.with_name('fake2d_benchmarks' + hello.suffix) if item['benchmark'] else hello,
                 item['args'], screenshot=item['screenshot'],
                 contains=item['contains'], forbids=item['forbids'], script_errors=True)
            for item in jobs]


UI_DEMO_FAILURE_MARKERS = ('update() failed', '[UI_OVERFLOW]', 'strict assets:', '[ERROR]')
MIGRATED_CALLERS = ('audio', 'beat-em-up', 'fx', 'map', 'platformer', 'post',
                   'shmup', 'snooker', 'text', 'tile', 'ui', 'ui2', 'ui3')


def ui_cases(hello, image_dir):
    jobs = []
    for flag, markers, shot in [('ui-layout', range(148000, 148006), 60),
                                ('ui-features', range(148100, 148103), 100),
                                ('ui-variable', range(149000, 149006), 60),
                                ('ui-stress', range(149100, 149106), 60)]:
        image = str(Path(image_dir) / (flag + '.png'))
        jobs.append(case('DynamicUI', flag, hello,
                         ['--headless', '--strict-assets', '--frames', '240',
                          '--' + flag + '-demo', '--screenshot', image,
                          '--shot-frame', str(shot), '--shot-min-colors', '12'],
                         screenshot=image, script_errors=True,
                         contains=['[script] ' + str(m) for m in markers],
                         forbidden_literals=UI_DEMO_FAILURE_MARKERS, metrics=flag))
    for flag in MIGRATED_CALLERS:
        jobs.append(case('MigratedUI', flag, hello,
                         ['--headless', '--frames', '3', '--' + flag + '-demo'],
                         script_errors=True,
                         forbidden_literals=['update() failed', '[UI_OVERFLOW]', '[ERROR]']))
    return jobs
