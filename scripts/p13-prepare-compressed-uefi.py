#!/usr/bin/env python3
from pathlib import Path
import difflib,json,hashlib,shutil
r=Path(__file__).resolve().parents[1];s=Path('/home/dinnerabc.guest/unixnu-p9/source');assert (s/'.p13-overlay-marker').exists()
o=r/'out/p13/compressed/source';o.mkdir(parents=True,exist_ok=True);changes=[]
def edit(rel,fn):
 p=s/rel;old=p.read_text();new=fn(old);assert old!=new,rel
 changes.extend(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/'+rel,tofile='b/'+rel));p.write_text(new)
base='P5Integration/RamPayload/'
shutil.copy2(r/'config/p13/CompressedPayload.h',s/base/'CompressedPayload.h')
edit(base+'P4RamPayload.c',lambda t:t.replace('#include "Range.h"','#include "Range.h"\n#include "CompressedPayload.h"').replace('UINT64 Start,End;HEADER *H;','UINT64 Start,End,PayloadSize;VOID *Payload;HEADER *H;').replace(' H=(HEADER*)(UINTN)Start;',' Status=P13Expand(Start,End,&Payload,&PayloadSize);if(EFI_ERROR(Status))return Status;\n H=Payload;').replace('End-Start<sizeof(*H)','PayloadSize<sizeof(*H)').replace('H->Size!=End-Start','H->Size!=PayloadSize'))
edit(base+'P4RamPayload.inf',lambda t:t.replace('  Range.h','  Range.h\n  CompressedPayload.h').replace('  MdePkg/MdePkg.dec','  MdePkg/MdePkg.dec\n  MdeModulePkg/MdeModulePkg.dec\n  CryptoPkg/CryptoPkg.dec').replace('  BaseLib\n','  BaseLib\n  BaseCryptLib\n'))
rel='Platforms/Msm8998Pkg/Device/oneplus/cheeseburger/cheeseburger.dsc'
edit(rel,lambda t:t.replace('  P5Integration/RamPayload/P4RamPayload.inf','  P5Integration/RamPayload/P4RamPayload.inf {\n    <LibraryClasses>\n      NULL|MdeModulePkg/Library/LzmaCustomDecompressLib/LzmaCustomDecompressLib.inf\n  }'))
rel='Platforms/Msm8998FamilyPkg/MemoryInitPei/MemoryInitPeiLib.c'
edit(rel,lambda t:t.replace('CompareMem((VOID*)(UINTN)Begin,"P4BNDL01",8)==0','(CompareMem((VOID*)(UINTN)Begin,"P4BNDL01",8)==0 || CompareMem((VOID*)(UINTN)Begin,"P13LZ001",8)==0)'))
(r/'patches/p13/compressed-transport.patch').write_text(''.join(changes))
for rel in [base+'P4RamPayload.c',base+'P4RamPayload.inf',base+'CompressedPayload.h','Platforms/Msm8998Pkg/Device/oneplus/cheeseburger/cheeseburger.dsc','Platforms/Msm8998FamilyPkg/MemoryInitPei/MemoryInitPeiLib.c']:
 dest=o/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(s/rel,dest)
(o/'manifest.json').write_text(json.dumps({str(p.relative_to(o)):hashlib.sha256(p.read_bytes()).hexdigest() for p in o.rglob('*') if p.is_file() and p.name!='manifest.json'},indent=2)+'\n')
print('P13 compressed transport prepared in private upper; P12 unchanged')
