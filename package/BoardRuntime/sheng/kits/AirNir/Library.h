/* SPDX-License-Identifier: MIT */
#ifndef AIR_LIBRARY_H
#define AIR_LIBRARY_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define AIR_LIBRARY_MAX_BYTES (64u * 1024u * 1024u)
#define AIR_LIBRARY_MAX_FUNCTIONS 4096u
enum air_library_status {
    AIR_LIBRARY_OK, AIR_LIBRARY_ARGUMENT, AIR_LIBRARY_FORMAT,
    AIR_LIBRARY_BOUNDS, AIR_LIBRARY_HASH, AIR_LIBRARY_INDEX, AIR_LIBRARY_ALIAS
};
/* The callback must hash all supplied bytes, write exactly 32 digest bytes and
 * return zero on success. It is trusted code, not a library-supplied callback. */
typedef int (*air_library_sha256)(const uint8_t *, size_t, uint8_t[32], void *);
struct air_library_view {
    const uint8_t *data;
    size_t bytes, slice_offset, slice_bytes;
    size_t functions_offset, functions_bytes;
    uint32_t count, reserved;
};
struct air_library_function {
    const uint8_t *name, *bitcode, *wrapper, *wrapper_sha256;
    size_t name_bytes, bitcode_bytes, wrapper_bytes;
    uint32_t index, stage;
};
/* The input, callback and output must be valid accessible storage. Opening
 * checks every record and complete module hash before publishing a view.
 * Only the observed AIR-bearing MTLB profile is accepted. Fat selection uses
 * the actual container profile, never a host/Apple-GPU ISA assumption.
 * A view and its backing must remain unchanged/alive for get(); callers must
 * own or pin immutable bytes. This is structural/integrity validation, not
 * AMFI, a signature trust decision or permission to execute a shader.
 * Both calls leave outputs unchanged on error. No allocation or copying of
 * library contents, hardware mappings, GPU work or pixel computation. */
uint32_t air_library_open(const uint8_t *, size_t, air_library_sha256, void *, struct air_library_view *);
uint32_t air_library_get(const struct air_library_view *, uint32_t, struct air_library_function *);
#ifdef __cplusplus
}
#endif
#endif
