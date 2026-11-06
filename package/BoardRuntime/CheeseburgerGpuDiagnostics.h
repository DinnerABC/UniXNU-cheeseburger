// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stdint.h>
enum {CoreSampleBefore=1,CoreSampleAfter=2,CoreResultValid=4,CoreFenceValid=8};
struct CheeseburgerCoreRecord {
 uint32_t valid;
 int32_t rc,fence_rc;
 uint32_t fence_stage,rbpre,sppre,rbpost,sppost;
 uint64_t sample;
};
struct CheeseburgerDrainRecord {
 uint32_t valid,stage,reason,rptr,wptr,me,vbif,mask,ackme,ackvb,status,status3,endr,endw,wait;
 uint64_t sample;
 uint32_t preStatus,preStatus3,cpStatus,cpStatus3,cpMe,cpRead,cpWrite,cpValid;
};
#ifdef __cplusplus
extern "C" {
#endif
extern volatile struct CheeseburgerCoreRecord native_adreno_core_record;
extern volatile struct CheeseburgerDrainRecord native_adreno_drain_record;
#ifdef __cplusplus
}
#endif
