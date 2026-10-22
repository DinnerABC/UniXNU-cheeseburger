#!/usr/bin/env python3
"""Apply reversible RAM-only payload filesystem and PEI reservation to guest copy."""
from pathlib import Path
import shutil,difflib
r=Path(__file__).resolve().parents[1];s=Path('/home/dinnerabc.guest/unixnu-p3/diagnostic')
assert (s/'P4Integration/Probe/P4Probe.inf').exists()
shutil.copytree(r/'config/p4/RamPayload',s/'P4Integration/RamPayload',dirs_exist_ok=True)
m='Platforms/Msm8998FamilyPkg/MemoryInitPei/MemoryInitPeiLib.c';p=s/m;before=p.read_text();after=before
if 'P4 RESERVED INITRD' not in before:
 after=after.replace('#include <Library/PlatformMemoryMapLib.h>','#include <Library/PlatformMemoryMapLib.h>\n#include "P4Range.h"')
 marker='  // Last one (terminator)'
 code='''  /* P4 RESERVED INITRD: valid bootloader range only, entirely conventional RAM.
   * Never reserve over firmware/MMIO or an existing non-conventional descriptor. */
  {
    UINT64 Begin, End, PageBegin, PageEnd; BOOLEAN Valid=FALSE, Conflict=FALSE;
    PARM_MEMORY_REGION_DESCRIPTOR_EX D;
    if (P4InitrdRange(Fdt,&Begin,&End) && End-Begin>=sizeof("P4BNDL01")-1 &&
        CompareMem((VOID*)(UINTN)Begin,"P4BNDL01",8)==0) {
      PageBegin=Begin & ~((UINT64)4095); PageEnd=(End+4095) & ~((UINT64)4095);
      for(D=GetPlatformMemoryMap(); D->Length; D++) {
        if((D->HobOption==Mem4G || D->HobOption==Mem6G || D->HobOption==Mem8G) && D->HobOption!=Mem)continue;
        if(D->HobOption==NoHob)continue;
        if(D->MemoryType==EfiConventionalMemory && PageBegin>=D->Address && PageEnd-D->Address<=D->Length)Valid=TRUE;
        else if(D->MemoryType!=EfiConventionalMemory && PageBegin<D->Address+D->Length && D->Address<PageEnd)Conflict=TRUE;
      }
      if(Valid && !Conflict && !(PageBegin<UefiMemoryBase+UefiMemorySize && UefiMemoryBase<PageEnd))
        BuildMemoryAllocationHob(PageBegin,PageEnd-PageBegin,EfiReservedMemoryType);
    }
  }

'''
 after=after.replace(marker,code+marker);p.write_text(after)
shutil.copy2(r/'config/p4/RamPayload/Range.h',p.parent/'P4Range.h')
patch=list(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/'+m,tofile='b/'+m))
base='Platforms/Msm8998Pkg/Device/oneplus/cheeseburger/'
for name in ['cheeseburger.dsc','cheeseburger.fdf']:
 p=s/(base+name);before=p.read_text();after=before
 if 'P4Integration/RamPayload/P4RamPayload.inf' not in before:
  if name.endswith('dsc'):after+='\n[Components]\n  P4Integration/RamPayload/P4RamPayload.inf\n'
  else:after=after.replace('    INF P4Integration/Probe/P4Probe.inf','    INF P4Integration/RamPayload/P4RamPayload.inf\n    INF P4Integration/Probe/P4Probe.inf')
  p.write_text(after);patch.extend(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/'+base+name,tofile='b/'+base+name))
if patch:(r/'patches/p4/uefi-rampayload.patch').write_text(''.join(patch))
print('RAM payload overlay ready in retained guest tree')
