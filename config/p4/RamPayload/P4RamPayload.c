/* Project-only, read-only memory filesystem for a verified fastboot boot payload.
 * No Block I/O, partition access, file writes or persistent variables. */
#include <Uefi.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/DevicePath.h>
#include <Guid/FileInfo.h>
#include <Guid/FileSystemInfo.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/DevicePathLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/HobLib.h>
#include <Pi/PiHob.h>
#include "Range.h"
#pragma pack(1)
typedef struct {CHAR8 Magic[8];UINT64 Size;UINT32 Count,Crc;} HEADER;
typedef struct {CHAR8 Name[64];UINT64 Offset,Size;UINT32 Crc,Flags;} ENTRY;
#pragma pack()
#define LIMIT 8
STATIC UINT8 *mData;
STATIC UINT64 mSize;
STATIC ENTRY *mEntries;
STATIC UINT32 mCount;
typedef struct {EFI_FILE_PROTOCOL Proto;INTN Index;UINT64 Position;CHAR16 Path[128];} FILEOBJ;
STATIC EFI_SIMPLE_FILE_SYSTEM_PROTOCOL mFs;
STATIC EFI_GUID mGuid={0x970879cb,0x72f0,0x4876,{0xa3,0x65,0x4b,0xa3,0xe9,0xba,0x99,0xa0}};
STATIC struct {VENDOR_DEVICE_PATH Vendor;EFI_DEVICE_PATH_PROTOCOL End;} mPath;
STATIC EFI_STATUS EFIAPI Close(EFI_FILE_PROTOCOL *This){FreePool(This);return EFI_SUCCESS;}
STATIC EFI_STATUS EFIAPI Delete(EFI_FILE_PROTOCOL *This){Close(This);return EFI_WARN_DELETE_FAILURE;}
STATIC EFI_STATUS EFIAPI Write(EFI_FILE_PROTOCOL *This,UINTN *N,VOID *B){return EFI_WRITE_PROTECTED;}
STATIC EFI_STATUS EFIAPI SetInfo(EFI_FILE_PROTOCOL *This,EFI_GUID *G,UINTN N,VOID *B){return EFI_WRITE_PROTECTED;}
STATIC EFI_STATUS EFIAPI Flush(EFI_FILE_PROTOCOL *This){return EFI_SUCCESS;}
STATIC EFI_STATUS EFIAPI GetPosition(EFI_FILE_PROTOCOL *This,UINT64 *Position){
 FILEOBJ *F=(FILEOBJ*)This;if(!Position)return EFI_INVALID_PARAMETER;*Position=F->Position;return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI SetPosition(EFI_FILE_PROTOCOL *This,UINT64 Position){
 FILEOBJ *F=(FILEOBJ*)This;
 if(F->Index<0){if(Position)return EFI_UNSUPPORTED;F->Position=0;return EFI_SUCCESS;}
 if(Position==MAX_UINT64)Position=mEntries[F->Index].Size;
 if(Position>mEntries[F->Index].Size)return EFI_INVALID_PARAMETER;
 F->Position=Position;return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI GetInfo(EFI_FILE_PROTOCOL *This,EFI_GUID *Guid,UINTN *N,VOID *Buffer){
 FILEOBJ *F=(FILEOBJ*)This;UINTN Need;EFI_FILE_INFO *Info;EFI_FILE_SYSTEM_INFO *Fs;
 if(!Guid||!N)return EFI_INVALID_PARAMETER;
 if(CompareGuid(Guid,&gEfiFileInfoGuid)){
  Need=SIZE_OF_EFI_FILE_INFO+StrSize(F->Path);
  if(!Buffer||*N<Need){*N=Need;return EFI_BUFFER_TOO_SMALL;}
  ZeroMem(Buffer,Need);Info=Buffer;Info->Size=Need;
  Info->FileSize=F->Index<0?0:mEntries[F->Index].Size;Info->PhysicalSize=Info->FileSize;
  Info->Attribute=EFI_FILE_READ_ONLY|(F->Index<0?EFI_FILE_DIRECTORY:0);
  CopyMem(Info->FileName,F->Path,StrSize(F->Path));*N=Need;return EFI_SUCCESS;
 }
 if(CompareGuid(Guid,&gEfiFileSystemInfoGuid)){
  Need=SIZE_OF_EFI_FILE_SYSTEM_INFO+sizeof(L"P4 RAM payload");
  if(!Buffer||*N<Need){*N=Need;return EFI_BUFFER_TOO_SMALL;}
  ZeroMem(Buffer,Need);Fs=Buffer;Fs->Size=Need;Fs->ReadOnly=TRUE;Fs->VolumeSize=mSize;Fs->BlockSize=1;
  CopyMem(Fs->VolumeLabel,L"P4 RAM payload",sizeof(L"P4 RAM payload"));*N=Need;return EFI_SUCCESS;
 }
 return EFI_UNSUPPORTED;
}
STATIC EFI_STATUS EFIAPI Read(EFI_FILE_PROTOCOL *This,UINTN *N,VOID *Buffer){
 FILEOBJ *F=(FILEOBJ*)This;ENTRY *E;UINT64 Available;
 if(!N||(!Buffer&&*N))return EFI_INVALID_PARAMETER;
 /* No kit discovery in this volume; directory enumeration returns end-of-directory. */
 if(F->Index<0){*N=0;return EFI_SUCCESS;}
 E=&mEntries[F->Index];Available=E->Size-F->Position;if(*N>Available)*N=(UINTN)Available;
 if(*N){CopyMem(Buffer,mData+E->Offset+F->Position,*N);}
 F->Position+=*N;return EFI_SUCCESS;
}
STATIC EFI_STATUS NewFile(INTN Index,CONST CHAR16 *Path,EFI_FILE_PROTOCOL **Out);
STATIC EFI_STATUS EFIAPI Open(EFI_FILE_PROTOCOL *This,EFI_FILE_PROTOCOL **Out,CHAR16 *Name,UINT64 Mode,UINT64 Attr){
 CHAR16 Path[128];UINTN N=0,I,J;CHAR8 Ascii[128];FILEOBJ *F=(FILEOBJ*)This;BOOLEAN IsDir=FALSE;
 if(!Out||!Name)return EFI_INVALID_PARAMETER;
 if(Mode!=EFI_FILE_MODE_READ)return EFI_WRITE_PROTECTED;
 if(F->Index>=0)return EFI_UNSUPPORTED;
 if(Name[0]!=L'\\' && Name[0]!=L'/' && StrCmp(F->Path,L"\\")!=0){
  for(I=0;F->Path[I]&&N<126;I++){Path[N++]=F->Path[I];}
  Path[N++]=L'\\';
 }else Path[N++]=L'\\';
 for(I=0;Name[I];I++){
  CHAR16 C=Name[I]==L'/'?L'\\':Name[I];if(N==1&&C==L'\\')continue;
  if(N>=127||C>127){return EFI_INVALID_PARAMETER;}
  Path[N++]=C;
 }
 while(N>1&&Path[N-1]==L'\\'){N--;}
 Path[N]=0;
 if(StrCmp(Path,L"\\")==0)return NewFile(-1,Path,Out);
 for(I=0;I<=N;I++)Ascii[I]=(CHAR8)Path[I];
 for(I=0;I<mCount;I++){
  if(AsciiStriCmp(Ascii,mEntries[I].Name)==0)return NewFile((INTN)I,Path,Out);
  if(AsciiStrLen(mEntries[I].Name)>N && mEntries[I].Name[N]=='\\'){
   for(J=0;J<N;J++)if(AsciiCharToUpper(Ascii[J])!=AsciiCharToUpper(mEntries[I].Name[J]))break;
   if(J==N)IsDir=TRUE;
  }
 }
 return IsDir?NewFile(-1,Path,Out):EFI_NOT_FOUND;
}
STATIC EFI_STATUS NewFile(INTN Index,CONST CHAR16 *Path,EFI_FILE_PROTOCOL **Out){
 FILEOBJ *F=AllocateZeroPool(sizeof(*F));if(!F)return EFI_OUT_OF_RESOURCES;
 F->Proto.Revision=EFI_FILE_PROTOCOL_REVISION;F->Proto.Open=Open;F->Proto.Close=Close;F->Proto.Delete=Delete;
 F->Proto.Read=Read;F->Proto.Write=Write;F->Proto.GetPosition=GetPosition;F->Proto.SetPosition=SetPosition;
 F->Proto.GetInfo=GetInfo;F->Proto.SetInfo=SetInfo;F->Proto.Flush=Flush;F->Index=Index;
 StrCpyS(F->Path,128,Path);*Out=&F->Proto;return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI OpenVolume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This,EFI_FILE_PROTOCOL **Root){return NewFile(-1,L"\\",Root);}
EFI_STATUS EFIAPI P4RamPayloadEntry(EFI_HANDLE Image,EFI_SYSTEM_TABLE *System){
 UINT64 Start,End;HEADER *H;UINT32 Crc,I,J;EFI_STATUS Status;EFI_HANDLE Handle=NULL;BOOLEAN Reserved=FALSE;
 EFI_PEI_HOB_POINTERS Hob;UINT64 HB,HL;
 if(!P4InitrdRange(GetFdt(),&Start,&End))return EFI_NOT_FOUND;
 /* Require the matching PEI allocation HOB before touching the candidate payload. */
 Hob.Raw=GetHobList();while(!END_OF_HOB_LIST(Hob)){
  if(Hob.Header->HobType==EFI_HOB_TYPE_MEMORY_ALLOCATION){
   HB=Hob.MemoryAllocation->AllocDescriptor.MemoryBaseAddress;HL=Hob.MemoryAllocation->AllocDescriptor.MemoryLength;
   if(Hob.MemoryAllocation->AllocDescriptor.MemoryType==EfiReservedMemoryType && Start>=HB && End>=Start && End-HB<=HL)Reserved=TRUE;
  }Hob.Raw=GET_NEXT_HOB(Hob);
 }
 if(!Reserved)return EFI_ACCESS_DENIED;
 H=(HEADER*)(UINTN)Start;
 if(End-Start<sizeof(*H)||CompareMem(H->Magic,"P4BNDL01",8)!=0)return EFI_NOT_FOUND;
 if(H->Count==0||H->Count>LIMIT||H->Size!=End-Start||sizeof(*H)+H->Count*sizeof(ENTRY)>H->Size)return EFI_COMPROMISED_DATA;
 mData=(UINT8*)H;mSize=H->Size;mCount=H->Count;mEntries=(ENTRY*)(H+1);
 Status=gBS->CalculateCrc32(mEntries,mCount*sizeof(ENTRY),&Crc);if(EFI_ERROR(Status)||Crc!=H->Crc)return EFI_CRC_ERROR;
 for(I=0;I<mCount;I++){
  ENTRY *E=&mEntries[I];
  if(!ScanMem8(E->Name,64,0)||E->Name[0]!='\\'||E->Flags||E->Offset<sizeof(*H)+mCount*sizeof(ENTRY)||E->Offset>mSize||E->Size>mSize-E->Offset)return EFI_COMPROMISED_DATA;
  for(J=0;J<I;J++)if(AsciiStriCmp(E->Name,mEntries[J].Name)==0 || (E->Offset<mEntries[J].Offset+mEntries[J].Size && mEntries[J].Offset<E->Offset+E->Size))return EFI_COMPROMISED_DATA;
  Status=gBS->CalculateCrc32(mData+E->Offset,(UINTN)E->Size,&Crc);if(EFI_ERROR(Status)||Crc!=E->Crc)return EFI_CRC_ERROR;
 }
 mFs.Revision=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_REVISION;mFs.OpenVolume=OpenVolume;
 mPath.Vendor.Header.Type=HARDWARE_DEVICE_PATH;mPath.Vendor.Header.SubType=HW_VENDOR_DP;
 SetDevicePathNodeLength(&mPath.Vendor.Header,sizeof(VENDOR_DEVICE_PATH));mPath.Vendor.Guid=mGuid;
 mPath.End.Type=END_DEVICE_PATH_TYPE;mPath.End.SubType=END_ENTIRE_DEVICE_PATH_SUBTYPE;
 SetDevicePathNodeLength(&mPath.End,sizeof(EFI_DEVICE_PATH_PROTOCOL));
 Status=gBS->InstallMultipleProtocolInterfaces(&Handle,&gEfiSimpleFileSystemProtocolGuid,&mFs,&gEfiDevicePathProtocolGuid,&mPath,NULL);
 Print(L"P4 RAM payload: %r files=%u bytes=%lu CRC verified; read-only\r\n",Status,mCount,mSize);
 return Status;
}
