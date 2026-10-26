#pragma once
#include <stdint.h>
// Linux v6.12 msm8998-oneplus-common + pm8998 PMIC: PON bit0, GPIO5/6 active-low.
static inline uint32_t cb_key_mask(uint8_t pon,uint8_t down,uint8_t up) {
 return ((pon&1)?2u:0u)|((up&1)?0u:4u)|((down&1)?0u:8u);
}
static inline uint32_t cb_read_command(uint8_t offset) {return (1u<<27)|((uint32_t)offset<<4);}
static inline uint64_t cb_observer_address(unsigned apid) {return apid<512?0x09400000ull+0x8000ull*apid:0;}
