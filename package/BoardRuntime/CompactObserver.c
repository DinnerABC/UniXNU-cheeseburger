// Compact P20 observer: one read-only status page, refreshed every five seconds.
#include <stdint.h>
#include "P21SwapDebug.h"
#include <stddef.h>
#include <kern/thread.h>
#include <kern/clock.h>
#include <kern/sched_prim.h>
#include <kern/sched.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <string.h>

extern volatile uint64_t p6_diag_record[];
extern unsigned char iso_font[];
extern volatile unsigned p20_scanout_state, p20_diag_active;
extern unsigned p20_scanout_owned(void);
extern volatile unsigned p21_scanout_generation;
static unsigned rendered_generation;
extern void p20_display_snapshot(void);
extern void p21_client_rows(char rows[6][91]),p21_aks_rows(char rows[2][91]),p21_vsync_row(char *,unsigned);
extern char p21_wait_rows[600][91];
extern volatile uint64_t p20_display_frames, p20_display_errors, p20_display_clients;
extern volatile uint64_t p20_registry_seen;
extern volatile int native_legacy_meta_status;
extern volatile uint64_t p20_client_pid, p20_client_last_status, p20_client_attempts;
extern volatile uint64_t p20_swap_stats[12];
extern volatile uint64_t cb_keys[13],cb_touch[28];
extern volatile uint32_t armcpu_smp_running,armcpu_core_timer[8],armcpu_core_sgi[8],armcpu_smp_step[8],armcpu_smp_fail[8];
extern volatile int32_t armcpu_smp_psci_rc[8];
extern volatile uint32_t native_touch_hid_reports;
extern uint32_t processor_avail_count;
extern volatile unsigned native_keyevents_stage,native_aks_opens,native_aks_calls;
extern volatile int native_keyevents_status;
extern uint32_t native_aks_lock_state(void);
extern volatile unsigned native_hid_stage,native_hid_class_sizes[5],native_hid_input_events[8];
extern volatile int native_hid_load_status,native_hid_matching_load_status,native_hid_input_meta_status,native_hid_input_results[8];
extern volatile uint64_t native_hid_input_object;
extern unsigned native_legacy_prepare_report(char *,unsigned);
extern volatile unsigned native_legacy_pm_ordinal;
extern volatile uint32_t native_legacy_frame_count,native_legacy_frame_kind[32];
extern volatile uint64_t p21_scale_count,p21_scale_ticks,p21_scale_stride;
extern volatile int native_owner_cache_native_union, native_owner_cache_status;
extern volatile int native_owner_cache_readonly, native_owner_cache_authorized;
extern volatile unsigned cheeseburger_unattended_remaining;
extern volatile uint64_t native_owner_cache_actual_mount_flags;
extern int nprocs;
extern unsigned int vm_page_free_count, vm_page_wire_count, vm_page_active_count, vm_page_inactive_count;
extern uint64_t max_mem;
extern void boot_log_tick(unsigned);
extern volatile uint32_t boot_log_state,boot_log_rounds,boot_log_errors;
extern volatile const char *boot_log_open_why;
extern void boot_process_capture(void), boot_process_snapshot(void), p20_exit_snapshot(void), p20_fault_snapshot(void);
extern char boot_process_rows[24][120], p20_ui_rows[24][91], p20_exit_rows[5][120];
extern volatile unsigned boot_process_passes, boot_process_busy;

volatile uint64_t p20_observer[32];
volatile unsigned p20_diag_enabled;
static unsigned started;
static uint64_t epoch, frequency;
static unsigned last_process_pass, process_changed;
static char rendered[35][91];
static unsigned char render_valid[35];
static char pending_rows[35][91];

static uint64_t counter(void) { uint64_t v; __asm__ volatile("isb\n\tmrs %0,cntpct_el0" : "=r"(v)); return v; }
static unsigned uptime(void) { return frequency ? (unsigned)((counter() - epoch) / frequency) : 0; }
static void pause_worker(event_t event) {
    assert_wait_timeout(event, THREAD_UNINT, 5000, NSEC_PER_MSEC);
    thread_block(THREAD_CONTINUE_NULL);
}
static void stage(unsigned row, const char *text) {
    if (row < 35) strlcpy(pending_rows[row], text, sizeof(pending_rows[row]));
}

// Same verified 12x24 renderer and framebuffer bounds as P20Observer.c.
static void line(unsigned row, const char *text) {
    if (!p6_diag_record[16] || !p6_diag_record[29] || p6_diag_record[26] != 4320 ||
        p6_diag_record[17] < 4320ULL * 1920 || row >= 35) return;
    char shown[91] = {0};
    size_t length = strnlen(text, 90);
    memcpy(shown, text, length);
    if (render_valid[row] && !strcmp(rendered[row], shown)) return;
    for (unsigned gy = 0; gy < 24; ++gy) {
        bool irq = ml_set_interrupts_enabled(false);
        unsigned expected=0;
        if (!__atomic_compare_exchange_n(&p20_scanout_state,&expected,2,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
            ml_set_interrupts_enabled(irq);
            return;
        }
        volatile uint32_t *out = (volatile uint32_t *)(uintptr_t)
            (p6_diag_record[16] + (uint64_t)(1008 + row * 24 + gy) * 4320);
        for (unsigned col = 0; col < 90; ++col) {
            unsigned char ch = col < length ? (unsigned char)shown[col] : ' ';
            unsigned char bits = iso_font[(unsigned)ch * 16 + (gy * 16 / 24)];
            for (unsigned gx = 0; gx < 12; ++gx)
                out[col * 12 + gx] = (bits & (1U << (gx * 8 / 12))) ? 0xff00ffff : 0xff000000;
        }
        __asm__ volatile("dsb sy" ::: "memory");
        __atomic_store_n(&p20_scanout_state,0,__ATOMIC_RELEASE);
        ml_set_interrupts_enabled(irq);
    }
    memcpy(rendered[row], shown, sizeof(shown));
    render_valid[row] = 1;
}

static void process_worker(void *arg, wait_result_t result) {
    (void)arg; (void)result;
    thread_set_thread_name(current_thread(), "p20_compact_process");
    for (;;) { if (nprocs > 0) boot_process_capture(); pause_worker((event_t)&boot_process_busy); }
}

static void monitor(void *arg, wait_result_t result) {
    (void)arg; (void)result;
    thread_set_thread_name(current_thread(), "p20_compact_observer");
    for (;;) {
        unsigned now = uptime();
        p20_display_snapshot();
        boot_process_snapshot();
        p20_exit_snapshot();
        p20_fault_snapshot();
        if (last_process_pass != boot_process_passes) {
            last_process_pass = boot_process_passes;
            process_changed = now;
        }
        p20_observer[0] = now;
        p20_observer[1] = boot_process_passes;
        p20_observer[2] = vm_page_free_count;
        p20_observer[3] = vm_page_wire_count;
        p20_observer[4] = vm_page_active_count;
        p20_observer[5] = vm_page_inactive_count;
        p20_observer[6] = nprocs;

        memset(pending_rows, 0, sizeof(pending_rows));
        char b[120];
        snprintf(b, sizeof(b), "P22 R2 SMP obs=%us procs=%d pass=%u age=%us busy=%u RESET=%us", now, nprocs,
                 boot_process_passes, now - process_changed, boot_process_busy,
                 cheeseburger_unattended_remaining); stage(0, b);
        snprintf(b, sizeof(b), "MEM %lluMiB free=%u wire=%u active=%u inactive=%u",
                 max_mem >> 20, vm_page_free_count, vm_page_wire_count,
                 vm_page_active_count, vm_page_inactive_count); stage(1, b);
        snprintf(b, sizeof(b), "ROOT union=%d auth=%d ro=%d flags=%llx", native_owner_cache_native_union,
                 native_owner_cache_authorized, native_owner_cache_readonly,
                 native_owner_cache_actual_mount_flags); stage(2, b);
        snprintf(b, sizeof(b), "DISPLAY st=%llu opens=%llu rawFrames=%llu rej=%llu meta=%x",
                 p20_observer[7], p20_display_clients, p20_display_frames, p20_display_errors, (unsigned)native_legacy_meta_status); stage(3, b);
        snprintf(b, sizeof(b), "OPEN tries=%llu pid=%llu status=%llx reg=%llu", p20_client_attempts,
                 p20_client_pid, p20_client_last_status, p20_registry_seen); stage(4, b);
        snprintf(b, sizeof(b), "SWAP check=%llu reject=%llu why=%llx transfer=%llu rc=%llx",
                 p20_swap_stats[0], p20_swap_stats[2], p20_swap_stats[3],
                 p20_swap_stats[7], p20_swap_stats[11]); stage(5, b);
        snprintf(b,sizeof(b),"HID stage=%u load=%x match=%x reports=%u/%x",native_hid_stage,native_hid_load_status,native_hid_input_results[3],native_hid_input_events[4],native_hid_input_results[4]);stage(6,b);
        snprintf(b,sizeof(b),"KEY stage=%lld raw=%06llx edges=%llu sent=%llu err=%llu",cb_keys[0],cb_keys[4],cb_keys[6],cb_keys[7],cb_keys[3]);stage(7,b);
        snprintf(b,sizeof(b),"CPU online=%u steps=%u/%u/%u/%u/%u/%u/%u",processor_avail_count,armcpu_smp_step[1],armcpu_smp_step[2],armcpu_smp_step[3],armcpu_smp_step[4],armcpu_smp_step[5],armcpu_smp_step[6],armcpu_smp_step[7]);stage(8,b);
        for(unsigned i=0;i<6;++i)stage(9+i,p20_ui_rows[12+i]);
        stage(15,"FIRST WATCHDOGD FAILURE (retained)");
        for(unsigned i=0;i<6;++i)stage(16+i,p20_ui_rows[18+i]);
        stage(22,"BACKBOARDD first failure");
        for(unsigned i=0;i<6;++i)stage(23+i,p20_ui_rows[6+i]);
        stage(29,p20_exit_rows[0]);stage(30,p20_exit_rows[3]);stage(31,p20_exit_rows[4]);
        snprintf(b,sizeof(b),"PERSIST state=%u rounds=%u errors=%u why=%s / 32MiB",boot_log_state,boot_log_rounds,boot_log_errors,boot_log_open_why?boot_log_open_why:"?");stage(32,b);

        if(cb_touch[0]) {
            snprintf(b,sizeof(b),"TOUCH st=%lld err=%lld frames=%llu active=%llu HID=%u",cb_touch[0],cb_touch[5],cb_touch[24],cb_touch[6],native_touch_hid_reports);stage(33,b);
            snprintf(b,sizeof(b),"I2C ok/err=%llu/%llu range=%llux%llu poll/irq=%llu/%llu",cb_touch[1],cb_touch[2],cb_touch[8],cb_touch[9],cb_touch[11],cb_touch[10]);stage(34,b);
        }

        unsigned generation=__atomic_load_n(&p21_scanout_generation,__ATOMIC_ACQUIRE);
        if(generation!=rendered_generation){memset(render_valid,0,sizeof(render_valid));rendered_generation=generation;}
        for (unsigned row = 0; row < 35; ++row) {
            if (__atomic_load_n(&p20_scanout_state, __ATOMIC_ACQUIRE) ||
                p20_scanout_owned()) break;
            line(row, pending_rows[row]);
        }
        pause_worker((event_t)&p20_observer[0]);
    }
}

void p20_observer_start(void) {
    unsigned enabled = 0;
    PE_parse_boot_argn("p20-observe", &enabled, sizeof(enabled));
    if (enabled != 1 || started) return;
    started = 1;
    p20_diag_enabled = 1;
    boot_log_tick(0);
    p20_diag_active = 1;
    __asm__ volatile("mrs %0,cntfrq_el0" : "=r"(frequency));
    epoch = counter();
    thread_t thread;
    kern_return_t s = kernel_thread_start_priority(process_worker, 0, BASEPRI_KERNEL, &thread);
    p20_observer[18] = (uint32_t)s;
    if (s == KERN_SUCCESS) thread_deallocate(thread);
    s = kernel_thread_start_priority(monitor, 0, BASEPRI_KERNEL, &thread);
    if(s != KERN_SUCCESS)p20_diag_active=0;
    p20_observer[19] = (uint32_t)s;
    if (s == KERN_SUCCESS) thread_deallocate(thread);
}
