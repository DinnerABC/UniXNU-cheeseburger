/* SPDX-License-Identifier: MIT */
#ifndef AIR_CONSTANT_UPLOADS_H
#define AIR_CONSTANT_UPLOADS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define AIR_CONSTANT_MAX_BINDINGS 16u
#define AIR_CONSTANT_MAX_RANGES 32u
#define AIR_CONSTANT_MAX_READS 4096u
#define AIR_CONSTANT_MAX_BYTES 16384u
struct air_constant_read {
    uint32_t binding, offset, bytes;
};
struct air_constant_range {
    uint32_t binding, source_begin, source_end, destination_begin;
};
struct air_constant_binding {
    const uint8_t *data;
    size_t bytes, minimum_bytes;
};
struct air_constant_layout {
    uint32_t constant_bytes, upload_alignment, ubo_begin, ubo_bytes;
    const struct air_constant_range *ranges;
    size_t range_count;
    const struct air_constant_read *reads;
    size_t read_count;
    uint32_t immediate_begin, immediate_words;
    const uint32_t *immediates;
};
enum air_constant_status {
    AIR_CONSTANT_OK,
    AIR_CONSTANT_ARGUMENT,
    AIR_CONSTANT_CAPACITY,
    AIR_CONSTANT_LAYOUT,
    AIR_CONSTANT_BINDING,
    AIR_CONSTANT_READ,
    AIR_CONSTANT_ALIAS
};
struct air_constant_result {
    uint32_t status, written_bytes, padded_bytes;
};
/* Trusted readable metadata/backing lifetimes are the caller's responsibility.
 * Validation precedes every write. Failure leaves destination unchanged and
 * returns zero counts. Only declared ranges/immediates are written; internal
 * preamble, primitive and driver-parameter allocations remain untouched.
 * No allocation, GPU submission, memory translation or pixel computation. */
struct air_constant_result
air_pack_constants(const struct air_constant_layout *,
                   const struct air_constant_binding bindings[AIR_CONSTANT_MAX_BINDINGS],
                   uint8_t *destination, size_t destination_bytes);
#ifdef __cplusplus
}
#endif
#endif
