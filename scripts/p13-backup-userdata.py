#!/usr/bin/env python3
"""One immutable, unmounted userdata rollback copy. Never writes device blocks."""
from pathlib import Path
import gzip,hashlib,json,os,subprocess,sys,threading,time
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools/p13'));from device import Device
d=Device('userdata-backup');d.require_root_recovery();assert d.shell('getprop ro.product.device')=='cheeseburger';assert d.shell('readlink -f /dev/block/bootdevice/by-name/userdata')=='/dev/block/sda14'
expected=json.loads((r/'out/p13/device-geometry.json').read_text())['partition_bytes'];assert int(d.shell('blockdev --getsize64 /dev/block/sda14'))==expected
out=r/'out/p13/private/userdata-rollback';out.mkdir(mode=0o700)
result={'state':'PREPARING','device_path':'/dev/block/sda14','expected_bytes':expected,'run_dir':str(d.out.relative_to(r)),'device_written':False}
def save(): (out/'manifest.json').write_text(json.dumps(result,indent=2)+'\n')
def mounted():return [line.split()[1] for line in d.shell('cat /proc/mounts').splitlines() if line.split()[0]=='/dev/block/sda14']
# Ordinary unmount only: no force, no lazy unmount, no formatter or wipe.
for _ in range(8):
 mounts=mounted()
 if not mounts:break
 mounts.sort(key=lambda x:(x.count('/'),len(x)),reverse=True);target=mounts[0]
 assert target in ['/data','/sdcard','/data/media/0'];d.shell('umount '+target)
assert not mounted(),'userdata remains mounted; stop without backup or flash'
result['unmounted']=True;result['state']='DEVICE_HASH_BEFORE';save();before=d.run('adb',['shell','sha256sum /dev/block/sda14'],timeout=1800).split()[0];assert len(before)==64;result['device_sha256_before']=before;result['state']='STREAMING_COMPRESSED';save()
cmd=[str(r/'.work/p3-tools-35/platform-tools/adb'),'-s',d.serial,'exec-out','gzip -1c /dev/block/sda14'];result['read_command']=cmd
compressed=out/'userdata-before.img.gz.partial';h=hashlib.sha256();count=0;t=time.time()
with compressed.open('xb') as f,(out/'read.stderr.log').open('wb') as err:
 os.chmod(compressed,0o600);p=subprocess.Popen(cmd,env=d.env,stdout=subprocess.PIPE,stderr=err);timer=threading.Timer(5400,p.kill);timer.start()
 try:
  while b:=p.stdout.read(1<<20):
   f.write(b);h.update(b);count+=len(b)
   result.update(compressed_bytes=count,stream_elapsed_seconds=time.time()-t);save()
  code=p.wait(timeout=10);f.flush();os.fsync(f.fileno())
 finally:timer.cancel()
result['read_exit_code']=code;result['compressed_sha256']=h.hexdigest();assert code==0
assert not mounted();result['state']='VALIDATING_AND_MAKING_RAW_ROLLBACK';save();raw=out/'userdata-before.img.partial';h=hashlib.sha256();n=0
with gzip.open(compressed,'rb') as src,raw.open('xb') as dest:
 os.chmod(raw,0o600)
 while b:=src.read(1<<20):
  n+=len(b);assert n<=expected,'Decompressed stream exceeds verified partition';h.update(b)
  if b==bytes(len(b)):dest.seek(len(b),1)
  else:dest.write(b)
  result['validated_bytes']=n;save()
 dest.truncate(n);dest.flush();os.fsync(dest.fileno())
assert n==expected and h.hexdigest()==before
result['state']='DEVICE_HASH_AFTER';save();after=d.run('adb',['shell','sha256sum /dev/block/sda14'],timeout=1800).split()[0];assert after==before and not mounted()
compressed.rename(out/'userdata-before.img.gz');raw.rename(out/'userdata-before.img');result.update(state='VERIFIED',actual_bytes=n,sha256=h.hexdigest(),device_sha256_after=after,verified=True,raw_file=str((out/'userdata-before.img').relative_to(r)),gzip_file=str((out/'userdata-before.img.gz').relative_to(r)),restore_scope='userdata only; original raw bytes preserved; no other partition restore needed for this experiment',device_written=False);save();print('VERIFIED readonly userdata rollback copy; bytes='+str(n),flush=True)
