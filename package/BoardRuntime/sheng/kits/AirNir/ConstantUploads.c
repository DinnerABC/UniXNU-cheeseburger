/* SPDX-License-Identifier: MIT */
#include "ConstantUploads.h"

static int inside(size_t begin, size_t bytes, size_t limit) {
    return begin <= limit && bytes <= limit - begin;
}
static int overlap(uintptr_t a, size_t an, uintptr_t b, size_t bn) {
    if (!an || !bn)
        return 0;
    if (an > UINTPTR_MAX - a || bn > UINTPTR_MAX - b)
        return 1;
    return a < b + bn && b < a + an;
}
static struct air_constant_result failure(uint32_t status) {
    return (struct air_constant_result){status, 0, 0};
}
struct air_constant_result
air_pack_constants(const struct air_constant_layout *l,
                   const struct air_constant_binding bindings[AIR_CONSTANT_MAX_BINDINGS],
                   uint8_t *dst, size_t dst_bytes) {
    if (!l || !bindings || !dst)
        return failure(AIR_CONSTANT_ARGUMENT);
    if (!l->constant_bytes || l->constant_bytes > AIR_CONSTANT_MAX_BYTES ||
        l->constant_bytes % 16 || l->range_count > AIR_CONSTANT_MAX_RANGES ||
        l->read_count > AIR_CONSTANT_MAX_READS || (l->range_count && !l->ranges) ||
        (l->read_count && !l->reads) || l->immediate_words > AIR_CONSTANT_MAX_BYTES / 4 ||
        (l->immediate_words && !l->immediates))
        return failure(AIR_CONSTANT_ARGUMENT);
    if (dst_bytes < l->constant_bytes)
        return failure(AIR_CONSTANT_CAPACITY);
    uint32_t align = l->upload_alignment;
    if (align < 16 || align > 256 || (align & (align - 1)) ||
        !inside(l->ubo_begin, l->ubo_bytes, l->constant_bytes) || l->ubo_begin % 16 ||
        l->ubo_bytes % 16 ||
        !inside(l->immediate_begin, (size_t)l->immediate_words * 4, l->constant_bytes) ||
        (l->immediate_words && (l->immediate_begin % 16 || l->immediate_words % 4)))
        return failure(AIR_CONSTANT_LAYOUT);
    if (overlap(l->ubo_begin, l->ubo_bytes, l->immediate_begin, (size_t)l->immediate_words * 4))
        return failure(AIR_CONSTANT_LAYOUT);
    /* Metadata cannot change underneath a copy because it aliases the output. */
    uintptr_t output = (uintptr_t)dst;
    if (overlap(output, l->constant_bytes, (uintptr_t)l, sizeof(*l)) ||
        overlap(output, l->constant_bytes, (uintptr_t)bindings,
                sizeof(*bindings) * AIR_CONSTANT_MAX_BINDINGS) ||
        overlap(output, l->constant_bytes, (uintptr_t)l->ranges,
                l->range_count * sizeof(*l->ranges)) ||
        overlap(output, l->constant_bytes, (uintptr_t)l->reads,
                l->read_count * sizeof(*l->reads)) ||
        overlap(output, l->constant_bytes, (uintptr_t)l->immediates,
                (size_t)l->immediate_words * 4))
        return failure(AIR_CONSTANT_ALIAS);
    uint32_t written = l->immediate_words * 4, padded = 0;
    for (size_t i = 0; i < l->range_count; i++) {
        const struct air_constant_range *r = &l->ranges[i];
        if (r->binding >= AIR_CONSTANT_MAX_BINDINGS || r->source_begin >= r->source_end ||
            r->source_begin % align || r->source_end % align || r->destination_begin % align)
            return failure(AIR_CONSTANT_LAYOUT);
        uint32_t bytes = r->source_end - r->source_begin;
        if (r->destination_begin < l->ubo_begin ||
            !inside(r->destination_begin - l->ubo_begin, bytes, l->ubo_bytes))
            return failure(AIR_CONSTANT_LAYOUT);
        if (overlap(r->destination_begin, bytes, l->immediate_begin,
                    (size_t)l->immediate_words * 4))
            return failure(AIR_CONSTANT_LAYOUT);
        for (size_t j = 0; j < i; j++) {
            const struct air_constant_range *p = &l->ranges[j];
            if (overlap(r->destination_begin, bytes, p->destination_begin,
                        p->source_end - p->source_begin))
                return failure(AIR_CONSTANT_LAYOUT);
        }
        const struct air_constant_binding *b = &bindings[r->binding];
        if (!b->data || b->bytes < b->minimum_bytes || !b->minimum_bytes ||
            overlap(output, l->constant_bytes, (uintptr_t)b->data, b->bytes))
            return failure(b->data && b->bytes >= b->minimum_bytes && b->minimum_bytes
                               ? AIR_CONSTANT_ALIAS
                               : AIR_CONSTANT_BINDING);
        int used = 0;
        for (size_t j = 0; j < l->read_count; j++) {
            const struct air_constant_read *read = &l->reads[j];
            if (read->binding == r->binding && read->offset >= r->source_begin &&
                inside(read->offset - r->source_begin, read->bytes, bytes))
                used = 1;
        }
        if (!used)
            return failure(AIR_CONSTANT_READ);
        size_t available = r->source_begin < b->bytes ? b->bytes - r->source_begin : 0;
        if (available > bytes)
            available = bytes;
        padded += bytes - (uint32_t)available;
        written += bytes;
    }
    for (size_t i = 0; i < l->read_count; i++) {
        const struct air_constant_read *read = &l->reads[i];
        if (read->binding >= AIR_CONSTANT_MAX_BINDINGS || !read->bytes || read->bytes % 4 ||
            read->offset % 4)
            return failure(AIR_CONSTANT_READ);
        const struct air_constant_binding *b = &bindings[read->binding];
        if (!b->data || b->bytes < b->minimum_bytes || !inside(read->offset, read->bytes, b->bytes))
            return failure(AIR_CONSTANT_READ);
        int covered = 0;
        for (size_t j = 0; j < l->range_count; j++) {
            const struct air_constant_range *r = &l->ranges[j];
            if (r->binding == read->binding && read->offset >= r->source_begin &&
                inside(read->offset - r->source_begin, read->bytes,
                       r->source_end - r->source_begin))
                covered = 1;
        }
        if (!covered)
            return failure(AIR_CONSTANT_READ);
    }
    /* No condition below can fail. Padding is transport alignment only, and
     * no recorded shader read may reference a byte that would be padded. */
    for (size_t i = 0; i < l->range_count; i++) {
        const struct air_constant_range *r = &l->ranges[i];
        const struct air_constant_binding *b = &bindings[r->binding];
        uint32_t bytes = r->source_end - r->source_begin;
        for (uint32_t j = 0; j < bytes; j++) {
            size_t offset = (size_t)r->source_begin + j;
            dst[r->destination_begin + j] = offset < b->bytes ? b->data[offset] : 0;
        }
    }
    for (uint32_t i = 0; i < l->immediate_words; i++) {
        uint32_t word = l->immediates[i];
        for (unsigned byte = 0; byte < 4; byte++)
            dst[l->immediate_begin + i * 4 + byte] = (uint8_t)(word >> (byte * 8));
    }
    return (struct air_constant_result){AIR_CONSTANT_OK, written, padded};
}
