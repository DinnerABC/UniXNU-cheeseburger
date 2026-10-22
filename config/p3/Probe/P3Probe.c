/** Read-only, finite screen diagnostics; no filesystem or variable writes. */
#include <PiDxe.h>
#include <Guid/GlobalVariable.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/BlockIo.h>
#include <Protocol/SerialIo.h>
#include <Protocol/FirmwareVolume2.h>
#include <Protocol/UniXNULoader.h>

STATIC VOID Count(IN CHAR16 *Name, IN EFI_GUID *Guid) {
  EFI_STATUS Status;
  EFI_HANDLE *Handles = NULL;
  UINTN Number = 0;
  Status = gBS->LocateHandleBuffer(ByProtocol, Guid, NULL, &Number, &Handles);
  Print(L"P3 %s: %r handles=%u\r\n", Name, Status, (UINT32)Number);
  if (Handles != NULL) { FreePool(Handles); }
}
STATIC VOID EFIAPI Report(IN EFI_EVENT Event, IN VOID *Context) {
  EFI_STATUS Status;
  UINT16 Current = 0;
  UINTN Size = sizeof(Current);
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  UINTN El;
  (VOID)Context;
  gBS->CloseEvent(Event);
  gBS->SetWatchdogTimer(0, 0, 0, NULL);
  gST->ConOut->ClearScreen(gST->ConOut);
  Print(L"P3 READ-ONLY RUNTIME PROBE v1\r\n");
  __asm__ __volatile__("mrs %0, CurrentEL" : "=r" (El));
  Print(L"P3 CurrentEL=%u\r\n", (UINT32)((El >> 2) & 3));
  Count(L"GOP", &gEfiGraphicsOutputProtocolGuid);
  Count(L"TextInput", &gEfiSimpleTextInProtocolGuid);
  Count(L"SimpleFS", &gEfiSimpleFileSystemProtocolGuid);
  Count(L"BlockIO", &gEfiBlockIoProtocolGuid);
  Count(L"SerialIO", &gEfiSerialIoProtocolGuid);
  Count(L"FV2", &gEfiFirmwareVolume2ProtocolGuid);
  Count(L"DarwinBoot Loader", &gUniXNULoaderProtocolGuid);
  Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  if (!EFI_ERROR(Status) && Gop->Mode != NULL && Gop->Mode->Info != NULL) {
    Print(L"P3 GOP %ux%u format=%u stride=%u\r\n",
      Gop->Mode->Info->HorizontalResolution, Gop->Mode->Info->VerticalResolution,
      (UINT32)Gop->Mode->Info->PixelFormat, Gop->Mode->Info->PixelsPerScanLine);
    Print(L"P3 FB base=0x%lx bytes=0x%lx\r\n", Gop->Mode->FrameBufferBase,
      (UINT64)Gop->Mode->FrameBufferSize);
  }
  Status = gRT->GetVariable(L"BootCurrent", &gEfiGlobalVariableGuid, NULL, &Size, &Current);
  Print(L"P3 BootCurrent: %r value=%04x\r\n", Status, Current);
  Print(L"P3 Capture this screen. Continuing in 25 seconds.\r\n");
  gBS->Stall(25000000);
}
EFI_STATUS EFIAPI P3ProbeEntry(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {
  EFI_EVENT Event;
  (VOID)ImageHandle;
  (VOID)SystemTable;
  return EfiCreateEventReadyToBootEx(TPL_CALLBACK, Report, NULL, &Event);
}
