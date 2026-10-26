// Read-only snapshots of upstream display counters and its existing diagnostic handoff hooks.
#include <stdint.h>
#include <string.h>
#include <kern/thread.h>
#include <machine/machine_routines.h>
extern volatile uint32_t native_legacy_stage,native_legacy_registration_calls;
extern volatile uint32_t native_legacy_client_calls,native_legacy_client_type,native_legacy_frame_count;
extern volatile int native_legacy_client_pid,native_legacy_client_status;
extern volatile uint32_t native_legacy_swap_validation_calls,native_legacy_swap_rejected_count,native_legacy_rej_reason;
extern volatile uint32_t native_legacy_transfer_calls,native_legacy_retired_frames;
extern volatile int native_legacy_transfer_status;
extern volatile uint64_t p20_observer[32];
volatile unsigned p21_scanout_generation;
static thread_t p21_copy_owner;
volatile uint64_t p20_display_clients,p20_display_frames,p20_display_errors,p20_registry_seen;
volatile uint64_t p20_client_pid,p20_client_last_status,p20_client_attempts,p20_swap_stats[12];
extern volatile unsigned p20_scanout_state,p20_diag_active;
unsigned p20_scanout_owned(void);
void p20_display_snapshot(void);
int boot_progress_prepare_scanout(void);
void boot_progress_cancel_scanout(void);
unsigned p20_scanout_owned(void) {
 return __atomic_load_n(&p20_scanout_state,__ATOMIC_ACQUIRE)!=0;
}
// Cross-core reservation: 0 idle, 1 OS copy, 2 one diagnostic scanline.
// Keep the rendering algorithm and validation unchanged.
int boot_progress_prepare_scanout(void) {
 boolean_t irq=ml_set_interrupts_enabled(FALSE);
 unsigned expected=0;
 if(!__atomic_compare_exchange_n(&p20_scanout_state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {ml_set_interrupts_enabled(irq);return 0;}
 p21_copy_owner=current_thread();

 ml_set_interrupts_enabled(irq);return 1;
}
void p21_scanout_complete(void);
void boot_progress_cancel_scanout(void) { p21_scanout_complete(); }
void p20_display_snapshot(void) {
 p20_observer[7]=native_legacy_stage;p20_registry_seen=native_legacy_registration_calls;
 p20_display_clients=native_legacy_client_calls;p20_display_frames=native_legacy_retired_frames;
 p20_display_errors=native_legacy_swap_rejected_count;
 p20_client_attempts=native_legacy_client_calls;p20_client_pid=(uint64_t)(int64_t)native_legacy_client_pid;
 p20_client_last_status=(uint64_t)(int64_t)native_legacy_client_status;
 p20_swap_stats[0]=native_legacy_swap_validation_calls;p20_swap_stats[2]=native_legacy_swap_rejected_count;
 p20_swap_stats[3]=native_legacy_rej_reason;p20_swap_stats[7]=native_legacy_transfer_calls;
 p20_swap_stats[11]=(uint64_t)(int64_t)native_legacy_transfer_status;
}

// Completion below handles successful copies, black fills and failures equally.
void boot_progress_stage(const char *message);
void boot_progress_stage(const char *message) { (void)message; }

// Never release a different thread's active transfer on an unrelated error.
void p21_scanout_complete(void) {
 boolean_t irq=ml_set_interrupts_enabled(FALSE);
 __atomic_fetch_add(&p21_scanout_generation,1,__ATOMIC_RELEASE);
 if(p21_copy_owner==current_thread()) {
  p21_copy_owner=THREAD_NULL;
  __atomic_store_n(&p20_scanout_state,0,__ATOMIC_RELEASE);
 }
 ml_set_interrupts_enabled(irq);
}
