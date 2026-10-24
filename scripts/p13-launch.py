"""One project-local logged job, detached so chat interruptions do not kill it."""
from pathlib import Path
import json,os,subprocess,sys,time
r=Path(__file__).resolve().parents[1];label=sys.argv[1];command=sys.argv[2:]
if command and command[0]=='--':command=command[1:]
o=r/'out/p13/logs';o.mkdir(parents=True,exist_ok=True)
for suffix in ['.log','.json','.process.json']:
 if (o/(label+suffix)).exists():raise SystemExit('Existing job/evidence label; inspect it instead of launching again')
f=(o/(label+'-launch.log')).open('ab');p=subprocess.Popen(['/opt/homebrew/bin/python3.13',str(r/'scripts/p13run.py'),label,'--',*command],cwd=r,stdin=subprocess.DEVNULL,stdout=f,stderr=subprocess.STDOUT,start_new_session=True,env=dict(os.environ,TMPDIR=str(r/'.work/tmp'),PYTHONDONTWRITEBYTECODE='1',DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer'))
j=dict(pid=p.pid,label=label,command=command,started_epoch=time.time(),detached=True);(o/(label+'.process.json')).write_text(json.dumps(j,indent=2)+'\n');print(json.dumps(j))
