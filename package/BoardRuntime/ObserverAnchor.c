// SPDX-License-Identifier: MIT
// Implements the upstream diagnostic-anchor callback using its retained mapping.
// No early physical framebuffer alias or additional MMIO mapping is created.
#include <stdint.h>
#include <machine/machine_routines.h>
volatile uint64_t p6_diag_record[64];
volatile unsigned p20_scanout_state, p20_diag_active;
int boot_progress_pulse_anchor(uintptr_t address,uint64_t bytes,unsigned width,unsigned height,unsigned row) {
 if(!address || width!=1080 || height!=1920 || row!=4320 || bytes<(uint64_t)row*height)return 0;
 boolean_t irq=ml_set_interrupts_enabled(FALSE);
 p6_diag_record[16]=address;p6_diag_record[17]=bytes;p6_diag_record[26]=row;
 // The observer only needs a valid-anchor flag; this slot is not a physical PA claim.
 p6_diag_record[29]=1;
 ml_set_interrupts_enabled(irq);return 1;
}
