/* P13 lossless transport only. P12 payload identity remains pinned. */
#include <Library/BaseCryptLib.h>
RETURN_STATUS EFIAPI LzmaUefiDecompressGetInfo(CONST VOID *,UINT32,UINT32 *,UINT32 *);
RETURN_STATUS EFIAPI LzmaUefiDecompress(CONST VOID *,UINTN,VOID *,VOID *);
#pragma pack(1)
typedef struct {UINT8 Magic[8];UINT64 Compressed,Expanded;UINT8 Digest[32];} P13_CONTAINER;
#pragma pack()
STATIC EFI_STATUS P13Expand(UINT64 Start,UINT64 End,VOID **Data,UINT64 *Size){
 STATIC CONST UINT8 Expected[32]={0x21,0xaf,0x3f,0x6e,0xb4,0x18,0xc3,0x10,0x3a,0xb4,0xee,0xd4,0x73,0xcf,0x43,0xe1,0x48,0x98,0x85,0x68,0xe5,0x17,0x2f,0xd6,0x9c,0x64,0xb3,0xa3,0xdf,0x04,0x91,0x94};
 P13_CONTAINER *C=(VOID*)(UINTN)Start;UINT32 Output,ScratchSize;VOID *Buffer,*Scratch;UINT8 Digest[32];RETURN_STATUS R;
 if(End<Start)return EFI_COMPROMISED_DATA;
 *Data=(VOID*)(UINTN)Start;*Size=End-Start;
 if(*Size<8||CompareMem(C->Magic,"P13LZ001",8)!=0)return EFI_SUCCESS;
 if(*Size<sizeof(*C)+13||C->Compressed!=*Size-sizeof(*C)||C->Compressed>MAX_UINT32||C->Expanded!=124993536||CompareMem(C->Digest,Expected,32)!=0)return EFI_COMPROMISED_DATA;
 R=LzmaUefiDecompressGetInfo(C+1,(UINT32)C->Compressed,&Output,&ScratchSize);
 if(RETURN_ERROR(R)||Output!=C->Expanded||ScratchSize>SIZE_1MB)return EFI_COMPROMISED_DATA;
 Buffer=AllocatePool(Output);Scratch=AllocatePool(ScratchSize);
 if(!Buffer||!Scratch){if(Buffer)FreePool(Buffer);if(Scratch)FreePool(Scratch);return EFI_OUT_OF_RESOURCES;}
 Print(L"P13: decompressing fixed P12 payload %lu -> %u bytes\r\n",C->Compressed,Output);
 R=LzmaUefiDecompress(C+1,(UINTN)C->Compressed,Buffer,Scratch);FreePool(Scratch);
 if(RETURN_ERROR(R)||!Sha256HashAll(Buffer,Output,Digest)||CompareMem(Digest,Expected,32)!=0){FreePool(Buffer);Print(L"P13: decompression/hash FAILED; no filesystem published\r\n");return EFI_SECURITY_VIOLATION;}
 *Data=Buffer;*Size=Output;
 Print(L"P13: expanded P12 payload SHA256 PASS\r\n");return EFI_SUCCESS;
}
