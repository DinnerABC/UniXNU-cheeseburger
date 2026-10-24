from pathlib import Path
import json,sys
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'.work/p4-unixnu/kits/NativePrelink'));import native_prelink as np
b=(r/'out/p10/inputs/A/kernelcache.macho').read_bytes();m=np.macho(b);s=next(x for x in m['segments'] if x['name']=='__PRELINK_INFO');p=np.unserialize_xml(b[np.backed_offset(m['segments'],s['va'],s['filesize'],len(b)):][:s['filesize']]);entries={x['CFBundleIdentifier']:x for x in p['_PrelinkInfoDictionary']}
selected={k:{'version':v.get('CFBundleVersion'),'dependencies':v.get('OSBundleLibraries',{}),'personalities':list(v.get('IOKitPersonalities',{}))} for k,v in entries.items() if any(x in k.lower() for x in ['apfs','hfs','storagefamily'])}
o=r/'out/p12/prelink';o.mkdir(exist_ok=True);(o/'available-filesystems.json').write_text(json.dumps(selected,indent=2)+'\n')
roots=['com.apple.security.AppleImage4','com.apple.driver.AppleMobileFileIntegrity','com.apple.kext.CoreTrust','com.apple.kec.pthread','com.apple.filesystems.apfs','com.apple.filesystems.hfs.kext']
try:
 plan=np.plan((r/'out/p11/native/kernel.macho').read_bytes(),b,roots,-0xc2d000);(o/'preliminary-plan.json').write_text(json.dumps(plan,indent=2)+'\n');print('PASS',plan['components'])
except Exception as e:
 (o/'preliminary-refusal.json').write_text(json.dumps({'error':str(e),'roots':roots,'modified_images':False},indent=2)+'\n');print('REFUSED',e);raise
