/* P13 lossless transport only. P12 payload identity remains pinned. */
#include <Library/BaseCryptLib.h>
RETURN_STATUS EFIAPI LzmaUefiDecompressGetInfo(CONST VOID *,UINT32,UINT32 *,UINT32 *);
RETURN_STATUS EFIAPI LzmaUefiDecompress(CONST VOID *,UINTN,VOID *,VOID *);
#pragma pack(1)
typedef struct {UINT8 Magic[8];UINT64 Compressed,Expanded;UINT8 Digest[32];} P13_CONTAINER;
#pragma pack()
STATIC EFI_STATUS P13Expand(UINT64 Start,UINT64 End,VOID **Data,UINT64 *Size){
 STATIC CONST UINT8 Expected[32]={0xea,0x32,0x40,0xc9,0x39,0x4e,0xc3,0xca,0x81,0x2c,0x6e,0xc1,0xde,0xd3,0x62,0x28,0x49,0x8c,0xa5,0xeb,0xd9,0x16,0x02,0x3c,0xce,0x44,0xbc,0x29,0xe7,0xb0,0x16,0xe4};
 P13_CONTAINER *C=(VOID*)(UINTN)Start;UINT32 Output,ScratchSize;VOID *Buffer,*Scratch;UINT8 Digest[32];RETURN_STATUS R;
 if(End<Start)return EFI_COMPROMISED_DATA;
 *Data=(VOID*)(UINTN)Start;*Size=End-Start;
 if(*Size<8||CompareMem(C->Magic,"P13LZ001",8)!=0)return EFI_SUCCESS;
 if(*Size<sizeof(*C)+13||C->Compressed!=*Size-sizeof(*C)||C->Compressed>MAX_UINT32||C->Expanded!=124993536||CompareMem(C->Digest,Expected,32)!=0)return EFI_COMPROMISED_DATA;
 R=LzmaUefiDecompressGetInfo(C+1,(UINT32)C->Compressed,&Output,&ScratchSize);
 if(RETURN_ERROR(R)||Output!=C->Expanded||ScratchSize>SIZE_1MB)return EFI_COMPROMISED_DATA;
 Buffer=AllocatePool(Output);Scratch=AllocatePool(ScratchSize);
 if(!Buffer||!Scratch){if(Buffer)FreePool(Buffer);if(Scratch)FreePool(Scratch);return EFI_OUT_OF_RESOURCES;}
 Print(L"P13: decompressing fixed P14 payload %lu -> %u bytes\r\n",C->Compressed,Output);
 R=LzmaUefiDecompress(C+1,(UINTN)C->Compressed,Buffer,Scratch);FreePool(Scratch);
 if(RETURN_ERROR(R)||!Sha256HashAll(Buffer,Output,Digest)||CompareMem(Digest,Expected,32)!=0){FreePool(Buffer);Print(L"P13: decompression/hash FAILED; no filesystem published\r\n");return EFI_SECURITY_VIOLATION;}
 *Data=Buffer;*Size=Output;
 Print(L"P13: expanded P14 payload SHA256 PASS\r\n");return EFI_SUCCESS;
}
