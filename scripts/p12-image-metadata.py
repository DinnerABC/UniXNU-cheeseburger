from pathlib import Path
import json,subprocess,plistlib,re
r=Path(__file__).resolve().parents[1];o=r/'out/p12/reference';components=json.loads((o/'components.json').read_text());mp=o/'metadata.json';metadata=json.loads(mp.read_text()) if mp.exists() else {}
for name,record in components.items():
 if name in metadata and metadata[name]['component']['sha256']==record['sha256']:continue
 p=r/record['file'];info_bytes=subprocess.check_output(['hdiutil','imageinfo','-plist',str(p)]);info=plistlib.loads(info_bytes);total=info['Size Information']['Total Bytes'];(o/(p.name+'.imageinfo.plist')).write_bytes(info_bytes)
 with p.open('rb') as f:f.seek(-512,2);udif=f.read(4)==b'koly'
 old=r/'out/p12/logs/metadata-os-01.log';previous=udif and name=='OS' and old.exists() and ('checksum of "'+str(p)+'" is VALID') in old.read_text()
 if udif and not previous:subprocess.run(['hdiutil','verify',str(p)],check=True)
 views=plistlib.loads(subprocess.check_output(['hdiutil','info','-plist']))['images'];found=[x for x in views if Path(x['image-path']).resolve()==p.resolve()]
 if not found:
  text=subprocess.check_output(['hdiutil','attach','-readonly','-nomount','-noautofsck',str(p)],text=True);(o/(p.name+'.attach.log')).write_text(text)
  views=plistlib.loads(subprocess.check_output(['hdiutil','info','-plist']))['images'];found=[x for x in views if Path(x['image-path']).resolve()==p.resolve()]
 assert len(found)==1 and found[0].get('writeable') is False
 view=found[0];entities=view['system-entities'];whole=[e['dev-entry'] for e in entities if e.get('content-hint')=='GUID_partition_scheme']
 if not whole:whole=[e['dev-entry'] for e in entities if re.fullmatch('/dev/disk[0-9]+',e.get('dev-entry',''))][:1]
 assert len(whole)==1;dev=whole[0];disk=plistlib.loads(subprocess.check_output(['diskutil','info','-plist',dev]));assert disk.get('Writable') is False and disk.get('BusProtocol')=='Disk Image'
 try:
  (o/(p.name+'.disk-list.plist')).write_bytes(subprocess.check_output(['diskutil','list','-plist',dev]));vol=[];containers=[]
  for e in entities:
   if 'dev-entry' not in e:continue
   v=plistlib.loads(subprocess.check_output(['diskutil','info','-plist',e['dev-entry']]));vol.append({k:v[k] for k in ['DeviceNode','FilesystemType','FilesystemName','VolumeName','VolumeUUID','DiskUUID','Content','APFSVolumeRole','TotalSize','Writable'] if k in v})
   if e.get('content-hint')=='Apple_APFS':
    result=subprocess.run(['diskutil','apfs','list','-plist',e['dev-entry']],capture_output=True)
    if result.returncode==0:containers.extend(plistlib.loads(result.stdout).get('Containers',[]))
  metadata[name]={'component':record,'virtual_disk_bytes':total,'image_format':info.get('Format'),'udif_checksum_verified':bool(udif),'checksum_evidence':'metadata-os-01.log' if previous else 'current command','entities':vol,'apfs_containers':containers}
  mp.write_text(json.dumps(metadata,indent=2,default=str)+'\n');print(name,json.dumps(metadata[name],default=str),flush=True)
 finally:subprocess.run(['hdiutil','detach',dev],check=True)
capacity=json.loads((r/'out/p12/geometry.json').read_text())['userdata']['capacity_bytes'];total=1<<20
for v in metadata.values():total+=(v['virtual_disk_bytes']+(1<<20)-1)//(1<<20)*(1<<20)
assert total<=capacity
(o/'capacity-check.json').write_text(json.dumps({'virtual_root_image_bytes':total,'userdata_capacity_bytes':capacity,'fits':True,'all_components_present':len(metadata)==2,'reference_only':True,'install_allowed':False},indent=2)+'\n')
p=r/'config/p12/board.toml';s=p.read_text().split('\n[ipsw.sha256]')[0];s+='\n[ipsw.sha256]\n'+''.join(json.dumps(Path(v['file']).name)+' = '+json.dumps(v['sha256'])+'\n' for v in components.values());p.write_text(s)
