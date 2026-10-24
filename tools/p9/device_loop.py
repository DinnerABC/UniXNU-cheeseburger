#!/usr/bin/env python3
"""Reuse the frozen P5 controller, adapting artifact namespaces only in memory."""
from pathlib import Path
import hashlib,json
r=Path(__file__).resolve().parents[2];p=r/'tools/p5/loop.py';source=p.read_text();sha=hashlib.sha256(p.read_bytes()).hexdigest()
expected=json.loads((r/'out/p5/inputs.json').read_text())['files']['tools/p5/loop.py']
assert sha==expected,'P5 controller changed: inspect before reusing'
substitutions={'out/p5/':'out/p9/','config/p5/':'config/p9/'}
for old,new in substitutions.items():source=source.replace(old,new)
assert 'out/p5/' not in source and 'config/p5/' not in source
(r/'out/p9/controller-reuse.json').write_text(json.dumps(dict(source=str(p.relative_to(r)),source_sha256=sha,adaptations=substitutions,scope='Only P9 artifact/output paths; state machine and USB/device operations reused unchanged'),indent=2)+'\n')
namespace={'__name__':'p9_reused_controller','__file__':str(p)};exec(compile(source,str(p),'exec'),namespace)
if __name__=='__main__':namespace['main']()
