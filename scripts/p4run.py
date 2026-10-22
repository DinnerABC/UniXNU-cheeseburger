#!/usr/bin/env python3
"""Record every P4 command and its actual exit status in the project."""
import json,os,subprocess,sys,time
from pathlib import Path
root=Path(__file__).resolve().parents[1]
label=sys.argv[1]; cmd=sys.argv[2:]
if cmd and cmd[0]=='--':cmd=cmd[1:]
out=root/'out/p4/logs';out.mkdir(parents=True,exist_ok=True)
tmp=root/'.work/tmp';tmp.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,TMPDIR=str(tmp),PYTHONDONTWRITEBYTECODE='1')
started=time.time();code=127
with (out/(label+'.log')).open('w') as log:
 try:code=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,env=env).returncode
 except OSError as e:log.write(str(e)+'\n')
(out/(label+'.json')).write_text(json.dumps({'command':cmd,'cwd':os.getcwd(),'exit_code':code,'started_epoch':started,'duration_seconds':time.time()-started},indent=2)+'\n')
summary=os.environ.get('GITHUB_STEP_SUMMARY')
if summary:
 with open(summary,'a') as f:f.write('**'+label+'**: exit '+str(code)+'. Compilation/image checks only; hardware boot is unverified.\n\n')
print(label+': exit '+str(code),flush=True)
sys.exit(code)
