#!/usr/bin/env python3
from pathlib import Path
import shutil,re,difflib
r=Path(__file__).resolve().parents[1];s=Path('/home/dinnerabc.guest/unixnu-p8/source');assert (s/'.p8-overlay-marker').exists()
p=s/'P8Integration/Probe';p.mkdir(parents=True,exist_ok=True)
for name in ['Range.h','P7Probe.inf','P7Probe.c']:
 text=(r/'config/p7/Probe'/name).read_text().replace('P7','P8')
 if name.endswith('.c'):
  start=text.index('STATIC VOID EFIAPI Ready(');end=text.index('EFI_STATUS EFIAPI P8ProbeEntry(',start)
  text=text.replace('#include "Range.h"','// P8 omits the old register-range diagnostic helpers')
  start=text.index('STATIC VOID EFIAPI Ready(');end=text.index('EFI_STATUS EFIAPI P8ProbeEntry(',start)
  text=text[:start]+'''STATIC VOID EFIAPI Ready(EFI_EVENT Event,VOID *Context){
 EFI_HANDLE *Handles=NULL;UINTN Count=0,I;EFI_STATUS S,K,A;EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;EFI_FILE_PROTOCOL *Root;
 gBS->CloseEvent(Event);
 Print(L"P8 integrated read-only payload; next output is XNU text console\\r\\n");
 S=gBS->LocateHandleBuffer(ByProtocol,&gEfiSimpleFileSystemProtocolGuid,NULL,&Count,&Handles);
 if(!EFI_ERROR(S))for(I=0;I<Count;I++){
  S=gBS->HandleProtocol(Handles[I],&gEfiSimpleFileSystemProtocolGuid,(VOID**)&Fs);if(EFI_ERROR(S))continue;
  S=Fs->OpenVolume(Fs,&Root);if(EFI_ERROR(S))continue;
  K=CheckFile(Root,L"\\\\kernel.macho",P8KernelBytes,P8KernelHash);
  A=CheckFile(Root,L"\\\\kernel.macho.adt",P8AdtBytes,P8AdtHash);
  Root->Close(Root);
  if(!EFI_ERROR(K)&&!EFI_ERROR(A))Print(L"P8 RAM filesystem inputs verified\\r\\n");
 }
 if(Handles)FreePool(Handles);
}
'''+text[end:]
 (p/name.replace('P7','P8')).write_text(text)
shutil.copy2(r/'config/p8/Expected.h',p/'P8Expected.h');shutil.copy2(r/'config/p8/Expected.h',s/'DarwinBootPkg/Driver/P8Expected.h')
base=s/'Platforms/Msm8998Pkg/Device/oneplus/cheeseburger'
for name in ['cheeseburger.dsc','cheeseburger.fdf']:
 q=base/name;t=q.read_text().replace('P7Integration/Probe/P7Probe.inf','P8Integration/Probe/P8Probe.inf');q.write_text(t)
q=s/'DarwinBootPkg/Driver/DarwinBoot.c';before=q.read_text();after=before.replace('P7','P8')
# Retain validation and real ExitBootServices, only replace the old color legend.
after=after.replace('Green=loader after EBS; magenta=XNU __start.', 'Starting XNU; persistent text console follows.')
q.write_text(after)
(r/'patches/p8/darwinboot-integration.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/DarwinBootPkg/Driver/DarwinBoot.c',tofile='b/DarwinBootPkg/Driver/DarwinBoot.c')))
q=s/'DarwinBootPkg/Driver/DarwinBoot.inf';q.write_text(q.read_text().replace('P7Expected.h','P8Expected.h'))
print('P8 integrated UEFI hashes updated; old 25 second checkpoint page removed')
