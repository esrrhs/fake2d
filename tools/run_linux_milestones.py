#!/usr/bin/env python3
"""Declarative driver for the unchanged Linux milestone workloads."""
import argparse,json
from pathlib import Path
from ci_common import ROOT,resolve_hello,add_hello_argument
from integration_bridge import case,run_cases
p=argparse.ArgumentParser();add_hello_argument(p);a=p.parse_args();hello=resolve_hello(a.hello)
jobs=[]
for item in json.loads((ROOT/'tools/linux_milestone_cases.json').read_text()):
    data=dict(item);name=data.pop('name');benchmark=data.pop('benchmark');args=data.pop('args')
    program=hello.with_name('fake2d_benchmarks'+hello.suffix) if benchmark else hello
    jobs.append(case('LinuxMilestone',name,program,args,**data))
raise SystemExit(run_cases(hello,jobs))
