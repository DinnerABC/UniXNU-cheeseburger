#!/usr/bin/env python3
"""Run the fixed official ordered patch preparer in the new target tree only."""
from pathlib import Path
import json,sys,subprocess
R=Path(__file__).resolve().parents[1];U=R/'.work/p21-official-framework';O=R/'out/p21/official-adaptation'
sys.path.insert(0,str(U/'scripts'))
from unixnu.cli import parser
from unixnu.config import Context
from unixnu import feeds,patches
assert subprocess.check_output(['git','-C',str(U),'rev-parse','HEAD'],text=True).strip()=='16e592abee8a550dec8154ed1f38b5ebc4f16213'
a=parser().parse_args(['tree','-D',str(R),'--out',str(O/'resolved')]);c=Context(a);modules=feeds.resolve(c)
assert str(c.tree).startswith(str(O/'resolved'))
result=patches.prepare(c,modules)
report={'result':'OFFICIAL_ORDERED_SOURCE_PREPARATION_PASS','framework_revision':'16e592abee8a550dec8154ed1f38b5ebc4f16213','source_stamp':str((c.work/'xnu-inputs.json').relative_to(R)),'patch_list':str(result.relative_to(R)),'patch_count':len(result.read_text().splitlines()),'framework_local_adaptation':json.loads((O/'framework-context-fix.json').read_text()),'compiled':False,'hardware_executed':False,'board_guard':c.example,'note':'Official arch/OS patch stack only. Board changes and provider choices must be integrated before a kernel build or boot.'}
(O/'official-source-preparation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
