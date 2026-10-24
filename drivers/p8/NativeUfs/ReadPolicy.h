// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
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
