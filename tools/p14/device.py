"""Identity-pinned P14 device client. Read-only commands and explicit transitions only."""
from pathlib import Path
import datetime,json,os,re,subprocess,time,uuid
ROOT=Path(__file__).resolve().parents[2]
class Device:
 def __init__(self,label):
  self.serial=json.loads((ROOT/'out/p3/private/selected-device.json').read_text())['serial']
  self.env=dict(os.environ,ADB_LIBUSB='1',ADB_SERVER_SOCKET='tcp:localhost:5038',ADB_VENDOR_KEYS=str(ROOT/'.work/android/adbkey'),ANDROID_USER_HOME=str(ROOT/'.work/android'),TMPDIR=str(ROOT/'.work/tmp'))
  self.out=ROOT/'out/p14/private'/(datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+label+'-'+uuid.uuid4().hex[:6]);self.out.mkdir(parents=True,mode=0o700)
  self.n=0
 def run(self,tool,args,timeout=30):
  self.n+=1;cmd=[str(ROOT/'.work/p3-tools-35/platform-tools'/tool),'-s',self.serial]+args;t=time.time()
  try:v=subprocess.run(cmd,env=self.env,capture_output=True,timeout=timeout);code=v.returncode;out=v.stdout+v.stderr
  except subprocess.TimeoutExpired as e:code=124;out=(e.stdout or b'')+(e.stderr or b'')
  dest=self.out/str(self.n).zfill(3);dest.with_suffix('.log').write_bytes(out);dest.with_suffix('.json').write_text(json.dumps({'command':cmd,'exit_code':code,'started':t,'duration':time.time()-t},indent=2)+'\n')
  text=(v.stdout if tool=='adb' and code!=124 else out).decode(errors='replace')
  if code or re.search(r'\bFAILED\b',out.decode(errors='replace')):raise RuntimeError('Device command failed; see '+str(dest.relative_to(ROOT)))
  return text
 def shell(self,command):return self.run('adb',['shell',command]).strip()
 def require_android(self):
  state=self.run('adb',['get-state']).strip();model=self.shell('getprop ro.product.device')
  if state!='device' or model not in ('OnePlus5','cheeseburger'):raise RuntimeError('Identity mismatch')
 def require_root_recovery(self):
  if self.shell('id -u')!='0' or self.shell('getprop ro.bootmode')!='recovery':raise RuntimeError('Root Recovery required')
