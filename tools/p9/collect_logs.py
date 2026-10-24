#!/usr/bin/env python3
"""Read existing Android/Recovery logs; never mount, reboot, write or use su.

This does not claim access to XNU RAM. Empty/denied pstore is recorded as such.
Raw logs and the serial-bearing command manifest remain under ignored out/.
"""
from pathlib import Path
import argparse,datetime,hashlib,json,os,re,subprocess,time,uuid
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--wait-seconds',type=int,default=0);a=p.parse_args()
 if not 0<=a.wait_seconds<=300:p.error('wait must be bounded to 0..300 seconds')
 stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+uuid.uuid4().hex[:8]
 out=ROOT/'out/p9/log-recovery'/stamp;out.mkdir(parents=True)
 serial=json.loads((ROOT/'out/p3/private/selected-device.json').read_text())
 if isinstance(serial,dict):serial=serial.get('serial') or serial.get('adb_serial')
 if not isinstance(serial,str) or not re.fullmatch(r'[A-Za-z0-9._:-]+',serial):raise SystemExit('No validated selected device serial')
 adb=ROOT/'.work/p3-tools-35/platform-tools/adb'
 env=dict(os.environ,ADB_SERVER_SOCKET='tcp:localhost:5038',ANDROID_USER_HOME=str(ROOT/'.work/android'),ADB_VENDOR_KEYS=str(ROOT/'.work/android/adbkey'),ADB_LIBUSB='1',TMPDIR=str(ROOT/'.work/tmp'))
 records=[]
 def run(label,remote,timeout=20):
  cmd=[str(adb),'-s',serial,*remote];started=time.time()
  try:
   v=subprocess.run(cmd,capture_output=True,env=env,timeout=timeout);rc=v.returncode;data=v.stdout;err=v.stderr
  except subprocess.TimeoutExpired as e:rc=124;data=e.stdout or b'';err=e.stderr or b''
  (out/(label+'.stderr')).write_bytes(err)
  # Failed/partial reads cannot masquerade as valid log files.
  dest=out/(label+('.log' if rc==0 else '.partial'));dest.write_bytes(data)
  row=dict(label=label,command=cmd,exit_code=rc,bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),output=dest.name,duration_seconds=time.time()-started);records.append(row)
  return rc,data
 deadline=time.monotonic()+a.wait_seconds
 while True:
  rc,state=run('connection-'+str(len(records)),['get-state'],5)
  if rc==0 and state.strip()==b'device':break
  if time.monotonic()>=deadline:
   result=dict(result='NO_ADB_TRANSPORT',xnu_log_downloaded=False,records=records);(out/'manifest.json').write_text(json.dumps(result,indent=2)+'\n');print('No selected ADB device; no log recovered. '+str(out));return 2
  time.sleep(min(5,max(0,deadline-time.monotonic())))
 rc,product=run('product',['shell','getprop','ro.product.device'])
 # P3 real Android reports OnePlus5; the frozen P5 loop accepts this alias.
 # Recovery may report cheeseburger. Serial selection remains mandatory.
 if rc or product.strip() not in (b'OnePlus5',b'cheeseburger'):
  (out/'manifest.json').write_text(json.dumps(dict(result='IDENTITY_REFUSED',xnu_log_downloaded=False,records=records),indent=2)+'\n')
  print('Device identity mismatch; no logs read. '+str(out));return 3
 # shell -T keeps bytes unmodified and propagates the remote shell exit status.
 # exec-out on this real adbd returned host exit 0 for a failed cat, with the
 # error message in stdout. Never count those bytes as a successful log read.
 run('kernel-version',['shell','-T','cat','/proc/version'])
 run('identity-permissions',['shell','id'])
 rc,listing=run('pstore-list',['shell','ls','-1','/sys/fs/pstore'])
 if rc==0:
  for name in listing.decode(errors='replace').splitlines():
   if re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,127}',name):
    run('pstore-'+name,['shell','-T','cat','/sys/fs/pstore/'+name],30)
 run('last-kmsg',['shell','-T','cat','/proc/last_kmsg'],30)
 run('boot-reason',['shell','getprop','ro.boot.bootreason'])
 run('pstore-sysfs',['shell','ls','-l','/sys/module/ramoops/parameters'])
 recovered=[x for x in records if (x['label'].startswith('pstore-') and x['label'] not in ['pstore-list','pstore-sysfs'] or x['label']=='last-kmsg') and x['exit_code']==0 and x['bytes']]
 result=dict(result='READ_ONLY_COLLECTION_COMPLETE',nonempty_logs=len(recovered),xnu_log_downloaded=False,xnu_attribution='Requires matching P9 marker/build identity in recovered text; not inferred from Android logs',records=records)
 (out/'manifest.json').write_text(json.dumps(result,indent=2)+'\n');print('Read-only collection complete; nonempty existing logs='+str(len(recovered))+'. '+str(out));return 0
if __name__=='__main__':raise SystemExit(main())
