#!/usr/bin/env python3
"""Documentation test data/run driver. Native Google Test owns assertions."""
import argparse,json,re
from pathlib import Path
from ci_common import ROOT,run_hello
from check_ui_demos import validate_ui_demos
from integration_bridge import case,run_cases

def main():
    p=argparse.ArgumentParser();p.add_argument('--hello',type=Path,default=ROOT/'build/bin/fake2d_hello')
    p.add_argument('--no-run',action='store_true');p.add_argument('--ctest-log',type=Path);a=p.parse_args()
    hello=a.hello.resolve();cmake=(ROOT/'examples/hello/CMakeLists.txt').read_text()
    block=re.search(r'set\(FAKE2D_TEST_SUITES\s+(.*?)\)',cmake,re.S)
    suites=block.group(1).split() if block else []
    demos=len(re.findall(r'\{"[a-z0-9-]+"',(ROOT/'include/fake2d/demos.h').read_text()))
    slots=int(re.search(r'kMaxEntities = (\d+)',(ROOT/'include/fake2d/entity_store.h').read_text()).group(1))
    logs=[]
    if not a.no_run:
        if a.ctest_log is not None:
            logs=[{'suite':name,'returncode':0,'text':a.ctest_log.read_text(errors='replace') if a.ctest_log.is_file() else ''} for name in suites]
        else:
            binary=hello.with_name('fake2d_engine_tests'+hello.suffix)
            for name in suites:
                rc,text=run_hello(binary,['--gtest_filter='+('Launcher.catalog' if name=='launcher' else 'EngineContracts.'+name.replace('-','_'))])
                logs.append({'suite':name,'returncode':rc,'text':text})
    data={'root':str(ROOT),'suite_block':bool(block),'suites':suites,'demos':demos,'slots':slots,
          'logs':logs,'run':not a.no_run,'patterns':json.loads((ROOT/'tools/documentation_cases.json').read_text())}
    rc=run_cases(hello,[case('Documentation','CountsAPIsAndRuntimeLedger',hello,[],kind='docs',transport_mode='data',data=data)])
    errors=[]
    if not a.no_run:validate_ui_demos(hello,errors)
    return rc or bool(errors)

if __name__=='__main__':raise SystemExit(main())
