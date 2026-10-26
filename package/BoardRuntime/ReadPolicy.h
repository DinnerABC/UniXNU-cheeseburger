// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
// Same READ(10) field layout as the pinned EDK2 UfsPeimRead10. Reject rather
// than wrap a request outside its 32-bit LBA / 16-bit nonzero count domain.
static inline bool p11Read10(unsigned char cdb[16],unsigned long long block,unsigned count) {
  if(!count||count>65535||block>0xffffffffULL||count-1>0xffffffffULL-block)return false;
  for(unsigned i=0;i<16;i++)cdb[i]=0;
  cdb[0]=0x28;
  for(unsigned i=0;i<4;i++)cdb[2+i]=(unsigned char)(block>>(24-8*i));
  cdb[7]=(unsigned char)(count>>8);cdb[8]=(unsigned char)count;
  return true;
}
// The sole SCSI submission gate. Never accept outbound data or management
// operations (FORMAT, UNMAP, WRITE, MODE SELECT, SECURITY OUT, cache flush).
static inline bool p8ReadCommand(unsigned op, unsigned direction, unsigned bytes) {
  if (bytes > 131072 || (bytes & 3)) return false;
  switch (op) {
    case 0x00: return direction == 0 && bytes == 0; // TEST UNIT READY
    case 0x12: return direction == 2 && bytes == 96; // INQUIRY
    case 0x25: return direction == 2 && bytes == 8;  // READ CAPACITY(10)
    case 0x9e: return direction == 2 && bytes == 32; // READ CAPACITY(16), SA checked by builder
    case 0x28: case 0x88: return direction == 2 && bytes != 0; // READ
    case 0xa0: return direction == 2 && bytes == 256; // REPORT LUNS
    default: return false;
  }
}
static inline bool p8BlockRange(unsigned long long block,unsigned long long count,
                               unsigned long long last,unsigned blockSize) {
  return count && blockSize >= 512 && blockSize <= 4096 && !(blockSize & (blockSize-1)) &&
    block <= last && count-1 <= last-block;
}
