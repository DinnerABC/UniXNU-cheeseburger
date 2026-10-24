#!/usr/bin/env python3
"""Inspect actual donor metadata with the pinned upstream parser, no image edits."""
from pathlib import Path
import hashlib,json,subprocess,sys
r=Path(__file__).resolve().parents[1];kit=r/'.work/p4-unixnu/kits/NativePrelink';sys.path.insert(0,str(kit));import native_prelink as np
o=r/'out/p10/components';o.mkdir(exist_ok=True)
for candidate in ['A','B']:
 p=r/'out/p10/inputs'/candidate/'kernelcache.macho';data=p.read_bytes();m=np.macho(data);s=next(x for x in m['segments'] if x['name']=='__PRELINK_INFO');at=np.backed_offset(m['segments'],s['va'],s['filesize'],len(data));metadata=np.unserialize_xml(data[at:at+s['filesize']]);entries={x['CFBundleIdentifier']:x for x in metadata['_PrelinkInfoDictionary']};selected={};pending=['com.apple.security.AppleImage4','com.apple.driver.AppleMobileFileIntegrity','com.apple.kext.CoreTrust']
 while pending:
  name=pending.pop()
  if name in selected:continue
  e=entries[name];selected[name]=e;pending.extend(e.get('OSBundleLibraries',{}))
 (o/(candidate+'-metadata.json')).write_text(json.dumps(selected,indent=2,default=lambda x:dict(bytes=len(x),sha256=hashlib.sha256(x).hexdigest()) if isinstance(x,bytes) else str(x))+'\n')
 summary=[]
 for name,e in selected.items():
  row=dict(bundle=name,version=e.get('CFBundleVersion'),security_extension=e.get('AppleSecurityExtension',False),dependencies=e.get('OSBundleLibraries',{}),kernel_resource=e.get('OSKernelResource',False))
  if e.get('_PrelinkExecutableSize') and not e.get('OSKernelResource'):
   va=e['_PrelinkExecutableLoadAddr'];module=np.macho(data,np.backed_offset(m['segments'],va,32,len(data)));row.update(original_address=hex(va),kind=module['kind'],kmod_info=hex(e['_PrelinkKmodInfo']),segments=module['segments'])
  summary.append(row)
 (o/(candidate+'-summary.json')).write_text(json.dumps(summary,indent=2)+'\n');print(candidate,[(x['bundle'],x['version']) for x in summary])
native=(r/'out/p9/kernel-v2/kernel.macho').read_bytes();donor=(r/'out/p10/inputs/A/kernelcache.macho').read_bytes()
try:
 report=np.plan(native,donor,['com.apple.security.AppleImage4','com.apple.driver.AppleMobileFileIntegrity','com.apple.kext.CoreTrust'],0)
 (o/'initial-plan.json').write_text(json.dumps(report,indent=2)+'\n');print('Initial upstream plan PASS')
except ValueError as e:
 (o/'initial-plan-error.json').write_text(json.dumps(dict(error=str(e),official_tool=True,images_modified=False),indent=2)+'\n');print('Initial upstream plan refused:',e)
