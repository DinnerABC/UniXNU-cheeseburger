from pathlib import Path
import json,plistlib,subprocess
r=Path(__file__).resolve().parents[1];o=r/'out/p12/system-consistency-v2';o.mkdir(exist_ok=True);meta=json.loads((r/'out/p12/reference/metadata.json').read_text());results={}
for component,m in meta.items():
 label='OS' if component=='OS' else 'SystemOS';mount=o/('mount-'+label);mount.mkdir(exist_ok=True);p=r/m['component']['file'];proc=subprocess.run(['hdiutil','attach','-readonly','-nomount','-noautofsck',str(p)],capture_output=True,text=True);(o/(label+'-attach.log')).write_text(proc.stdout+proc.stderr);proc.check_returncode()
 views=plistlib.loads(subprocess.check_output(['hdiutil','info','-plist']))['images']
 view=next(v for v in views if Path(v['image-path']).resolve()==p.resolve());assert view.get('writeable') is False
 devices=[e['dev-entry'] for e in view['system-entities'] if e.get('dev-entry')]
 volume=None
 for node in devices:
  info=plistlib.loads(subprocess.check_output(['diskutil','info','-plist',node]))
  if info.get('FilesystemType')=='apfs' and info.get('VolumeUUID') in [e.get('VolumeUUID') for e in m['entities']]:volume=node
 assert volume
 whole=devices[0]
 disk=plistlib.loads(subprocess.check_output(['diskutil','info','-plist',whole]));assert disk.get('BusProtocol')=='Disk Image' and disk.get('Writable') is False

 try:
  proc=subprocess.run(['diskutil','mount','readOnly','-mountPoint',str(mount),volume],capture_output=True,text=True);(o/(label+'-mount.log')).write_text(proc.stdout+proc.stderr);proc.check_returncode()
  info=plistlib.loads(subprocess.check_output(['diskutil','info','-plist',str(mount)]));assert info.get('Writable') is False and info.get('FilesystemType')=='apfs';actual=info['VolumeUUID'];expected=[x['VolumeUUID'] for x in m['entities'] if x.get('FilesystemType')=='apfs'];assert expected==[actual]
  row={'volume_uuid':actual,'filesystem':'apfs','readonly':True,'reference_product':'iPad7,1','build':'21H461','apple_boot_signature_verified':False}
  version=mount/'System/Library/CoreServices/SystemVersion.plist'
  if version.exists():
   v=plistlib.loads(version.read_bytes());assert v['ProductBuildVersion']=='21H461' and v['ProductVersion']=='17.7.11';row['system_version']=v
  else:row['system_version_present']=False
  candidate=mount/('sbin/launchd' if component=='OS' else 'usr/lib/dyld')
  if component=='OS':assert candidate.is_file(),'Reference OS lacks launchd'
  if candidate.is_file():
   arch=subprocess.check_output(['xcrun','lipo','-archs',str(candidate)],text=True).strip();assert 'arm64' in arch.split(),'Require actual ordinary ARM64 slice';row['architecture']=arch
   check=subprocess.run(['codesign','--verify','--strict','--verbose=2',str(candidate)],capture_output=True,text=True);(o/(label+'-codesign.log')).write_text(check.stdout+check.stderr);row['codesign_verify_exit_code']=check.returncode;row['codesign_scope']='Host CodeDirectory/code signature integrity result, not Apple secure-boot chain certification'
  container=info.get('APFSContainerReference')
  if container:
   result=subprocess.run(['diskutil','apfs','list','-plist',str(container)],capture_output=True)
   if result.returncode==0:(o/(label+'-apfs.plist')).write_bytes(result.stdout);row['container_metadata_saved']=True
  results[label]=row;(o/'result.json').write_text(json.dumps(results,indent=2,default=str)+'\n');print(label,row,flush=True)
 finally:subprocess.run(['hdiutil','detach',whole],check=True)
