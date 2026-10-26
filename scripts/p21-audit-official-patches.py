#!/usr/bin/env python3
"""Classify every resolved common/OS patch against the preserved build source."""
from pathlib import Path
import json,subprocess,hashlib
R=Path(__file__).resolve().parents[1];U=R/'.work/p21-official-framework';S=R/'.work/p4-unixnu/.cache/xnu-work';O=R/'out/p21/official-adaptation/patch-audit';O.mkdir(parents=True,exist_ok=True)
roots={'arch':U/'profiles/arch/arm64','os':U/'profiles/os/ipados-17.7.11-21H461','component:XnuPrelink':U/'kits/XnuPrelink'}
rows=[]
for owner in ['arch','os']:
 series=roots[owner]/'patches/xnu/series'
 for number,line in enumerate(series.read_text().splitlines(),1):
  name=line.split('#',1)[0].strip()
  if not name:continue
  if name.startswith('component:'):
   kind,rel=name.split('/',1);p=roots[kind]/rel
  elif name.startswith('arch:'):p=roots['arch']/name[5:]
  else:p=series.parent/name
  p=p.resolve();assert p.is_file(),p
  forward=subprocess.run(['git','-C',str(S),'apply','--check',str(p)],capture_output=True,text=True)
  reverse=subprocess.run(['git','-C',str(S),'apply','--reverse','--check',str(p)],capture_output=True,text=True)
  status='already_present' if reverse.returncode==0 else ('applicable_not_present' if forward.returncode==0 else 'overlap_or_context_review_required')
  idx=len(rows);(O/f'{idx:02d}-{p.stem}.log').write_text('FORWARD\n'+forward.stderr+'\nREVERSE\n'+reverse.stderr)
  rows.append({'order':idx,'owner':owner,'series_line':number,'patch':str(p.relative_to(U)),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'status':status})
result={'framework_revision':subprocess.check_output(['git','-C',str(U),'rev-parse','HEAD'],text=True).strip(),'source':str(S.relative_to(R)),'mutations':False,'note':'Context classification only. Not proof of compiled inclusion or runtime suitability. Overlaps require semantic review; never blindly apply.','patches':rows}
(O/'result.json').write_text(json.dumps(result,indent=2)+'\n');print({k:sum(x['status']==k for x in rows) for k in ['already_present','applicable_not_present','overlap_or_context_review_required']})
