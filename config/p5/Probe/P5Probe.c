/* Read-only real EFI filesystem validation, no variable or partition writes. */
#include <PiDxe.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>
#include <Guid/EventGroup.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseCryptLib.h>
#include "Range.h"
#include "P5Expected.h"
STATIC EFI_STATUS CheckFile(EFI_FILE_PROTOCOL *Root,CHAR16 *Path,UINTN ExpectedSize,CONST UINT8 *ExpectedHash) {
 EFI_FILE_PROTOCOL *F=NULL;EFI_FILE_INFO *Info=NULL;VOID *Buffer=NULL;UINTN N=0;EFI_STATUS S;UINT8 Hash[32];
 S=Root->Open(Root,&F,Path,EFI_FILE_MODE_READ,0);if(EFI_ERROR(S))return S;
 S=F->GetInfo(F,&gEfiFileInfoGuid,&N,NULL);if(S!=EFI_BUFFER_TOO_SMALL || N>4096)goto Done;
 Info=AllocatePool(N);if(!Info){S=EFI_OUT_OF_RESOURCES;goto Done;}
 S=F->GetInfo(F,&gEfiFileInfoGuid,&N,Info);if(EFI_ERROR(S))goto Done;
 if(Info->FileSize!=ExpectedSize){S=EFI_BAD_BUFFER_SIZE;goto Done;}
 Buffer=AllocatePool(ExpectedSize);if(!Buffer){S=EFI_OUT_OF_RESOURCES;goto Done;}
 N=ExpectedSize;S=F->Read(F,&N,Buffer);if(EFI_ERROR(S))goto Done;
 if(N!=ExpectedSize || !Sha256HashAll(Buffer,N,Hash) || CompareMem(Hash,ExpectedHash,32)){S=EFI_SECURITY_VIOLATION;goto Done;}
 Print(L"P5 %s bytes=%lu SHA256 PASS\r\n",Path,(UINT64)N);S=EFI_SUCCESS;
Done:
 if(Buffer)FreePool(Buffer);
 if(Info)FreePool(Info);
 F->Close(F);return S;
}
STATIC VOID EFIAPI Ready(EFI_EVENT Event,VOID *Context){
 EFI_HANDLE *Handles=NULL;UINTN Count=0,I;EFI_STATUS S,K,A;EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;EFI_FILE_PROTOCOL *Root;UINT64 Begin=0,End=0;BOOLEAN Range;
 gBS->CloseEvent(Event);gST->ConOut->ClearScreen(gST->ConOut);
 Print(L"P5 STAGE0: UEFI live; readonly RAM payload proof\r\n");
 Range=P4InitrdRange(GetFdt(),&Begin,&End);Print(L"P5 initrd range: valid=%u base=%lx end=%lx\r\n",Range,Begin,End);
 S=gBS->LocateHandleBuffer(ByProtocol,&gEfiSimpleFileSystemProtocolGuid,NULL,&Count,&Handles);Print(L"P5 SimpleFS: %r count=%u\r\n",S,(UINT32)Count);
 if(!EFI_ERROR(S))for(I=0;I<Count;I++){
  S=gBS->HandleProtocol(Handles[I],&gEfiSimpleFileSystemProtocolGuid,(VOID**)&Fs);if(EFI_ERROR(S))continue;
  S=Fs->OpenVolume(Fs,&Root);if(EFI_ERROR(S))continue;
  K=CheckFile(Root,L"\\kernel.macho",P5KernelBytes,P5KernelHash);
  A=CheckFile(Root,L"\\kernel.macho.adt",P5AdtBytes,P5AdtHash);
  Print(L"P5 volume %u: kernel=%r ADT=%r\r\n",(UINT32)I,K,A);Root->Close(Root);
 }
 if(Handles)FreePool(Handles);
 Print(L"P5 Green stripe=loader after EBS ONLY\r\nP5 Magenta top stripe=actual XNU __start code\r\n");
 Print(L"P5 Capture this page. Continue in 25 seconds.\r\n");gBS->Stall(25000000);
}
EFI_STATUS EFIAPI P5ProbeEntry(EFI_HANDLE Image,EFI_SYSTEM_TABLE *System){EFI_EVENT Event;return EfiCreateEventReadyToBootEx(TPL_CALLBACK,Ready,NULL,&Event);}
