from pathlib import Path
import json,plistlib,difflib
r=Path(__file__).resolve().parents[1];k=r/'.work/p4-unixnu/.cache/xnu-work';out=r/'out/p11/kernel-source';out.mkdir(parents=True,exist_ok=True);patch=r/'patches/p11';patch.mkdir(parents=True,exist_ok=True)
for rel in ['osfmk/kern/startup.c','iokit/KernelConfigTables.cpp']:
 p=k/rel;old=p.read_text();saved=out/(p.name+'.p10')
 if not saved.exists():saved.write_text(old)
 if rel.endswith('startup.c'):
  anchor='\tkernel_bootstrap_log("mac_policy_initmach");'
  hook='''#if defined(ARM64_BOARD_CONFIG_MSM8998)
 extern void cheeseburger_pmgr_startup(void);
 extern void cheeseburger_ufs_register(void);
 kernel_bootstrap_log("cheeseburger_ufs_register");
 cheeseburger_ufs_register();
 kernel_bootstrap_log("cheeseburger_pmgr_startup");
 cheeseburger_pmgr_startup();
#endif
'''
  if 'cheeseburger_pmgr_startup();' not in old:
   assert old.count(anchor)==1;new=old.replace(anchor,hook+anchor)
  else:new=old
  assert new.index('PE_init_iokit();')<new.index('(void) spllo();')<new.index('cheeseburger_pmgr_startup();')<new.index('mac_policy_initmach();')
 else:
  props=plistlib.loads((r/'drivers/CheeseburgerPlatformPower/Info.plist').read_bytes())['IOKitPersonalities']['CheeseburgerPlatformPower']
  props.pop('CFBundleIdentifier',None) # built-in archive, not an external kext request
  def val(x):return str(x)+':32' if isinstance(x,int) else "'"+x+"'"
  entry='{'+''.join(val(key)+'='+val(v)+';' for key,v in props.items())+'},'
  new=old if "'IOClass'='CheeseburgerPlatformPower'" in old else old.replace('    "("\n','    "("\n    '+json.dumps(entry)+'\n',1)
  new=new.replace("'CFBundleIdentifier'='org.unixnu.cheeseburger.NativeUfs';",'').replace("'CFBundleIdentifier'='org.unixnu.cheeseburger.PlatformPower';",'')
  assert "'IOClass'='NativeUfs'" in new and "'IOClass'='CheeseburgerPlatformPower'" in new
 p.write_text(new);(out/p.name).write_text(new)
 (patch/(p.name+'.patch')).write_text(''.join(difflib.unified_diff(saved.read_text().splitlines(True),new.splitlines(True),fromfile='a/'+rel,tofile='b/'+rel)))
print('P11 real personality and post-IOKit/pre-AMFI startup ordering applied')
