#!/usr/bin/env python3
"""Exactly one explicitly authorized userdata flash, never auto-retry a partial write."""
from pathlib import Path
import fcntl,hashlib,json,os,re,subprocess,sys,time,uuid
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools/p13'));from device import Device
out=r/'out/p13';lock=(out/'flash.lock').open('a');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
state=out/'flash-result.json';assert not state.exists(),'An attempt already exists; no repeat writes'
g=json.loads((out/'preflight.json').read_text());inputs=json.loads((out/'inputs.json').read_text());m=json.loads((r/'out/p12/manifest.json').read_text());assert g['result']=='P13_WRITE_PREFLIGHT_PASS' and time.time()-g['created_epoch']<1800
image=(r/g['image']).resolve();expected=m['files'][str(image.relative_to(r/'out/p12'))];assert image.is_file() and image.stat().st_size==inputs['image_bytes']==expected['bytes'];assert expected['sha256']==g['image_sha256']==inputs['image_sha256']=='fd43ee7035ce185ce4215ac93fdc006fd784a54b3b7442e21457c226cbe03093'
backup=json.loads((out/'private/userdata-rollback/manifest.json').read_text());assert backup['state']=='USER_CANCELLED_NO_USERDATA_BACKUP' and not backup['verified'];assert json.loads((out/'recovery-check.json').read_text())['result']=='ANDROID_REINITIALIZATION_CAPABILITY_PASS';assert json.loads((out/'logs/check-inputs-01.json').read_text())['exit_code']==0
assert json.loads((out/'logs/check-backups-01.json').read_text())['exit_code']==0
print('Rechecking the exact accepted P12 file before the one authorized userdata write',flush=True)
before=image.stat();h=hashlib.sha256()
with image.open('rb') as src:
 while block:=src.read(8<<20):h.update(block)
after=image.stat();assert (before.st_ino,before.st_mtime_ns,before.st_size)==(after.st_ino,after.st_mtime_ns,after.st_size) and h.hexdigest()==g['image_sha256']
(out/'immediate-image-check.json').write_text(json.dumps({'sha256':h.hexdigest(),'bytes':after.st_size,'verified_epoch':time.time(),'unchanged_during_hash':True},indent=2)+'\n')
d=Device('flash');now=d.run('fastboot',['getvar','partition-size:userdata']);match=re.search(r'partition-size:userdata:\s*(0x[0-9A-Fa-f]+|[0-9]+)',now);assert match and int(match[1],0)==g['userdata_capacity'];assert re.search(r'unlocked:\s*yes',d.run('fastboot',['getvar','unlocked']))
command=[str(r/'.work/p3-tools-35/platform-tools/fastboot'),'-s',d.serial,'-S','128M','flash','userdata',str(image)];assert command[5:7]==['flash','userdata']
result={'run_id':time.strftime('%Y%m%dT%H%M%SZ',time.gmtime())+'-'+uuid.uuid4().hex[:8],'state':'ATTEMPT_STARTED','attempts':1,'partition':'userdata','command':command,'image':g['image'],'image_sha256':g['image_sha256'],'image_bytes':inputs['image_bytes'],'started_epoch':time.time(),'full_device_readback':False,'other_partition_writes':False}
def save():state.write_text(json.dumps(result,indent=2)+'\n')
save();log=out/'flash-stdout-stderr.log'
with log.open('xb') as f:
 p=subprocess.Popen(command,env=d.env,stdout=f,stderr=subprocess.STDOUT)
 try:code=p.wait(timeout=5400)
 except subprocess.TimeoutExpired:p.kill();p.wait();code=124
result.update(exit_code=code,duration_seconds=time.time()-result['started_epoch']);text=log.read_text(errors='replace');sends=re.findall(r"Sending sparse 'userdata'\s+(\d+)/(\d+)",text);writes=len(re.findall(r"Writing 'userdata'.*?OKAY",text,flags=re.S));failed=bool(re.search(r'\bFAILED\b|error:',text,re.I));result.update(sparse_sends=len(sends),write_success_markers=writes)
# Exact chunk totals and an OKAY per writing step, in addition to the true exit code.
if code==0 and not failed and sends and int(sends[-1][0])==int(sends[-1][1])==len(sends) and writes==len(sends):result['state']='FASTBOOT_FLASH_SUCCEEDED'
else:result['state']='FAILED_OR_PARTIAL_STOP_NO_RETRY'
save();print(json.dumps({k:v for k,v in result.items() if k!='command'},indent=2));raise SystemExit(0 if result['state']=='FASTBOOT_FLASH_SUCCEEDED' else 1)
