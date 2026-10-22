#!/usr/bin/env python3
"""Bounded, identity-checked temporary-boot loop. No partition-write operations."""
import argparse,csv,datetime,hashlib,fcntl,json,os,plistlib,re,runpy,struct,subprocess,time,uuid,zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
TOOLS=ROOT/'.work/p3-tools-35/platform-tools'
class LoopError(RuntimeError):pass
def command_failed(code,output):return code!=0 or bool(re.search(r'\bFAILED\b',output))
def digest(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()
def verify_image(image,expected):
 image=Path(image).resolve()
 if ROOT not in image.parents:raise LoopError('Image must be inside project')
 if digest(image)!=expected:raise LoopError('Image hash mismatch')
 b=image.read_bytes()
 if b[:8]!=b'ANDROID!':raise LoopError('Unsupported Android image')
 ks,ka,rs,ra,ss,sa,ta,page,version,_=struct.unpack_from('<10I',b,8)
 native=version in [0,1] and page==2048 and not ss and rs==1
 ram=version==0 and page==4096 and not ss and (ka,ra,ta)==(0x8000,0x1000000,0x100) and 24<=rs<=29522547
 if not (native or ram):raise LoopError('Unqualified boot layout')
 if ks<=0 or page+ks>len(b):raise LoopError('Truncated kernel')
 d=zlib.decompressobj(31);fd=d.decompress(b[page:page+ks])+d.flush()
 if len(fd)!=6291456 or fd[56:60]!=b'ARMd':raise LoopError('Not qualified MSM8998 UEFI FD')
 parse=runpy.run_path(str(ROOT/'scripts/p3-dtb-inspect.py'))['parse']
 actual=parse((ROOT/'out/p3/private/recovery-device.dtb').read_bytes())['/'];candidate=parse(d.unused_data)['/']
 for key in ['model','compatible','qcom,msm-id','qcom,board-id']:
  if candidate.get(key)!=actual.get(key):raise LoopError('Device FDT identity mismatch: '+key)
 off=page+((ks+page-1)//page)*page
 if off+rs>len(b):raise LoopError('Truncated ramdisk')
 if native:
  if b[off:off+rs]!=b'\n':raise LoopError('Unqualified ramdisk')
 else:
  import sys
  sys.path.insert(0,str(ROOT/'tools/ram_payload'))
  from bundle import verify
  files=verify(b[off:off+rs]);required={'\\EFI\\UniXNU\\unixnu.toml','\\kernel.macho','\\kernel.macho.adt'}
  if set(files)!=required:raise LoopError('Unqualified RAM payload files')
  validation=json.loads((ROOT/'out/p5/kernel/validation.json').read_text())
  if validation.get('result')!='PASS' or hashlib.sha256(files['\\kernel.macho']).hexdigest()!=validation['sha256']:raise LoopError('Kernel qualification mismatch')
  adt=json.loads((ROOT/'out/p5/adt-parser/validation.json').read_text())
  if adt['exit_code']!=0 or hashlib.sha256(files['\\kernel.macho.adt']).hexdigest()!=adt['adt_sha256']:raise LoopError('ADT qualification mismatch')
  if files['\\EFI\\UniXNU\\unixnu.toml']!=(ROOT/'config/p5/unixnu.toml').read_bytes():raise LoopError('Unqualified RAM boot policy')
 return dict(image=str(image.relative_to(ROOT)),sha256=expected,bytes=len(b),fd_sha256=hashlib.sha256(fd).hexdigest(),dtb_identity_match=True,qualification='UEFI temporary boot only; not an XNU boot proof')
class DeviceLoop:
 def __init__(self):
  self.run_id=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+uuid.uuid4().hex[:8]
  self.out=ROOT/'out/p5/device-loop'/self.run_id;self.private=self.out/'private';self.private.mkdir(parents=True,mode=0o700)
  self.serial=json.loads((ROOT/'out/p3/private/selected-device.json').read_text())['serial']
  self.env=dict(os.environ,ADB_LIBUSB='1',ADB_SERVER_SOCKET='tcp:localhost:5038',ADB_VENDOR_KEYS=str(ROOT/'.work/android/adbkey'),ANDROID_USER_HOME=str(ROOT/'.work/android'),TMPDIR=str(ROOT/'.work/tmp'))
  self.count=0;self.last_state=None;self.boot_sent=False;self.boot_attempted=False
 def event(self,stage,**data):
  rec=dict(run_id=self.run_id,time_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),stage=stage,**data)
  with (self.out/'events.jsonl').open('a') as f:f.write(json.dumps(rec)+'\n')
 def execute(self,label,cmd,timeout=12):
  self.count+=1;t=time.monotonic()
  try:
   v=subprocess.run([str(x) for x in cmd],env=self.env,capture_output=True,timeout=timeout);code=v.returncode;out=v.stdout+v.stderr
  except subprocess.TimeoutExpired as e:code=124;out=(e.stdout or b'')+(e.stderr or b'')
  meta=dict(command=[str(x) for x in cmd],exit_code=code,duration_seconds=time.monotonic()-t)
  p=self.private/(str(self.count).zfill(4)+'-'+label);p.with_suffix('.log').write_bytes(out);p.with_suffix('.json').write_text(json.dumps(meta,indent=2)+'\n')
  self.event('COMMAND',label=label,exit_code=code,duration_seconds=meta['duration_seconds'])
  return code,out.decode(errors='replace')
 def adb(self,label,args,timeout=12):return self.execute(label,[TOOLS/'adb','-s',self.serial]+args,timeout)
 def fastboot(self,label,args,timeout=12):return self.execute(label,[TOOLS/'fastboot','-s',self.serial]+args,timeout)
 def usb(self):
  code,text=self.execute('usb',['ioreg','-a','-p','IOUSB','-l'])
  if code:return {'available':False,'matched':False}
  try:tree=plistlib.loads(text.encode())
  except Exception:return {'available':False,'matched':False}
  matches=[]
  def walk(node):
   if isinstance(node,dict):
    if str(node.get('USB Serial Number',''))==self.serial:matches.append({k:node.get(k) for k in ['idVendor','idProduct','locationID','USB Product Name']})
    for v in node.values():walk(v)
   elif isinstance(node,list):
    for v in node:walk(v)
  walk(tree);return {'available':True,'matched':bool(matches),'devices':matches}
 def state(self):
  ac,ao=self.execute('adb-enumeration',[TOOLS/'adb','devices','-l'])
  fc,fo=self.execute('fastboot-enumeration',[TOOLS/'fastboot','devices','-l'])
  adb_rows=[x.split() for x in ao.splitlines() if x.startswith(self.serial+'\t') or x.startswith(self.serial+' ')]
  fb=any(x.split() and x.split()[0]==self.serial for x in fo.splitlines()) if fc==0 else False
  usb=self.usb();state='UNKNOWN'
  if adb_rows:
   mode=adb_rows[0][1]
   if mode in ['device','recovery']:
    code,out=self.adb('identity',['shell','getprop ro.product.device; getprop ro.bootmode'])
    lines=out.strip().splitlines()
    if code or not lines or lines[0] not in ['OnePlus5','cheeseburger']:raise LoopError('ADB product mismatch')
    state='RECOVERY_ADB' if mode=='recovery' or 'recovery' in lines[1:] else 'ANDROID_ADB'
   elif mode=='unauthorized':state='ADB_UNAUTHORIZED'
   else:state='ADB_'+mode.upper()
  elif fb:state='FASTBOOT'
  elif usb['matched']:state='UEFI_POSSIBLE' if self.boot_sent else 'USB_PRESENT_UNKNOWN'
  elif usb['available']:state='USB_DISCONNECTED_AFTER_BOOT' if self.boot_sent else 'USB_DISCONNECTED'
  result=dict(state=state,usb=usb,adb_enumeration_exit=ac,fastboot_enumeration_exit=fc,uefi_execution_proven=False)
  if result!=self.last_state:self.event('STATE',**result);self.last_state=result
  return state
 def ensure_fastboot(self):
  state=self.state()
  if state in ['ANDROID_ADB','RECOVERY_ADB']:
   code,out=self.adb('reboot-bootloader',['reboot','bootloader'])
   if command_failed(code,out):raise LoopError('ADB reboot failed')
  elif state!='FASTBOOT':raise LoopError('Cannot software recover state '+state)
  deadline=time.monotonic()+60
  while time.monotonic()<deadline:
   if self.state()=='FASTBOOT':break
   time.sleep(2)
  else:raise LoopError('Fastboot did not appear; manual recovery required')
  for label,args,pattern in [('product',['getvar','product'],r'product:\s*msm8998\b'),('unlocked',['getvar','unlocked'],r'unlocked:\s*yes\b')]:
   code,out=self.fastboot(label,args)
   if command_failed(code,out) or not re.search(pattern,out):raise LoopError('Fastboot '+label+' qualification failed')
 def boot(self,image,expected,observe):
  self.lock=(ROOT/'out/p5/device-loop/device.lock').open('a')
  try:fcntl.flock(self.lock.fileno(),fcntl.LOCK_EX|fcntl.LOCK_NB)
  except BlockingIOError:raise LoopError('Another P5 boot is active; no concurrent device operation')
  self.image_sha256=expected
  index=ROOT/'out/p5/device-loop/index.jsonl'
  if index.exists():
   for line in index.read_text().splitlines():
    previous=json.loads(line)
    if previous.get('image_sha256')==expected and previous.get('attempts',0)>0:raise LoopError('This exact image was already tested; no automatic identical-image repeat')
  self.event('BUILD',status='REUSE_EXISTING_ARTIFACT',note='No compilation claimed by this command; use pipeline.py for source builds')
  info=verify_image(image,expected);self.event('VERIFY',**info)
  # Re-hash minimum critical recovery set. Existence alone is insufficient.
  required={'persist','modemst1','modemst2','fsg','boot','recovery','abl','xbl'};found=set()
  for manifest in (ROOT/'backups/p3').glob('*/manifest.json'):
   for rec in json.loads(manifest.read_text()):
    if rec.get('partition') in required and rec.get('verified'):
     f=ROOT/rec['file']
     if f.stat().st_size!=rec['expected_bytes'] or digest(f)!=rec['sha256']:raise LoopError('Backup integrity failure')
     found.add(rec['partition'])
  if found!=required:raise LoopError('Required backup evidence missing')
  self.event('BACKUP_GATE',status='HASH_VERIFIED',restoration_write_tested=False)
  self.ensure_fastboot();self.event('BOOT',image_sha256=expected)
  self.boot_attempted=True
  code,out=self.fastboot('temporary-boot',['boot',str(Path(image).resolve())],45)
  if command_failed(code,out):raise LoopError('Fastboot temporary boot failed; no automatic repeat')
  self.boot_sent=True;self.event('OBSERVE',transport_accepted=True,execution_proven=False)
  boot_observation_start=time.monotonic();deadline=boot_observation_start+observe
  while time.monotonic()<deadline:
   state=self.state()
   if state in ['ANDROID_ADB','RECOVERY_ADB']:
    if state=='ANDROID_ADB':
     c,text=self.adb('boot-completed',['shell','getprop sys.boot_completed'])
     if c or text.strip()!='1':time.sleep(3);continue
    self.event('COLLECT',status='HOST_TRANSPORT_LOGS_SAVED',uefi_log='SCREEN_OR_OTHER_CHANNEL_REQUIRED')
    self.finish('RETURNED_'+state,'Software roundtrip observed; no kernel execution inferred');return
   if state=='FASTBOOT' and time.monotonic()-boot_observation_start>=20:
    self.finish('RETURNED_FASTBOOT','No blind boot retry; image unchanged');return
   time.sleep(3)
  self.finish('NO_TRANSPORT_MANUAL_RECOVERY','Observation timed out; could be UEFI, hang or USB loss; no automatic reset route proven')
 def finish(self,result,note):
  rec=dict(run_id=self.run_id,image_sha256=getattr(self,'image_sha256',None),result=result,note=note,attempts=1 if self.boot_attempted else 0,utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
  self.event('CLASSIFY',**{k:v for k,v in rec.items() if k!='run_id'});self.event('STOP',reason=result)
  self.result=result
  (self.out/'summary.json').write_text(json.dumps(rec,indent=2)+'\n');(self.out/'summary.md').write_text('# '+self.run_id+'\n\n'+result+'\n\n'+note+'\n')
  index=self.out.parent/'index.jsonl'
  with index.open('a') as f:f.write(json.dumps(rec)+'\n')
  table=self.out.parent/'index.csv';new=not table.exists()
  with table.open('a') as f:
   w=csv.DictWriter(f,fieldnames=list(rec));
   if new:w.writeheader()
   w.writerow(rec)
  print(json.dumps(rec))
def main():
 p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
 sub.add_parser('detect');v=sub.add_parser('verify');v.add_argument('image');v.add_argument('--sha256',required=True)
 b=sub.add_parser('boot');b.add_argument('image');b.add_argument('--sha256',required=True);b.add_argument('--observe-seconds',type=int,default=180)
 a=p.parse_args()
 if a.command=='verify':print(json.dumps(verify_image(a.image,a.sha256),indent=2));return
 loop=DeviceLoop()
 try:
  if a.command=='detect':loop.finish(loop.state(),'Read-only device inventory')
  else:
   if not 30<=a.observe_seconds<=600:raise LoopError('Observation must be 30..600 seconds')
   loop.boot(a.image,a.sha256,a.observe_seconds)
   if loop.result not in ['RETURNED_ANDROID_ADB','RETURNED_RECOVERY_ADB']:raise SystemExit(2)
 except (LoopError,OSError,ValueError,subprocess.SubprocessError) as e:
  loop.finish('BLOCKED',str(e));raise SystemExit(1)
if __name__=='__main__':main()
