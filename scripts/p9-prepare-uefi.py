#!/usr/bin/env python3
from pathlib import Path
import shutil,re,difflib
r=Path(__file__).resolve().parents[1];s=Path('/home/dinnerabc.guest/unixnu-p9/source');assert (s/'.p9-overlay-marker').exists()
p=s/'P9Integration/Probe';p.mkdir(parents=True,exist_ok=True)
for name in ['Range.h','P7Probe.inf','P7Probe.c']:
 text=(r/'config/p7/Probe'/name).read_text().replace('P7','P9')
 if name.endswith('.c'):
  start=text.index('STATIC VOID EFIAPI Ready(');end=text.index('EFI_STATUS EFIAPI P9ProbeEntry(',start)
  text=text.replace('#include "Range.h"','// P9 omits the old register-range diagnostic helpers')
  start=text.index('STATIC VOID EFIAPI Ready(');end=text.index('EFI_STATUS EFIAPI P9ProbeEntry(',start)
  text=text[:start]+'''STATIC VOID EFIAPI Ready(EFI_EVENT Event,VOID *Context){
 EFI_HANDLE *Handles=NULL;UINTN Count=0,I;EFI_STATUS S,K,A;EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;EFI_FILE_PROTOCOL *Root;
 gBS->CloseEvent(Event);
 Print(L"P9 integrated read-only payload; next output is XNU text console\\r\\n");
 S=gBS->LocateHandleBuffer(ByProtocol,&gEfiSimpleFileSystemProtocolGuid,NULL,&Count,&Handles);
 if(!EFI_ERROR(S))for(I=0;I<Count;I++){
  S=gBS->HandleProtocol(Handles[I],&gEfiSimpleFileSystemProtocolGuid,(VOID**)&Fs);if(EFI_ERROR(S))continue;
  S=Fs->OpenVolume(Fs,&Root);if(EFI_ERROR(S))continue;
  K=CheckFile(Root,L"\\\\kernel.macho",P9KernelBytes,P9KernelHash);
  A=CheckFile(Root,L"\\\\kernel.macho.adt",P9AdtBytes,P9AdtHash);
  Root->Close(Root);
  if(!EFI_ERROR(K)&&!EFI_ERROR(A))Print(L"P9 RAM filesystem inputs verified\\r\\n");
 }
 if(Handles)FreePool(Handles);
}
'''+text[end:]
 (p/name.replace('P7','P9')).write_text(text)
shutil.copy2(r/'config/p9/Expected.h',p/'P9Expected.h');shutil.copy2(r/'config/p9/Expected.h',s/'DarwinBootPkg/Driver/P9Expected.h')
base=s/'Platforms/Msm8998Pkg/Device/oneplus/cheeseburger'
for name in ['cheeseburger.dsc','cheeseburger.fdf']:
 q=base/name;t=q.read_text().replace('P8Integration/Probe/P8Probe.inf','P9Integration/Probe/P9Probe.inf');q.write_text(t)
q=s/'DarwinBootPkg/Driver/DarwinBoot.c';before=q.read_text();after=before.replace('P8','P9')
# Retain validation and real ExitBootServices, only replace the old color legend.
after=after.replace('Green=loader after EBS; magenta=XNU __start.', 'Starting XNU; persistent text console follows.')
q.write_text(after)
(r/'patches/p9/darwinboot-integration.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/DarwinBootPkg/Driver/DarwinBoot.c',tofile='b/DarwinBootPkg/Driver/DarwinBoot.c')))
q=s/'DarwinBootPkg/Driver/DarwinBoot.inf';q.write_text(q.read_text().replace('P8Expected.h','P9Expected.h'))
print('P9 integrated UEFI hashes updated; old 25 second checkpoint page removed')
