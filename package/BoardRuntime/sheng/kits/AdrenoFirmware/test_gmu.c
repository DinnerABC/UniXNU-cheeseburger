/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "AdrenoFirmware.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const AdrenoFirmwareRegion regions[] = {
  {0, 0x4000, 1}, {0x4000, 0xffc000, 2}, {0x10004000, 0x4000, 3}
};

static void refusal(const uint8_t *data, size_t bytes,
                    const AdrenoFirmwareRegion *ranges, size_t range_count,
                    size_t capacity, AdrenoFirmwareResult expected)
{
  AdrenoFirmwareBlock blocks[32], before[32];
  memset(blocks, 0xa5, sizeof(blocks));
  memcpy(before, blocks, sizeof(blocks));
  size_t count = 123;
  AdrenoFirmwareResult result = AdrenoFirmwarePlanGmu(data, bytes, ranges, range_count, blocks, capacity, &count);
  assert(result != AdrenoFirmwareSuccess);
  if (expected != AdrenoFirmwareSuccess) assert(result == expected);
  assert(count == 0 && !memcmp(blocks, before, sizeof(blocks)));
}

int main(int argc, char **argv)
{
  assert(argc == 2);
  FILE *file = fopen(argv[1], "rb");
  assert(file && !fseek(file, 0, SEEK_END));
  long length = ftell(file);
  assert(length == 66844 && !fseek(file, 0, SEEK_SET));
  uint8_t *data = malloc((size_t)length);
  assert(data && fread(data, 1, (size_t)length, file) == (size_t)length);
  fclose(file);
  AdrenoFirmwareBlock blocks[32];
  size_t count = 0;
  assert(AdrenoFirmwarePlanGmu(data, length, regions, 3, blocks, 32, &count) == AdrenoFirmwareSuccess && count == 15);
  unsigned payloads = 0;
  size_t payload_bytes = 0;
  for (size_t i = 0; i < count; ++i) {
    payloads += blocks[i].bytes != 0;
    payload_bytes += blocks[i].bytes;
    if (!blocks[i].bytes) assert(blocks[i].region_identifier == UINT32_MAX);
  }
  assert(payloads == 4 && payload_bytes == 66604);
  assert(blocks[11].address == 0 && blocks[11].bytes == 0x1dc && blocks[11].region_identifier == 1);
  assert(blocks[12].address == 0x4000 && blocks[12].bytes == 0x10210 && blocks[12].region_identifier == 2);
  assert(blocks[13].address == 0x10007f98 && blocks[13].bytes == 0x28 && blocks[13].region_identifier == 3);
  assert(blocks[14].address == 0x10007fdc && blocks[14].bytes == 0x18 && blocks[14].region_identifier == 3);
  unsigned valid_prefixes = 0;
  for (size_t bytes = 0; bytes < (size_t)length; ++bytes) {
    count = 0;
    AdrenoFirmwareResult result = AdrenoFirmwarePlanGmu(data, bytes, regions, 3, blocks, 32, &count);
    if (result == AdrenoFirmwareSuccess) valid_prefixes++;
    else refusal(data, bytes, regions, 3, 32, AdrenoFirmwareSuccess);
  }
  /* Ending on a complete record is structurally valid, not authentic firmware. */
  assert(valid_prefixes == 14);
  refusal(data, length, regions, 3, 14, AdrenoFirmwareNoSpace);
  refusal(data, length, regions, 2, 32, AdrenoFirmwareUnmapped);
  AdrenoFirmwareRegion bad[3];
  memcpy(bad, regions, sizeof(bad)); bad[1].address = 0x3ffc;
  refusal(data, length, bad, 3, 32, AdrenoFirmwareInvalid);
  memcpy(bad, regions, sizeof(bad)); bad[1].identifier = 1;
  refusal(data, length, bad, 3, 32, AdrenoFirmwareInvalid);
  memcpy(bad, regions, sizeof(bad)); bad[2].bytes -= 0x10;
  refusal(data, length, bad, 3, 32, AdrenoFirmwareUnmapped);
  data[0xb4] = 0xff; data[0xb5] = 0xff; data[0xb6] = 0xff; data[0xb7] = 0xff;
  refusal(data, length, regions, 3, 32, AdrenoFirmwareTruncated);
  free(data);
  puts("PASS: actual 15-record GMU plan, all 66844 prefixes and six explicit refusal controls; no partial output");
}
