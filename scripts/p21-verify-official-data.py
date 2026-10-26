#!/usr/bin/env python3
"""Verify official seed changes with guest ownership enabled; local image only."""
from pathlib import Path
import hashlib,json,os,plistlib,subprocess,sys,tempfile
R=Path(__file__).resolve().parents[1];O=R/'out/p21/official-adaptation/data-seed';U=R/'.work/p21-official-framework'
sys.path.insert(0,str(U/'drivers/DarwinBoot/Test'));import packed_ramdisk
SRC=O/'skip-setup-1g.dbr';DEST=O/'owner-data-1g.dbr'
assert os.geteuid()==0,'Run via the macOS administrator authorization dialog'
assert not DEST.exists(),'Do not overwrite a prepared seed'
assert hashlib.sha256((R/'out/p20/r8/data-seed/owner-data-1g.dbr').read_bytes()).hexdigest()=='0451a8ca7c3914112f3379549939512b9c735312202aa1ef59b82815554772e0'
report=json.loads(SRC.with_suffix('.json').read_text());assert report['no_auto_dim'] and report['build']=='21H461'
PREFS=report['preferences'];EXPECTED_UUID='D2420479-F58E-44C5-AB79-1815EC32C686'
def call(*a):
 try:return subprocess.check_output([str(x) for x in a],stderr=subprocess.PIPE)
 except subprocess.CalledProcessError as e:
  print(e.output.decode(errors='replace'),file=sys.stderr);print((e.stderr or b'').decode(errors='replace'),file=sys.stderr);raise
def info(p):return plistlib.loads(call('/usr/sbin/diskutil','info','-plist',p))
# Recover only a prior failed attachment created by this exact local helper.
for image in plistlib.loads(call('/usr/bin/hdiutil','info','-plist'))['images']:
 path=image.get('image-path','')
 if path.startswith(str(O/'p21-official-data-')) and path.endswith('/copy.raw'):
  dev=next(e['dev-entry'] for e in image['system-entities'] if e.get('dev-entry'))
  call('/usr/bin/hdiutil','detach',dev)
logical,regions=packed_ramdisk.regions(SRC.read_bytes());assert logical==1024**3
with tempfile.TemporaryDirectory(prefix='p21-official-data-',dir=str(O)) as td:
 td=Path(td);raw=td/'copy.raw';mount=td/'mount';mount.mkdir()
 with raw.open('wb') as f:
  f.truncate(logical)
  for off,data in regions:f.seek(off);f.write(data)
 mounted=False;whole=None
 try:
  entities=plistlib.loads(call('/usr/bin/hdiutil','attach','-imagekey','diskimage-class=CRawDiskImage','-nomount','-nobrowse','-plist',raw))['system-entities']
  devs=[x['dev-entry'] for x in entities if 'dev-entry' in x];whole=devs[0]
  volumes=[d for d in devs if info(d).get('VolumeUUID')==EXPECTED_UUID];assert len(volumes)==1,volumes
  volume=volumes[0];call('/sbin/mount_apfs','-o','nobrowse,owners',volume,mount);mounted=True
  assert info(mount)['GlobalPermissionsEnabled'] and info(mount)['WritableVolume']
  before={};after={}
  for name in PREFS:
   p=mount/'mobile/Library/Preferences'/(name+'.plist');st=p.stat();before[name]={'uid':st.st_uid,'gid':st.st_gid,'mode':oct(st.st_mode&0o777)}
   os.chown(p,501,501);os.chmod(p,0o600)
  call('/sbin/umount',mount);mounted=False
  call('/sbin/mount_apfs','-o','rdonly,nobrowse,owners',volume,mount);mounted=True
  assert info(mount)['GlobalPermissionsEnabled'] and not info(mount)['WritableVolume']
  values={}
  for name in PREFS:
   p=mount/'mobile/Library/Preferences'/(name+'.plist');st=p.stat();assert (st.st_uid,st.st_gid,st.st_mode&0o777)==(501,501,0o600)
   values[name]=plistlib.loads(p.read_bytes());after[name]={'uid':st.st_uid,'gid':st.st_gid,'mode':'0600','sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
  assert values['com.apple.migration']['LastSystemVersion']=='21H461'
  assert values['com.apple.migration']['DMUserDataDisposition']['buildVersion']=='21H461'
  assert values['com.apple.purplebuddy']['SetupDone'] and values['com.apple.purplebuddy']['SetupFinishedAllSteps']
  for key in ['SBDisableAutoDim','SBDontDimOrLockOnAC','SBDisableIdleSleep']:assert values['com.apple.springboard'][key] is True
  for sub in ['Bundle','Data/System','Shared/SystemGroup','Temp']:assert (mount/'containers'/sub).is_dir()
  call('/sbin/umount',mount);mounted=False
  fsck=subprocess.run(['/sbin/fsck_apfs','-n',volume],capture_output=True,text=True);(O/'fsck-apfs.log').write_text(fsck.stdout+fsck.stderr);assert fsck.returncode==0
 finally:
  if mounted:call('/sbin/umount',mount)
  if whole:call('/usr/bin/hdiutil','detach',whole)
 metadata=packed_ramdisk.pack(raw,DEST)
 result={'result':'OFFICIAL_SKIP_SETUP_READONLY_REMOUNT_PASS','official_script_revision':'16e592abee8a550dec8154ed1f38b5ebc4f16213','input_official_seed_sha256':hashlib.sha256(SRC.read_bytes()).hexdigest(),'output':metadata,'volume_uuid':EXPECTED_UUID,'build':'21H461','skip_setup':True,'no_auto_dim':True,'before_ownership':before,'verified_preferences':after,'fsck_apfs_exit':0,'phone_operations':[],'base_seed_unchanged':True,'persistent_store_enabled':False}
 (O/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
