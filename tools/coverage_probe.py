#!/usr/bin/env python3
"""Real GCC regression: executed, uncalled, and never-constructed lambdas."""
import argparse
import copy
import gzip
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from coverage_blocks import block_hits, merge_observed, gcov_dump_executable


def collect(compiler, gcov, evidence_dir=None):
    scratch=Path(__file__).resolve().parents[1]/'.deps'
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='coverage-blocks-',dir=scratch) as tmp:
        root = Path(tmp); (root/'src').mkdir()
        source = root/'src/probe.cpp'
        source.write_text("""#include <functional>
#include <vector>
std::vector<std::function<int()>> callbacks;
void reg(std::function<int()> f) { callbacks.push_back(f); }
int choose(int n) { return n; }
int never_choose() {
 return 42; // FALSE_TERNARY_BODY
}
int main(int argc, char**) {
 int calls=0;
 reg(std::function<int()>(
  [&]() { // USED_OPEN
   if (argc>10) {
    calls+=1000; // FALSE_BRANCH_BODY
   }
   int chosen = argc>10
       ? never_choose() // FALSE_TERNARY_ARM
       : choose(1); // TRUE_TERNARY_ARM
   return calls+=chosen; // USED_BODY
  }));
 reg(std::function<int()>(
  [&]() { // UNCALLED_OPEN
   return calls+99; // UNCALLED_BODY
  }));
 if (argc>10) {
  reg(std::function<int()>(
   [&]() { // ABSENT_OPEN
    return calls+200; // ABSENT_BODY
   }));
  return callbacks.back()();
 }
 return callbacks.front()()!=1;
}
""")
        subprocess.run([compiler,'--coverage','-O0','-g','-std=c++17',str(source),'-o',str(root/'probe')],check=True)
        subprocess.run([str(root/'probe')],check=True)
        notes=next(root.glob('*.gcno'))
        subprocess.run([gcov,'--json-format',str(notes)],cwd=root,check=True,stdout=subprocess.DEVNULL)
        data=json.loads(gzip.decompress(next(root.glob('*.gcov.json.gz')).read_bytes()))
        file=next(f for f in data['files'] if f['file']==str(source))
        report={'files':[{'file':'src/probe.cpp','lines':copy.deepcopy(file['lines'])}]}
        before=copy.deepcopy(report)
        hits,proof,ambiguous=block_hits(notes,gcov,root)
        aliases=root/'tool-aliases';aliases.mkdir()
        aliased=aliases/'gcov-14'
        aliased.symlink_to(Path(shutil.which(gcov) or gcov).resolve())
        (aliases/'gcov-dump-14').symlink_to(Path(shutil.which(gcov_dump_executable(gcov)) or gcov_dump_executable(gcov)).resolve())
        alias_hits,_,_=block_hits(notes,str(aliased),root)
        changes=merge_observed(report,hits)
        def count(doc, marker):
            number=next(i for i,l in enumerate(source.read_text().splitlines(),1) if marker in l)
            return sum(l['count'] for l in doc['files'][0]['lines'] if l['line_number']==number)
        if evidence_dir:
            dest=Path(evidence_dir).resolve()
            shutil.copytree(root,dest,symlinks=True)
            (dest/'before.json').write_text(json.dumps(before,indent=2))
            (dest/'after.json').write_text(json.dumps(report,indent=2))
            (dest/'proof.json').write_text(json.dumps({'observed':proof,'changes':changes,'ambiguous_skipped':ambiguous},indent=2))
        markers=('USED_OPEN','USED_BODY','UNCALLED_OPEN','UNCALLED_BODY','ABSENT_OPEN','ABSENT_BODY','FALSE_BRANCH_BODY','FALSE_TERNARY_BODY','FALSE_TERNARY_ARM','TRUE_TERNARY_ARM')
        return {'counts': {m: {'before': count(before,m), 'after': count(report,m)} for m in markers},
                'hits': sorted([list(k)+[v] for k,v in hits.items()]), 'alias_hits': sorted([list(k)+[v] for k,v in alias_hits.items()]),
                'companion': gcov_dump_executable('/usr/bin/x86_64-linux-gnu-gcov-14'),
                'denominator_before': [l['line_number'] for l in before['files'][0]['lines']],
                'denominator_after': [l['line_number'] for l in report['files'][0]['lines']],
                'idempotent_changes': merge_observed(report,hits),
                'no_proof_changes': merge_observed(copy.deepcopy(before),{})}
