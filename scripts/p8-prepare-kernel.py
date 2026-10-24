#!/usr/bin/env python3
"""Keep the proven switch, replace transient stripes with a persistent console."""
from pathlib import Path
import re,difflib,json,plistlib
r=Path(__file__).resolve().parents[1];k=r/'.work/p4-unixnu/.cache/xnu-work';out=r/'out/p8/kernel-source';out.mkdir(parents=True,exist_ok=True)
def edit(name,fn):
 p=k/name;before=p.read_text();after=fn(before)
 if after!=before:
  (out/(p.name+'.p7')).write_text(before);p.write_text(after)
  (r/'patches/p8'/(p.name+'.patch')).write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/'+name,tofile='b/'+name)))
 (out/p.name).write_text(after)
def asm(s):
 s=s.replace('.section __BOOTDATA,__data\n\t.align 3\n\t.globl EXT(p6_diag_record)', '.section __DATA,__data\n\t.align 3\n\t.globl EXT(p6_diag_record)')
 return re.sub(r'^\s*P[67]_BAR .*$', '\t// P8: no per-stage visual probes',s,flags=re.M)
edit('osfmk/arm64/start.s',asm)
def cpoints(s):
 s=s.replace('#if defined(ARM64_BOARD_CONFIG_MSM8998)\n#define P7_C_POINT','#if 0 /* P8 uses the functional text console */\n#define P7_C_POINT')
 if 'arm_setup_pre_sign();' in s:
  start=s.index('\t__asm__ volatile(',s.index('arm_init(\n'))
  vbar=s.index('\t    "adrp x9, _ExceptionVectorsBase@PAGE',start)
  s=s[:start]+'\t__asm__ volatile(\n'+s[vbar:]
 return s
edit('osfmk/arm/arm_init.c',cpoints)
edit('osfmk/arm64/arm_vm_init.c',cpoints)
console=r/'drivers/p8/FramebufferConsole.inc'
edit('osfmk/console/serial_console.c',lambda s:s if 'P8 persistent' in s else s.replace('void\nconsole_write(char *str, int size)','\n'+console.read_text()+'\nvoid\nconsole_write(char *str, int size)').replace('console_write(char *str, int size)\n{','console_write(char *str, int size)\n{\n#if defined(ARM64_BOARD_CONFIG_MSM8998)\n\tif (p8_console_ready()) { for (int p8i=0; p8i<size; p8i++) p8_console_char(str[p8i]); return; }\n#endif').replace('console_write_unbuffered(char c)\n{','console_write_unbuffered(char c)\n{\n#if defined(ARM64_BOARD_CONFIG_MSM8998)\n\tif (p8_console_ready()) { p8_console_char(c); return; }\n#endif'))
# Profiles are derived from the actual pinned storage family, never ABI-offset stubs.
raw=(r/'upstream/IOStorageFamily/Info.plist').read_text();raw=re.sub(r'#if TARGET_OS_OSX.*?#endif[^\n]*','',raw,flags=re.S)
ps=plistlib.loads(raw.encode())['IOKitPersonalities']
selected={name:ps[name] for name in ['IOBlockStorageDriver','IOGUIDPartitionScheme','IOFDiskPartitionScheme','IOMediaBSDClient']}
selected['NativeUfs']={'IOClass':'NativeUfs','IOProviderClass':'IOPlatformDevice','IONameMatch':'qcom,ufshc','IOProbeScore':1000,'CFBundleIdentifier':'org.unixnu.cheeseburger.NativeUfs'}
def serialize(v):
 if isinstance(v,bool):return '.true.' if v else '.false.'
 if isinstance(v,int):return str(v)+':32'
 if isinstance(v,str):return "'"+v+"'"
 if isinstance(v,list):return '('+','.join(serialize(x) for x in v)+')'
 if isinstance(v,dict):return '{'+''.join(serialize(key)+'='+serialize(val)+';' for key,val in v.items())+'}'
 raise ValueError(type(v))
entries=','.join(serialize(v) for v in selected.values())+','
def tables(s):
 if "org.unixnu.cheeseburger.NativeUfs" in s:return s
 return s.replace('    "("\n','    "("\n    '+json.dumps(entries)+'\n',1)
edit('iokit/KernelConfigTables.cpp',tables)
(r/'config/p8/personalities.json').write_text(json.dumps(selected,indent=2)+'\n')
print('P8 permanent DATA console record, functional console and plist-derived storage matching applied')
