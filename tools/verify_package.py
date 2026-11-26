#!/usr/bin/env python3
"""Build/run plumbing for native Google Test package verification."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from ci_common import ROOT
from integration_bridge import case, run_cases


def main():
    stage=Path(sys.argv[1]).resolve()
    bindir=Path(sys.argv[2]).resolve() if len(sys.argv)>2 else ROOT/'build/bin'
    binary=bindir/('fake2d_hello.exe' if os.name=='nt' else 'fake2d_hello')
    tracked=subprocess.check_output(['git','-C',str(ROOT),'ls-files','--','assets'],text=True).splitlines()
    generated='''assets/mario.json assets/platformer.json assets/slope.json assets/level.json assets/path.json assets/tiles.png assets/pf_tiles.png assets/_tile_anim.json assets/_tile_anim.png assets/ta_level.json assets/ta_sheet.png assets/white.png assets/ball.png assets/brick.png assets/paddle.png assets/coin.png assets/music.wav assets/lang/en.tsv assets/lang/zh.tsv'''.split()
    jobs=[case('StagedPackage','AssetTreeAndLaunchers',binary,[],kind='package_static',stage=str(stage),required_files=tracked+generated)]
    launcher=next((stage/n for n in ['run.sh','run.command'] if os.access(stage/n,os.X_OK)),None)
    with tempfile.TemporaryDirectory(prefix='fake2d-verify-') as tmp:
        before=sorted('./'+p.relative_to(stage/'assets').as_posix() for p in (stage/'assets').rglob('*') if p.is_file() and not p.is_symlink())
        def native(name,exe,args,contains=()):
            env={'FAKE2D_TEST_STRICT_ASSETS':'1','LIBGL_ALWAYS_SOFTWARE':'1',
                 'LD_LIBRARY_PATH':str(stage/'lib')+':'+os.environ.get('LD_LIBRARY_PATH',''),
                 'DYLD_LIBRARY_PATH':str(stage/'lib')+':'+os.environ.get('DYLD_LIBRARY_PATH','')}
            jobs.append(case('StagedPackage',name,bindir/exe,args,cwd=str(stage),env=env,
                             contains=list(contains),forbidden_literals=['texture load failed','strict assets:','update() failed']))
        def demo(name,args,**extra):
            jobs.append(case('StagedPackage',name,launcher,args,cwd=tmp,
                             forbidden_literals=['texture load failed','strict assets:','update() failed'],**extra))
        if launcher:
            native('ui','fake2d_engine_tests',['--gtest_filter=EngineContracts.ui'],['[ui] PASS'])
            image=str(Path(tmp)/'ui.png')
            demo('ui_box_demo',['--headless','--ui-demo','--frames','3','--screenshot',image,'--shot-frame','2','--shot-min-colors','12'],screenshot=image,forbids=[r'\[UI_OVERFLOW\]'])
            for name in ['ui-layout','ui-features','ui-variable','ui-stress']:
                image=str(Path(tmp)/(name+'.png'))
                demo(name,['--headless','--'+name+'-demo','--frames','240','--screenshot',image,'--shot-frame','60','--shot-min-colors','12'],screenshot=image,forbids=[r'\[UI_OVERFLOW\]'])
            native('stress_seed42','fake2d_engine_tests',['--gtest_filter=Seeds/UIStress.states/Seed42'])
            native('ui_benchmark','fake2d_ui_benchmark',['--benchmark_filter=UI/'],['version=1.55.0','rich_batches_max='])
            native('art','fake2d_engine_tests',['--gtest_filter=EngineContracts.art'],['[art] PASS'])
            for name,frames,shot in [('mario',180,None),('snooker',900,500),('contra',2500,1800)]:
                image=str(Path(tmp)/(name+'.png'));args=['--headless','--ai','--frames',str(frames),'--'+name+'-demo','--screenshot',image]
                if shot is not None:args+=['--shot-frame',str(shot)]
                args+=['--shot-min-colors','12'];demo(name,args,screenshot=image)
            jobs.append(case('StagedPackage','MissingTextureIsFatal',launcher,
                             ['--headless','--ai','--frames','120','--snooker-demo'],cwd=tmp,
                             hide_file=str(stage/'assets/white.png'),expected_exit='nonzero',
                             contains=['texture load failed: assets/white.png']))
            jobs.append(case('StagedPackage','NoGeneratedAssets',binary,[],kind='package_after',stage=str(stage),before=before))
        return run_cases(binary,jobs)

if __name__=='__main__':raise SystemExit(main())
