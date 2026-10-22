#!/usr/bin/env python3
"""Record actual Xcode availability and qualified-board readiness; never fake a kernel build."""
from pathlib import Path
import json,subprocess,sys,tomllib
root=Path(__file__).resolve().parents[1];out=root/'out/p2/xnu';out.mkdir(parents=True,exist_ok=True)
checks=[]
for cmd in [['xcodebuild','-version'],['xcrun','--sdk','iphoneos','--show-sdk-path']]:
 try:
  r=subprocess.run(cmd,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
  checks.append({'command':cmd,'exit_code':r.returncode,'output':r.stdout})
 except OSError as e:checks.append({'command':cmd,'exit_code':127,'output':str(e)})
board=tomllib.loads((root/'board.toml').read_text());missing=[]
if any(c['exit_code'] for c in checks):missing.append('Full Xcode/iphoneos SDK unavailable')
if board['board'].get('example'):missing.append('Board intentionally remains incomplete (example=true)')
for key in ['prelink','archives']:
 if not board.get('kernel',{}).get(key):missing.append('Qualified kernel.'+key+' missing')
if not board.get('ipsw'):missing.append('Independent IPSW identity/hashes missing')
if not board.get('payload',{}).get('adt'):missing.append('MSM8998 ADT generator and verified platform parameters missing')
report={'result':'BLOCKED' if missing else 'READY_FOR_REVIEW','exit_code':78 if missing else 0,'checks':checks,'missing':missing,'kernel_compiled':False,'hardware_validated':False}
(out/'readiness.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));sys.exit(report['exit_code'])
