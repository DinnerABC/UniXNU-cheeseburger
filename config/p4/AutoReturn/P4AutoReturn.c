/** Bounded UEFI-only test recovery. Ring is volatile, never claimed persistent. */
#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#define P4_RECORDS 32
#define P4_SECONDS 90
STATIC EFI_GUID mLogGuid = {0xFCFF91F2,0xB67A,0x4B9C,{0xB3,0xD4,0x5A,0xAE,0x62,0xD2,0x58,0x51}};
typedef struct { UINT64 Sequence; UINT64 Counter; UINT32 Stage; UINT32 Crc; } P4_RECORD;
typedef struct { UINT32 Magic; UINT16 Version; UINT16 Capacity; UINT64 Next; UINT64 Frequency; P4_RECORD Records[P4_RECORDS]; } P4_RING;
STATIC P4_RING *mRing;
STATIC EFI_EVENT mTimer;
STATIC BOOLEAN mExited;
STATIC UINT32 Crc(IN CONST UINT8 *Data, IN UINTN Size) {
  UINT32 Value = 0xFFFFFFFFU; UINTN I; UINTN Bit;
  for (I=0;I<Size;I++) { Value ^= Data[I]; for (Bit=0;Bit<8;Bit++) { Value=(Value>>1)^((Value&1)?0xEDB88320U:0); } }
  return ~Value;
}
STATIC VOID LogStage(IN UINT32 Stage) {
  P4_RECORD *Record; UINT64 Counter;
  if (mRing == NULL) { return; }
  __asm__ __volatile__("mrs %0, cntpct_el0" : "=r" (Counter));
  Record=&mRing->Records[mRing->Next % P4_RECORDS];
  Record->Sequence=mRing->Next++; Record->Counter=Counter; Record->Stage=Stage;
  Record->Crc=Crc((UINT8 *)Record, 20);
}
STATIC VOID EFIAPI Exited(IN EFI_EVENT Event, IN VOID *Context) {
  (VOID)Event; (VOID)Context; mExited=TRUE; LogStage(4);
}
STATIC VOID EFIAPI Reset(IN EFI_EVENT Event, IN VOID *Context) {
  (VOID)Event; (VOID)Context;
  if (mExited) { return; }
  LogStage(3);
  Print(L"P4 AUTORETURN: UEFI observation completed; cold reset to installed OS\r\n");
  gRT->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
}
STATIC VOID EFIAPI Ready(IN EFI_EVENT Event, IN VOID *Context) {
  EFI_STATUS Status; UINT64 Mpidr; UINT64 Midr; (VOID)Context; gBS->CloseEvent(Event); LogStage(2);
  __asm__ __volatile__("mrs %0, mpidr_el1" : "=r" (Mpidr));
  __asm__ __volatile__("mrs %0, midr_el1" : "=r" (Midr));
  Print(L"P4 MPIDR=0x%lx MIDR=0x%lx CNTFRQ=%lu\r\n", Mpidr, Midr, mRing->Frequency);
  Print(L"P4 AUTORETURN: armed for %u s; volatile ring only\r\n", P4_SECONDS);
  Status=gBS->SetTimer(mTimer, TimerRelative, (UINT64)P4_SECONDS*10000000);
  Print(L"P4 AUTORETURN timer: %r\r\n", Status);
}
EFI_STATUS EFIAPI P4AutoReturnEntry(IN EFI_HANDLE Image, IN EFI_SYSTEM_TABLE *SystemTable) {
  EFI_STATUS Status; EFI_EVENT ReadyEvent; EFI_EVENT ExitEvent;
  (VOID)Image; (VOID)SystemTable;
  mRing=AllocateZeroPool(sizeof(*mRing)); if (mRing==NULL) { return EFI_OUT_OF_RESOURCES; }
  __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r" (mRing->Frequency));
  mRing->Magic=0x474C3450; mRing->Version=1; mRing->Capacity=P4_RECORDS; LogStage(1);
  Status=gBS->InstallConfigurationTable(&mLogGuid,mRing); if (EFI_ERROR(Status)) { return Status; }
  Status=gBS->CreateEvent(EVT_TIMER|EVT_NOTIFY_SIGNAL,TPL_CALLBACK,Reset,NULL,&mTimer);
  if (EFI_ERROR(Status)) { return Status; }
  Status=gBS->CreateEventEx(EVT_NOTIFY_SIGNAL,TPL_NOTIFY,Exited,NULL,&gEfiEventExitBootServicesGuid,&ExitEvent);
  if (EFI_ERROR(Status)) { return Status; }
  return EfiCreateEventReadyToBootEx(TPL_CALLBACK,Ready,NULL,&ReadyEvent);
}
