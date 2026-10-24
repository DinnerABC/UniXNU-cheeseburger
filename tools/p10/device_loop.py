#!/usr/bin/env python3
"""Reuse the frozen P5 controller, with P10 payload gates and explicit recovered-test replay authorization."""
from pathlib import Path
import hashlib,json,sys
r=Path(__file__).resolve().parents[2];p=r/'tools/p5/loop.py';source=p.read_text();sha=hashlib.sha256(p.read_bytes()).hexdigest()
expected=json.loads((r/'out/p5/inputs.json').read_text())['files']['tools/p5/loop.py']
assert sha==expected,'P5 controller changed: inspect before reusing'
substitutions={'out/p5/':'out/p10/','config/p5/':'config/p10/'}
for old,new in substitutions.items():source=source.replace(old,new)
assert 'out/p5/' not in source and 'config/p5/' not in source
# P10 carries real prelinked components and the two unchanged donor trust
# modules. Expand only the bounded payload profile; retain the frozen device
# state machine, backup, identity and identical-image refusal gates.
source=source.replace('24<=rs<=29522547','24<=rs<=67108864')
source=source.replace('from bundle import verify','import bundle;bundle.MAX_BYTES=67108864\n  from bundle import verify')
source=source.replace("if set(files)!=required:raise LoopError('Unqualified RAM payload files')",'''required.add('\\\\kernel.macho.trustcache')
  if hashlib.sha256(files.get('\\\\kernel.macho.trustcache',b'')).hexdigest()!='7416ca4f8377146626bb284e2133c76bee6cb4e3ee0af5d648b186235a7323a7':raise LoopError('Original trust-cache pin mismatch')
  gate=json.loads((ROOT/'out/p10/qualification/result.json').read_text())
  if gate['result']!='STATIC_PRELINK_AND_SELECTED_ABI_GATE_PASS' or hashlib.sha256(files['\\\\kernel.macho']).hexdigest()!=gate['candidate_sha256']:raise LoopError('P10 security integration gate mismatch')
  if set(files)!=required:raise LoopError('Unqualified RAM payload files')''')
(r/'out/p10/controller-reuse.json').write_text(json.dumps(dict(source=str(p.relative_to(r)),source_sha256=sha,adaptations=substitutions,payload_profile={'maximum_bytes':67108864,'extra_file':'kernel.macho.trustcache','trust_cache_sha256':'7416ca4f8377146626bb284e2133c76bee6cb4e3ee0af5d648b186235a7323a7','security_gate':'out/p10/qualification/result.json'},scope='Namespaced artifacts plus qualified larger four-file security payload; frozen state machine, identity, backups and USB operations unchanged'),indent=2)+'\n')
# A human may request one observation replay after a completed return. Never
# allow this option for an unresolved hang or for any other previous attempt.
repeat_of=None
if '--repeat-observation-of' in sys.argv:
 i=sys.argv.index('--repeat-observation-of')
 if i+1>=len(sys.argv):raise SystemExit('Missing completed run ID')
 repeat_of=sys.argv[i+1];del sys.argv[i:i+2]
 records=[json.loads(line) for line in (r/'out/p10/device-loop/index.jsonl').read_text().splitlines()]
 matches=[rec for rec in records if rec.get('run_id')==repeat_of]
 if len(matches)!=1 or matches[0].get('result') not in ('RETURNED_ANDROID_ADB','RETURNED_RECOVERY_ADB') or matches[0].get('attempts')!=1:
  raise SystemExit('Observation replay requires one completed, recovered test')
 old="if previous.get('image_sha256')==expected and previous.get('attempts',0)>0:raise LoopError('This exact image was already tested; no automatic identical-image repeat')"
 new="""if previous.get('image_sha256')==expected and previous.get('attempts',0)>0:
     if previous.get('run_id')!=REPEAT_OF:raise LoopError('Another attempt exists; observation replay refused')
     self.event('EXPLICIT_OBSERVATION_REPLAY',previous_run_id=REPEAT_OF,reason='User missed screen and explicitly requested resend after recovery return')"""
 assert source.count(old)==1
 source=source.replace(old,new)
namespace={'__name__':'p10_reused_controller','__file__':str(p),'REPEAT_OF':repeat_of};exec(compile(source,str(p),'exec'),namespace)
if __name__=='__main__':namespace['main']()
