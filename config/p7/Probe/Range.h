/* Read bootloader-provided big-endian FDT addresses; never use a fixed load address. */
#ifndef P4_RANGE_H
#define P4_RANGE_H
#include <Library/FdtParserLib.h>
STATIC BOOLEAN P4FdtNumber(CONST VOID *F, INTN Node, CONST CHAR8 *Name, UINT64 *Value) {
  INT32 N; CONST UINT8 *P; UINTN I;
  P=fdt_getprop(F,Node,Name,&N); if(P==NULL || (N!=4 && N!=8))return FALSE;
  *Value=0; for(I=0;I<(UINTN)N;I++)*Value=(*Value<<8)|P[I]; return TRUE;
}
STATIC BOOLEAN P4InitrdRange(fdt *F, UINT64 *Start, UINT64 *End) {
  INTN Node; uint64_t Base,Size; INTN I;
  if(F==NULL){return FALSE;}
  Node=fdt_path_offset(F,"/chosen");
  if(Node<0 || !P4FdtNumber(F,Node,"linux,initrd-start",Start) ||
     !P4FdtNumber(F,Node,"linux,initrd-end",End))return FALSE;
  /* Bound by the actual device Recovery ramdisk size proven in P3. */
  if(*End<=*Start || *End-*Start>29522547 || *End>MAX_UINT64-4095)return FALSE;
  for(I=0;fdt_get_memory(F,I,&Base,&Size);I++)
    if(*Start>=Base && Size<=MAX_UINT64-Base && *End<=Base+Size)return TRUE;
  return FALSE;
}
#endif
