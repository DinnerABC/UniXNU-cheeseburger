#!/usr/bin/env python3
"""Execute the actual read-only EFI file callbacks on host, with allocator/base-lib shims."""
from pathlib import Path
import subprocess,json
r=Path(__file__).resolve().parents[1];src=(r/'config/p4/RamPayload/P4RamPayload.c').read_text();code=src[:src.index('EFI_STATUS EFIAPI P4RamPayloadEntry')].replace('#include "Range.h"','').replace('#include <Library/HobLib.h>','').replace('#include <Pi/PiHob.h>','')
o=r/'out/p4/ramfs-tests';o.mkdir(parents=True,exist_ok=True)
prefix='''#include <stdlib.h>
#include <string.h>
#include <stdio.h>
'''
shim=r'''
EFI_BOOT_SERVICES *gBS;
EFI_GUID gEfiFileInfoGuid=EFI_FILE_INFO_ID;
EFI_GUID gEfiFileSystemInfoGuid=EFI_FILE_SYSTEM_INFO_ID;
VOID *EFIAPI AllocateZeroPool(UINTN N){return calloc(1,N);}
VOID EFIAPI FreePool(VOID *P){free(P);}
VOID *EFIAPI CopyMem(VOID *D,CONST VOID *S,UINTN N){return memcpy(D,S,N);}
VOID *EFIAPI ZeroMem(VOID *D,UINTN N){return memset(D,0,N);}
INTN EFIAPI CompareMem(CONST VOID *A,CONST VOID *B,UINTN N){return memcmp(A,B,N);}
BOOLEAN EFIAPI CompareGuid(CONST GUID *A,CONST GUID *B){return memcmp(A,B,16)==0;}
UINTN EFIAPI StrLen(CONST CHAR16 *P){UINTN N=0;while(P[N])N++;return N;}
UINTN EFIAPI StrSize(CONST CHAR16 *P){return (StrLen(P)+1)*2;}
INTN EFIAPI StrCmp(CONST CHAR16 *A,CONST CHAR16 *B){while(*A&&*A==*B){A++;B++;}return *A-*B;}
RETURN_STATUS EFIAPI StrCpyS(CHAR16 *D,UINTN N,CONST CHAR16 *S){if(StrLen(S)>=N)return RETURN_BUFFER_TOO_SMALL;CopyMem(D,S,StrSize(S));return RETURN_SUCCESS;}
CHAR8 EFIAPI AsciiCharToUpper(CHAR8 C){return C>='a'&&C<='z'?C-32:C;}
UINTN EFIAPI AsciiStrLen(CONST CHAR8 *P){return strlen(P);}
INTN EFIAPI AsciiStriCmp(CONST CHAR8 *A,CONST CHAR8 *B){while(*A&&AsciiCharToUpper(*A)==AsciiCharToUpper(*B)){A++;B++;}return AsciiCharToUpper(*A)-AsciiCharToUpper(*B);}
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d\n",__LINE__);return 1;}}while(0)
int main(void){
 UINT8 Data[4096]={0};ENTRY Entries[2]={0};EFI_FILE_PROTOCOL *Root,*File,*Dir;UINTN N;UINT64 Position;UINT8 Buffer[256];EFI_STATUS S;
 strcpy(Entries[0].Name,"\\kernel.macho");Entries[0].Offset=100;Entries[0].Size=6;memcpy(Data+100,"kernel",6);
 strcpy(Entries[1].Name,"\\EFI\\UniXNU\\unixnu.toml");Entries[1].Offset=200;Entries[1].Size=3;memcpy(Data+200,"cfg",3);
 mData=Data;mSize=sizeof(Data);mCount=2;mEntries=Entries;
 CHECK(OpenVolume(&mFs,&Root)==EFI_SUCCESS);
 CHECK(Root->Open(Root,&File,L"/KERNEL.MACHO",EFI_FILE_MODE_READ,0)==EFI_SUCCESS);
 N=99;CHECK(File->Read(File,&N,Buffer)==EFI_SUCCESS&&N==6&&!memcmp(Buffer,"kernel",6));
 CHECK(File->GetPosition(File,&Position)==EFI_SUCCESS&&Position==6);N=10;CHECK(File->Read(File,&N,Buffer)==EFI_SUCCESS&&N==0);
 CHECK(File->SetPosition(File,MAX_UINT64)==EFI_SUCCESS);CHECK(File->SetPosition(File,7)==EFI_INVALID_PARAMETER);
 CHECK(File->SetPosition(File,2)==EFI_SUCCESS);N=2;CHECK(File->Read(File,&N,Buffer)==EFI_SUCCESS&&N==2&&!memcmp(Buffer,"rn",2));
 N=0;CHECK(File->GetInfo(File,&gEfiFileInfoGuid,&N,NULL)==EFI_BUFFER_TOO_SMALL);
 CHECK(N<=sizeof(Buffer));S=File->GetInfo(File,&gEfiFileInfoGuid,&N,Buffer);CHECK(S==EFI_SUCCESS&&((EFI_FILE_INFO*)Buffer)->FileSize==6);
 N=1;CHECK(File->Write(File,&N,Buffer)==EFI_WRITE_PROTECTED);CHECK(File->Delete(File)==EFI_WARN_DELETE_FAILURE);
 CHECK(Root->Open(Root,&File,L"/kernel.macho",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0)==EFI_WRITE_PROTECTED);
 CHECK(Root->Open(Root,&Dir,L"/EFI/UniXNU",EFI_FILE_MODE_READ,0)==EFI_SUCCESS);
 CHECK(Dir->Open(Dir,&File,L"unixnu.toml",EFI_FILE_MODE_READ,0)==EFI_SUCCESS);N=4;CHECK(File->Read(File,&N,Buffer)==EFI_SUCCESS&&N==3&&!memcmp(Buffer,"cfg",3));File->Close(File);Dir->Close(Dir);
 CHECK(Root->Open(Root,&File,L"/missing",EFI_FILE_MODE_READ,0)==EFI_NOT_FOUND);Root->Close(Root);
 puts("Actual EFI filesystem callbacks PASS: paths, info, bounded read/EOF/seek, write/delete refusal");return 0;
}
'''
(o/'test.c').write_text(prefix+code+shim)
e=r.parent/'upstream/edk2-msm8998/Common/edk2';cmd=['xcrun','clang','-fshort-wchar','-Wno-incompatible-library-redeclaration','-I',str(e/'MdePkg/Include'),'-I',str(e/'MdePkg/Include/AArch64'),'-Wl,-dead_strip',str(o/'test.c'),'-o',str(o/'test')]
v=subprocess.run(cmd,capture_output=True,text=True);(o/'compile.log').write_text(v.stdout+v.stderr)
if v.returncode:raise SystemExit(v.returncode)
v=subprocess.run([str(o/'test')],capture_output=True,text=True);print(v.stdout,end='');(o/'result.json').write_text(json.dumps(dict(command=cmd,exit_code=v.returncode,output=v.stdout,scope='Host execution of actual protocol callbacks; not firmware runtime proof'),indent=2)+'\n');raise SystemExit(v.returncode)
