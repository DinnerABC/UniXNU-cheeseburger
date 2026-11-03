/* SPDX-License-Identifier: MIT */
#include "../ConstantUploads.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
static unsigned cases;
static void refuse(struct air_constant_layout *l, struct air_constant_binding *bindings,
                   uint8_t *dst, size_t bytes, unsigned status) {
    uint8_t before[256];
    memcpy(before, dst, 256);
    struct air_constant_result r = air_pack_constants(l, bindings, dst, bytes);
    assert(r.status == status && r.written_bytes == 0 && r.padded_bytes == 0);
    assert(!memcmp(before, dst, 256));
    cases++;
}
int main(void) {
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    uint8_t *guard =
        mmap(NULL, page * 4, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(guard != MAP_FAILED);
    assert(!mprotect(guard + page, page, PROT_NONE));
    assert(!mprotect(guard + page * 3, page, PROT_NONE));
    uint8_t *a = guard + page - 8, *b = guard + page * 3 - 80;
    for (unsigned i = 0; i < 8; i++)
        a[i] = (uint8_t)(0x10 + i);
    for (unsigned i = 0; i < 80; i++)
        b[i] = (uint8_t)(0x40 + i);
    uint8_t *dst = mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(dst != MAP_FAILED);
    memset(dst, 0x6b, 256);
    struct air_constant_binding bindings[16] = {{0}};
    bindings[3] = (struct air_constant_binding){a, 8, 8};
    bindings[7] = (struct air_constant_binding){b, 80, 80};
    struct air_constant_range ranges[] = {{3, 0, 64, 64}, {7, 64, 128, 128}};
    struct air_constant_read reads[] = {{3, 0, 8}, {7, 64, 16}};
    uint32_t imms[] = {0x11223344, 0x89abcdef, 0, 0xffffffff};
    struct air_constant_layout original = {256, 64, 64, 128, ranges, 2, reads, 2, 208, 4, imms},
                               l = original;
    struct air_constant_result r = air_pack_constants(&l, bindings, dst, 256);
    assert(!r.status && r.written_bytes == 144 && r.padded_bytes == 104);
    for (unsigned i = 0; i < 256; i++) {
        uint8_t expected = 0x6b;
        if (i >= 64 && i < 128)
            expected = i < 72 ? a[i - 64] : 0;
        if (i >= 128 && i < 192)
            expected = i < 144 ? b[i - 64] : 0;
        if (i >= 208 && i < 224)
            expected = (uint8_t)(imms[(i - 208) / 4] >> (((i - 208) % 4) * 8));
        assert(dst[i] == expected);
    }
    cases++;
    l = original;
    l.range_count = 0;
    l.read_count = 0;
    r = air_pack_constants(&l, bindings, dst, 256);
    assert(!r.status && r.written_bytes == 16);
    cases++;
    l = original;
    refuse(&l, bindings, dst, 255, AIR_CONSTANT_CAPACITY);
    l.constant_bytes = AIR_CONSTANT_MAX_BYTES + 16;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_ARGUMENT);
    l = original;
    l.upload_alignment = 0;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_LAYOUT);
    l = original;
    l.range_count = 33;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_ARGUMENT);
    l = original;
    l.read_count = 4097;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_ARGUMENT);
    l = original;
    l.ranges = NULL;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_ARGUMENT);
    l = original;
    l.reads = NULL;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_ARGUMENT);
    l = original;
    l.immediates = NULL;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_ARGUMENT);
    l = original;
    l.immediate_begin = 128;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_LAYOUT);
    l = original;
    l.immediate_begin = 209;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_LAYOUT);
    l = original;
    l.immediate_words = 3;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_LAYOUT);
    l = original;
    l.immediate_words = 4097;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_ARGUMENT);
    l = original;
    l.immediate_begin = 256;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_LAYOUT);
    l = original;
    l.ubo_begin = 256;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_LAYOUT);
    l = original;
    l.ubo_bytes = 127;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_LAYOUT);
    l = original;
    bindings[3].bytes = 7;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_BINDING);
    bindings[3].bytes = 8;
    bindings[3].data = NULL;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_BINDING);
    bindings[3].data = a;
    bindings[3].bytes = 4;
    bindings[3].minimum_bytes = 4;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_READ);
    bindings[3] = (struct air_constant_binding){a, 8, 8};
    ranges[0].binding = 16;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_LAYOUT);
    ranges[0].binding = 3;
    ranges[0].source_begin = 4;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_LAYOUT);
    ranges[0].source_begin = 0;
    ranges[0].source_end = 0;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_LAYOUT);
    ranges[0].source_end = 64;
    ranges[0].destination_begin = 0;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_LAYOUT);
    ranges[0].destination_begin = 64;
    ranges[1].destination_begin = 64;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_LAYOUT);
    ranges[1].destination_begin = 128;
    reads[0].binding = 16;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_READ);
    reads[0].binding = 3;
    reads[0].bytes = 0;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_READ);
    reads[0].bytes = 8;
    reads[0].offset = 1;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_READ);
    reads[0].offset = 0;
    l.read_count = 1;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_READ);
    l = original;
    bindings[3].data = dst + 64;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_ALIAS);
    bindings[3].data = a;
    l.immediates = (uint32_t *)(void *)(dst + 32);
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_ALIAS);
    l = original;
    bindings[3].data = (const uint8_t *)(UINTPTR_MAX - 3);
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_ALIAS);
    bindings[3].data = a;
    uint8_t before[256];
    memcpy(before, dst, 256);
    memcpy(dst, ranges, sizeof(ranges));
    l.ranges = (void *)dst;
    refuse(&l, bindings, dst, 256, AIR_CONSTANT_ALIAS);
    memcpy(dst, before, 256);
    l = original;
    assert(!munmap(guard, page * 4));
    assert(!munmap(dst, page));
    printf("PASS: %u guarded constant upload cases; short bindings end at inaccessible pages\n",
           cases);
    return 0;
}
