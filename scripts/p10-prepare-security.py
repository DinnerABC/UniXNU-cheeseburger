#!/usr/bin/env python3
"""Preserve original registration/validation; report actual registration and return."""
from pathlib import Path
import difflib,shutil
r=Path(__file__).resolve().parents[1];k=r/'.work/p4-unixnu/.cache/xnu-work'
for name,before,after in [
 ('libkern/img4/interface.c','\timg4if = i4;','\timg4if = i4;\n\tprintf("P10 Image4 registered: interface=%u\\n", i4->i4if_version);'),
 ('libkern/amfi/amfi.c','\tamfi = mfi;','\tamfi = mfi;\n\tprintf("P10 AMFI registered: TrustCache interface=%u\\n", mfi->TrustCache.version);'),
 ('osfmk/kern/startup.c','\ttrust_cache_runtime_init();','\ttrust_cache_runtime_init();\n\tkernel_bootstrap_log("P10 Trust Cache Runtime initialized");')]:
 p=k/name;s=p.read_text()
 if after not in s:
  assert before in s
  old=r/'out/p10/baseline/source'/name;old.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,old)
  new=s.replace(before,after,1);patch=r/'patches/p10'/(p.name+'.patch');patch.write_text(''.join(difflib.unified_diff(s.splitlines(True),new.splitlines(True),fromfile='a/'+name,tofile='b/'+name)));p.write_text(new)
 dst=r/'out/p10/source'/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dst)
print('Original registrations and trust-cache validation preserved; three real progress messages added')
