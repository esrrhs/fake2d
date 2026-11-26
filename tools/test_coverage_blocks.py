#!/usr/bin/env python3
"""Launch native Google Test reporter counterexamples; no assertions here."""
import argparse
from pathlib import Path
from integration_bridge import case, run_cases
from ci_common import ROOT

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--compiler',default='g++');p.add_argument('--gcov',default='gcov')
    p.add_argument('--evidence-dir');p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/fake2d_integration_tests')
    a=p.parse_args()
    return run_cases(a.test_binary,[case('CoverageReporter','ConservativeBlocks',a.test_binary,[],
                     transport_mode='reporter',kind='reporter',compiler=a.compiler,gcov=a.gcov,
                     evidence_dir=a.evidence_dir)])
if __name__=='__main__':raise SystemExit(main())
