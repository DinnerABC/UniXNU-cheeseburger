#!/usr/bin/env python3
"""Check final linked classes, lifetime and address translation; no hardware claim."""
from pathlib import Path
import json,hashlib,struct,sys
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools/adt'));from adt import decode
o=r/'out/p8';syms=(o/'kernel/tool-validation/symbols.log').read_text();table=(o/'kernel-source/KernelConfigTables.cpp').read_text();checks={}
for symbol in ['_p6_diag_record','_p8_console_ring','_p8_console_count','_iso_font']:
 line=next(x for x in syms.splitlines() if x.endswith(' '+symbol));assert '(__DATA,' in line;checks[symbol]=line
for name in ['NativeUfs','NativeUfsLun','IOBlockStorageDevice','IOBlockStorageDriver','IOStorage','IOMedia','IOMediaBSDClient','IOGUIDPartitionScheme']:
 assert name in syms
 if name not in ['NativeUfsLun','IOBlockStorageDevice','IOStorage','IOMedia']:assert "'IOClass'='"+name+"'" in table
assert (o/'kernel/tool-validation/undefined.log').read_text().strip()==''
t=decode((o/'adt/kernel.macho.adt').read_bytes());a=next(x for x in t['children'] if x['name']=='arm-io');rr=a['properties']['ranges'];ranges=[struct.unpack_from('<QQQ',rr,i) for i in range(0,len(rr),24)];assert ranges[0]==(0,0x17a00000,0x200000)
resolved={}
for node in a['children']:
 reg=node['properties'].get('reg',b'');pairs=[struct.unpack_from('<QQ',reg,i) for i in range(0,len(reg),16)]
 result=[]
 for base,size in pairs:
  matches=[parent+base-child for child,parent,length in ranges if base>=child and base-child<=length and size<=length-(base-child)]
  assert len(matches)==1,(node['name'],pairs,ranges);result.append((matches[0],size))
 resolved[node['name']]=result
assert resolved['intc']==[(0x17a00000,0x10000),(0x17b00000,0x100000)]
assert resolved['ufshc@1da4000']==[(0x1da4000,0x2500)]
ufs=(r/'drivers/p8/NativeUfs/NativeUfs.cpp').read_text();assert 'smmuBypassStream' not in ufs and 'native_ufs_store_write' not in ufs
# Preserve raw build and test errors; this checker only certifies static linkage.
(o/'integration-check.json').write_text(json.dumps(dict(result='PASS',persistent_symbols=checks,translated_nodes=resolved,classes_linked=True,undefined_symbols=0,hardware_tested=False),indent=2)+'\n');print('Final class linkage, permanent DATA and bounded bus translations PASS (offline only)')
