#!/usr/bin/env python3
"""Configure guest overlayfs P5 build; lower P3/P4 diagnostic source is read-only."""
from pathlib import Path
import shutil,difflib,json
r=Path(__file__).resolve().parents[1];s=Path('/home/dinnerabc.guest/unixnu-p5/source');assert (s/'.p5-overlay-marker').exists()
shutil.copytree(r/'config/p4/RamPayload',s/'P5Integration/RamPayload',dirs_exist_ok=True)
shutil.copytree(r/'config/p5/Probe',s/'P5Integration/Probe',dirs_exist_ok=True)
shutil.copy2(r/'config/p5/Expected.h',s/'DarwinBootPkg/Driver/P5Expected.h')
shutil.copy2(r/'config/p5/Expected.h',s/'P5Integration/Probe/P5Expected.h')
base='Platforms/Msm8998Pkg/Device/oneplus/cheeseburger/'
for name in ['cheeseburger.dsc','cheeseburger.fdf']:
 p=s/(base+name);t=p.read_text().replace('P4Integration/Probe/P4Probe.inf','P5Integration/Probe/P5Probe.inf').replace('P4Integration/RamPayload/P4RamPayload.inf','P5Integration/RamPayload/P4RamPayload.inf');
 if name.endswith('dsc') and '  RngLib|' not in t:
  t+='\n[LibraryClasses]\n  RngLib|MdePkg/Library/BaseRngLibNull/BaseRngLibNull.inf\n'
 p.write_text(t)
p=s/'DarwinBootPkg/Driver/DarwinBoot.c';before=p.read_text();after=before
if '#include "P5Expected.h"' not in before:
 after=after.replace('#include <Library/PrintLib.h>','#include <Library/PrintLib.h>\n#include <Library/BaseCryptLib.h>\n#include "P5Expected.h"')
 needle='  Status = ParseKernel (Image, ImageSize, &Kernel);'
 after=after.replace(needle,'''  {
    UINT8 Hash[32];
    if (ImageSize != P5KernelBytes || !Sha256HashAll(Image,ImageSize,Hash) || CompareMem(Hash,P5KernelHash,32)!=0) { Status=EFI_SECURITY_VIOLATION; goto Fail; }
    Print(L"P5 Stage1: kernel read SHA256 PASS bytes=%lu\\r\\n",(UINT64)ImageSize);
  }
'''+needle)
 needle='  Length = StrSize (Request->KernelPath);'
 after=after.replace(needle,'  Print(L"P5 Stage2: Mach-O parsed; entry=0x%lx\\r\\n",Kernel.Entry);\n'+needle)
 needle='  Offset = 0;\n  RamdiskOffset = MAX_UINTN;'
 after=after.replace(needle,'''  {
    UINT8 Hash[32];
    if (TreeSize != P5AdtBytes || !Sha256HashAll(Tree,TreeSize,Hash) || CompareMem(Hash,P5AdtHash,32)!=0) { Status=EFI_SECURITY_VIOLATION; goto Fail; }
    Print(L"P5 ADT read SHA256 PASS bytes=%lu\\r\\n",(UINT64)TreeSize);
  }
'''+needle)
 after=after.replace('  //\n  // The same facts in large type','  Print(L"P5 Stage3: ADT/BootArgs/memory-map ready. Green=loader after EBS; magenta=XNU __start.\\r\\n");\n  gBS->Stall(20000000);\n\n  //\n  // The same facts in large type')
 after=after.replace('means the kernel entry code ran.','means DarwinEnter ran AFTER EBS; NOT XNU.\\nMagenta top stripe is emitted ONLY by XNU __start.')
 p.write_text(after)
 (r/'patches/p5/darwinboot-stage-gates.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/DarwinBootPkg/Driver/DarwinBoot.c',tofile='b/DarwinBootPkg/Driver/DarwinBoot.c')))
p=s/'DarwinBootPkg/Driver/DarwinBoot.inf';t=p.read_text()
if '  BaseCryptLib' not in t:t=t.replace('  BaseLib','  BaseLib\n  BaseCryptLib').replace('  MdePkg/MdePkg.dec','  MdePkg/MdePkg.dec\n  CryptoPkg/CryptoPkg.dec');p.write_text(t)
print('P5 isolated overlay configured; real kernel/ADT SHA256 gates enabled')
