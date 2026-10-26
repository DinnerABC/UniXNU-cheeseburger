#pragma once
#include <stdint.h>
struct NativeVerifiedBlockPolicy { const uint8_t *manifest; uint64_t manifestBytes; };
extern "C" { extern const NativeVerifiedBlockPolicy verified_root_policies[]; extern const uint64_t verified_root_policy_count; }
