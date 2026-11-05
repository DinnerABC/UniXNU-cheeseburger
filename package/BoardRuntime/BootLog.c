// SPDX-License-Identifier: MIT
// Diagnostic log on UFS (boot argument sheng-log=1): every 2 s the text the board page would show (the diagnostic page, the live
// banner, the GPU rows, a caught panic message) and the kernel message buffer are written to the last 32 MiB of userdata through
// NativeUfs's bounded log window, so a run can be read back afterwards (scripts/read-sheng-log.py over the Linux log reader).
//
// Window layout (4096-byte blocks): 0 header, 1..32 the kernel message buffer (oldest first), 33.. text slots of 8 blocks in a ring,
// then the export (version 3, int-150): the newest complete blob that user space handed over (boot_log_export_put: the Metal plugin's
// pipeline caches, so the next image can start with them) lies in NativeUfs's export region, the 256 MiB below the window, which the
// reader fetches on its own. Its two halves alternate, so the header always names a blob that is wholly on the medium. (Version 2 kept a
// 16 MiB area at the window's tail; the UI's pipeline caches outgrew it in int-148 and int-149.)
// The header is written last in each round, so a reader never trusts a slot or an export the header does not count yet.
#include <kern/thread.h>
#include <kern/clock.h>
#include <string.h>
#include <pexpert/pexpert.h>
#include <IOKit/IOLib.h>
#include "BoardBootArgs.h"

extern int native_ufs_log_open(uint64_t *, uint64_t *, const char **) __attribute__((weak_import));
extern int native_ufs_log_write(uint64_t, uint32_t, const void *) __attribute__((weak_import));
extern uint64_t native_ufs_export_blocks(void) __attribute__((weak_import));
extern int native_ufs_export_write(uint64_t, uint32_t, const void *) __attribute__((weak_import));
extern int native_ufs_log_sync(void) __attribute__((weak_import));
extern unsigned native_ufs_store_gpt_check(void) __attribute__((weak_import));
extern char boot_progress_page_text[48][120] __attribute__((weak_import));
extern char boot_progress_banner_text[49][120] __attribute__((weak_import));
extern volatile unsigned boot_progress_banner_rows __attribute__((weak_import));
extern char boot_progress_ext[8][48] __attribute__((weak_import));
extern char boot_progress_ext_wide[2][112] __attribute__((weak_import));
extern char boot_progress_panic_text[256] __attribute__((weak_import));
extern volatile uint32_t native_data_persist_state __attribute__((weak_import));
extern volatile uint32_t native_data_persist_header __attribute__((weak_import));
extern volatile uint32_t native_data_persist_gpt __attribute__((weak_import));
extern volatile uint32_t native_data_persist_gpt_after __attribute__((weak_import));
extern volatile uint64_t native_data_persist_pages __attribute__((weak_import));
extern volatile uint64_t native_data_persist_loaded __attribute__((weak_import));
extern volatile uint64_t native_data_persist_writes __attribute__((weak_import));
extern volatile uint64_t native_data_persist_syncs __attribute__((weak_import));
extern volatile uint64_t native_data_persist_errors __attribute__((weak_import));
extern volatile uint64_t native_data_persist_first_lba __attribute__((weak_import));
extern volatile uint64_t native_data_persist_present __attribute__((weak_import));
extern const char *boot_progress_tag(void) __attribute__((weak_import));
extern unsigned boot_ui_trace_role_rows(unsigned, char [][120], unsigned, unsigned) __attribute__((weak_import));
extern unsigned boot_ui_trace_hot_rows(char [][120], unsigned) __attribute__((weak_import));
extern unsigned long vm_kernel_slide;
extern volatile uint64_t native_gop_wait_stats[7] __attribute__((weak_import));
extern volatile uint32_t native_adreno_fence_wait[8] __attribute__((weak_import));
extern volatile uint64_t native_adreno_wait_outer[4] __attribute__((weak_import));
extern volatile uint64_t native_adreno_service_stats[20] __attribute__((weak_import));
/* Effective uid of the last task that reached the GPU user client's open. */
extern volatile uint64_t native_adreno_client_uid __attribute__((weak_import));
/* Reads of the accelerator's discovery properties by the original Metal path. */
extern volatile uint64_t native_gpu_plugin_reads[3] __attribute__((weak_import));
/* Latest process name that reached the GPU user client's open. */
extern char native_adreno_client_name[17] __attribute__((weak_import));
/* Internal result of the last failed user submission (see CheeseburgerGpuCp). */
extern volatile int32_t native_adreno_submit_rc __attribute__((weak_import));
extern volatile uint32_t native_adreno_submit_stage __attribute__((weak_import));
extern volatile uint32_t native_adreno_submit_irq __attribute__((weak_import));
extern volatile uint32_t native_adreno_submit_cpirq __attribute__((weak_import));
extern volatile uint32_t native_adreno_submit_seq __attribute__((weak_import));
extern volatile uint32_t native_adreno_submit_post __attribute__((weak_import));
extern volatile uint32_t native_adreno_submit_preirq __attribute__((weak_import));
extern volatile uint32_t native_adreno_submit_precpirq __attribute__((weak_import));
extern volatile uint32_t native_adreno_submit_fenced __attribute__((weak_import));
extern volatile uint32_t native_adreno_submit_prefence __attribute__((weak_import));
extern volatile uint32_t native_adreno_submit_prefence_valid __attribute__((weak_import));
extern volatile uint32_t native_adreno_submit_fence_raw __attribute__((weak_import));
extern volatile uint32_t native_adreno_submit_atos_rc __attribute__((weak_import));
extern volatile uint64_t native_adreno_submit_atos_par __attribute__((weak_import));
extern volatile uint64_t native_adreno_submit_atos_expect __attribute__((weak_import));
extern volatile uint64_t native_adreno_submit_atos_pte __attribute__((weak_import));
extern volatile uint32_t native_adreno_selftest[8] __attribute__((weak_import));
extern volatile uint32_t native_adreno_rtmap[8] __attribute__((weak_import));
extern volatile uint32_t native_adreno_rtmap2[8] __attribute__((weak_import));
extern volatile uint32_t native_adreno_pa_test[8] __attribute__((weak_import));
extern volatile uint32_t native_adreno_ib_head[8] __attribute__((weak_import));
extern volatile uint32_t native_adreno_bo_dump[16] __attribute__((weak_import));
extern volatile uint32_t native_adreno_bo_atos[18] __attribute__((weak_import));
/* Turnip tunnel: calls carrying the NTNL header, and selector probes. */
extern volatile uint64_t native_adreno_tunnel[2] __attribute__((weak_import));
extern volatile uint32_t native_adreno_copy_test[8] __attribute__((weak_import));
/* Staged submission gate before the first client IB, and the last streams. */
extern volatile uint32_t native_adreno_gate[22] __attribute__((weak_import));
/* A540 2D clear: rc/seq/words, four quadrant pixels, edge sample, match, the
 * IB and target physical addresses, and the scanout band result. */
extern volatile uint32_t native_adreno_clear[24] __attribute__((weak_import));
/* GPU frame on the panel: render rc, physical address, stride, four band
 * pixels, and the display driver's flip result/pipe/control. */
extern volatile uint32_t native_adreno_flip[10] __attribute__((weak_import));
/* IR3 compute band: rc/seq/words, four samples, match, band PA/bytes and the
 * local/group geometry. */
extern volatile uint32_t native_adreno_compute[12] __attribute__((weak_import));
/* Compute isolation matrix: const/shift probes into our own 4 KiB SSBO and
 * the same two shaders over the scanout band. */
extern volatile uint32_t native_adreno_cprobe[24] __attribute__((weak_import));
/* GPU->GPU 2D copy: rc/seq/words, same-pitch and 1088-pitch samples and
 * match counts, plus the two destination pitches. */
extern volatile uint32_t native_adreno_copy2d[16] __attribute__((weak_import));
/* High physical address self-test: rc/seq, min/max page PA, pages above
 * 4 GiB and the four quadrant samples. */
extern volatile uint32_t native_adreno_highpa[12] __attribute__((weak_import));
/* High-address CP: IB fetch from a high page, then CP_MEM_TO_MEM read of a
 * high page, with the four returned words. */
extern volatile uint32_t native_adreno_highcp[8] __attribute__((weak_import));
/* 8 MiB surface through the batch mapper: rc/seq/words, runs, min/max PA,
 * four band samples and the match count. */
extern volatile uint32_t native_adreno_bigbo[13] __attribute__((weak_import));
/* GPU->compositor surface paint: rc/seq/words, runs, stride, format, match and
 * four quadrant samples. */
extern volatile uint32_t native_adreno_surface[12] __attribute__((weak_import));
extern volatile uint32_t native_adreno_ring_log540[4][68] __attribute__((weak_import));
extern volatile uint32_t native_adreno_ring_log540_count __attribute__((weak_import));
extern volatile uint32_t native_adreno_late_test[8] __attribute__((weak_import));
extern volatile uint32_t native_adreno_submit_staged __attribute__((weak_import));
extern volatile uint64_t native_adreno_submit_stage_gva __attribute__((weak_import));
extern volatile uint32_t native_adreno_pwr[14] __attribute__((weak_import));
/* Last refused open stage (1 not ready, 2 busy, 3 memory, 4 init, 5 attach, 6 start). */
extern volatile int native_adreno_open_stage __attribute__((weak_import));
extern volatile uint64_t native_adreno_abi_opens __attribute__((weak_import));
extern volatile uint64_t cb_gpu_paths[8] __attribute__((weak_import));
extern volatile uint64_t native_adreno_probe_exec __attribute__((weak_import));
extern volatile uint64_t native_gpu_addon_mount_flags __attribute__((weak_import));
extern volatile int native_gpu_addon_orig __attribute__((weak_import));
extern volatile int native_gpu_addon_own __attribute__((weak_import));
extern volatile int native_gpu_addon_rollback __attribute__((weak_import));
extern volatile uint64_t native_gpu_addon_data_stat[3] __attribute__((weak_import));
extern volatile uint64_t native_gpu_addon_mount_stat[3] __attribute__((weak_import));
extern volatile uint64_t native_gpu_addon_orig_stat[3] __attribute__((weak_import));
extern volatile int native_gpu_addon_boot_mount __attribute__((weak_import));
extern char native_adreno_service_text[64] __attribute__((weak_import));
extern volatile int native_adreno_service_ready __attribute__((weak_import));
extern volatile int native_gpu_addon_stage __attribute__((weak_import));
extern volatile int native_gpu_addon_status __attribute__((weak_import));
extern volatile int native_gpu_addon_files __attribute__((weak_import));
extern volatile int native_gpu_addon_written __attribute__((weak_import));
extern volatile int native_gpu_addon_same __attribute__((weak_import));
extern volatile int native_gpu_addon_failed __attribute__((weak_import));
extern volatile int native_gpu_addon_error __attribute__((weak_import));
extern volatile int native_gpu_addon2_stage __attribute__((weak_import));
extern volatile int native_gpu_addon2_status __attribute__((weak_import));
extern volatile int native_gpu_addon3_stage __attribute__((weak_import));
extern volatile int native_gpu_addon3_status __attribute__((weak_import));
/* Owner copy of the sealed launchd service cache: stage, last error, the
 * sealed and augmented sizes, object/daemon counts, whether the loader binary
 * carries launchd_unsecure_cache=, the union mount stage/status and rollback. */
extern volatile int native_gpu_addon_cache_stage __attribute__((weak_import));
extern volatile int native_gpu_addon_cache_status __attribute__((weak_import));
extern volatile int native_gpu_addon_cache_loader __attribute__((weak_import));
extern volatile int native_gpu_addon_cache_mount_stage __attribute__((weak_import));
extern volatile int native_gpu_addon_cache_mount_status __attribute__((weak_import));
extern volatile int native_gpu_addon_cache_rollback __attribute__((weak_import));
extern volatile uint64_t native_gpu_addon_cache_input __attribute__((weak_import));
extern volatile uint64_t native_gpu_addon_cache_output __attribute__((weak_import));
extern volatile int native_gpu_addon_cache_objects __attribute__((weak_import));
extern volatile int native_gpu_addon_cache_daemons __attribute__((weak_import));
extern volatile uint32_t native_adreno_export_stats[4] __attribute__((weak_import));
extern volatile int32_t native_touch_strokes[16][7] __attribute__((weak_import));
extern volatile uint32_t native_touch_stroke_count __attribute__((weak_import));
extern volatile int32_t native_touch_extent[4] __attribute__((weak_import));
extern volatile uint32_t native_adreno_method_stats[20][4] __attribute__((weak_import));
extern volatile uint64_t native_adreno_vm_phase_us[2][8] __attribute__((weak_import));
extern volatile uint32_t native_adreno_map_fail[7][2] __attribute__((weak_import));
extern volatile uint32_t native_adreno_pool_stats[8] __attribute__((weak_import));
extern volatile uint32_t native_adreno_wc_audit[6] __attribute__((weak_import));
extern volatile uint32_t native_touch_edge_stats[4] __attribute__((weak_import));
extern volatile uint64_t native_adreno_reassert_ticks __attribute__((weak_import));
extern volatile uint64_t native_adreno_cache_stats[5] __attribute__((weak_import));
// NativeAdrenoPublish.cpp: per process name that opened the GPU, and the processes that asked the Metal discovery for its plugin.
struct native_adreno_client_record {char name[20]; uint32_t opens, refused, closes, slot, peak_mib, last_refusal;};
extern struct native_adreno_client_record native_adreno_client_log[8] __attribute__((weak_import));
struct native_adreno_asker_record {char name[20]; uint32_t reads;};
extern struct native_adreno_asker_record native_adreno_metal_askers[12] __attribute__((weak_import));
extern char native_gpu_coherence_row[120] __attribute__((weak_import));
extern size_t boot_progress_profile_rows(char *, size_t) __attribute__((weak_import));
extern volatile uint64_t native_gpu_idle_sleeps __attribute__((weak_import));
extern volatile uint64_t native_gpu_idle_spins __attribute__((weak_import));
extern volatile uint32_t native_adreno_coherent_skips[4] __attribute__((weak_import));
extern volatile uint32_t native_adreno_io_coherent __attribute__((weak_import));
extern char native_cpufreq_row[120] __attribute__((weak_import));
extern uint32_t native_gpu_perf_vote[2] __attribute__((weak_import));
extern volatile uint32_t native_adreno_pool_status __attribute__((weak_import));
extern volatile uint32_t native_adreno_pool_wc __attribute__((weak_import));
extern volatile uint64_t native_adreno_fence_phys __attribute__((weak_import));
extern int (*native_adreno_dram_read32)(uint64_t, uint32_t *) __attribute__((weak_import));
extern int (*native_adreno_cached_read32)(uint64_t, uint32_t *) __attribute__((weak_import));
extern volatile uint32_t native_adreno_ring_log[4][4 + 112] __attribute__((weak_import));
extern volatile uint32_t native_adreno_ring_log_count __attribute__((weak_import));
extern volatile uint32_t native_adreno_ib_log[4][4 + 28] __attribute__((weak_import));
extern volatile uint32_t native_adreno_ib_log_count __attribute__((weak_import));
extern unsigned native_legacy_properties_now(char *, unsigned) __attribute__((weak_import));
extern unsigned native_legacy_clients_dump(char *, unsigned) __attribute__((weak_import));
extern unsigned native_legacy_swap_timeline(char *, unsigned) __attribute__((weak_import));
extern unsigned native_legacy_prepare_report(char *, unsigned) __attribute__((weak_import));
extern char native_adreno_metal_rows[48][96] __attribute__((weak_import));
extern unsigned native_legacy_state_words(char *, unsigned) __attribute__((weak_import));
extern unsigned native_legacy_event_rows(char *, unsigned) __attribute__((weak_import));
extern volatile uint32_t native_adreno_view_diag[6] __attribute__((weak_import));
extern int boot_reset_probe(char *, unsigned) __attribute__((weak_import));
extern int boot_reset_bootloader(char *, unsigned) __attribute__((weak_import));
extern volatile uint64_t native_adreno_import_diag[10] __attribute__((weak_import));
extern volatile uint32_t native_adreno_import_stage __attribute__((weak_import));
extern volatile uint64_t native_adreno_import_fail_pa __attribute__((weak_import)), native_adreno_import_fail_offset __attribute__((weak_import));
// What the reset probe and the autoreset attempt saw, for the [reset] section of the log.
static char boot_log_reset_probe_text[240], boot_log_reset_result[120];
// The kernel message buffer (bsd/sys/msgbuf.h); only these leading fields are read.
struct boot_log_msgbuf {int msg_magic, msg_size, msg_bufx, msg_bufr; char *msg_bufc;};
extern struct boot_log_msgbuf *msgbufp;

enum {LogBlock = 4096, LogMsgFirst = 1, LogMsgBlocks = 256, LogSlotFirst = 257, LogSlotBlocks = 32, LogPeriodMs = 5000, LogMaxErrors = 3,
      LogExportStage = 64};   // export bytes staged per write: 256 KiB
struct boot_log_header {
  char magic[8];                 // "SHLOG001"
  uint32_t version, header_bytes;
  uint64_t boot_id;              // CNTPCT when the writer started: tells one run from the next
  uint64_t first_lba;            // absolute 4096-byte LBA of block 0 on LUN 0
  uint32_t window_blocks, msg_first, msg_blocks, msg_bytes, slot_first, slot_blocks, slot_count, slots_written;
  uint32_t uptime_s, rounds, write_errors, gpt_open, gpt_last, state;
  char tag[32];
  // version 3: the newest complete export at block export_first of the export region (export_blocks long, ending export_below blocks
  // below block 0 of this window); export_bytes 0 = nothing exported yet
  uint32_t export_first, export_blocks, export_bytes, export_generation;
  uint32_t export_below;
};
// Writer state for the board page: 0 off, 1 waiting for the window, 2 writing, 3 stopped after errors, 4 stopped (GPT changed).
volatile uint32_t boot_log_state, boot_log_rounds, boot_log_errors;
volatile uint64_t boot_log_first_lba;
static int boot_log_started;
static uint8_t *boot_log_buffer;   // LogMsgBlocks blocks: the message buffer, then reused for a slot and the header
static uint8_t *boot_log_heartbeat_buffer, *boot_log_autoreset_buffer, *boot_log_step_buffer;
volatile uint64_t boot_log_blocks, boot_log_hb_first;
volatile uint32_t boot_log_hb_count;
static uint64_t boot_log_ticks_start, boot_log_freq;

static uint64_t log_ticks(void) {uint64_t v; __asm__ volatile("isb\n\tmrs %0, cntpct_el0" : "=r"(v)); return v;}

// Appends a text row without its trailing blanks; returns the new length (never past cap - 1).
static size_t put(char *out, size_t at, size_t cap, const char *text, size_t max) {
  size_t n = strnlen(text, max);
  while (n && (text[n - 1] == ' ' || text[n - 1] == 0)) --n;
  for (size_t i = 0; i < n && at + 2 < cap; ++i) {char c = text[i]; out[at++] = (c >= 32 && c < 127) ? c : '?';}
  if (at + 1 < cap) out[at++] = '\n';
  return at;
}

extern volatile uint64_t p20_observer[32],p20_registry_seen,p20_display_clients,p20_display_frames,p20_swap_stats[12];
extern volatile int native_legacy_meta_status;
extern volatile int cheeseburger_watchdog_policy_requested,cheeseburger_watchdog_policy_status,cheeseburger_watchdog_policy_applied;
extern char p21_wait_rows[360][91];
extern char p20_ui_rows[24][91],p20_exit_rows[5][120],boot_process_rows[24][120];
extern char p8_console_ring[32768];extern volatile unsigned p8_console_count;
extern void p21_client_rows(char rows[6][91]),p21_aks_rows(char rows[2][91]),p21_vsync_row(char *,unsigned);
// Existing read-only reports; live counters are approximate, not an atomic snapshot.
extern volatile int cheeseburger_upstream_policy_requested,cheeseburger_upstream_policy_status,cheeseburger_upstream_policy_applied;
static char display_report[4096];
static size_t put_report(char *out,size_t at,size_t cap,const char *text,size_t limit) {
 size_t n=strnlen(text,limit),begin=0;
 for(size_t i=0;i<=n;++i)if(i==n || text[i]=='\n') {
  if(i>begin)at=put(out,at,cap,text+begin,i-begin);
  begin=i+1;
 }
 return at;
}

extern volatile uint32_t native_legacy_stage;
#include "OfficialLogReports.inc"
extern unsigned p21_match_dump(char *,unsigned);
static char official_report[32768],match_report[32768];
extern volatile uint64_t cb_touch[28];
extern volatile uint64_t cb_touch_extra[80] __attribute__((weak_import));
extern volatile uint64_t cb_usb[128] __attribute__((weak_import));
extern volatile uint64_t cb_disp[128] __attribute__((weak_import));
extern volatile uint64_t cb_gpu[80] __attribute__((weak_import));
extern volatile uint64_t cb_reset[12] __attribute__((weak_import));
extern volatile uint64_t cb_smmu[24] __attribute__((weak_import));
extern volatile uint64_t native_smmu_irq[6] __attribute__((weak_import));
extern volatile uint64_t cb_map[4] __attribute__((weak_import));
extern volatile uint64_t cb_cp[128] __attribute__((weak_import));
extern volatile uint64_t cb_zap[32] __attribute__((weak_import));
extern volatile uint64_t cb_mapdiag[4] __attribute__((weak_import));
extern volatile uint64_t cb_mapret[4] __attribute__((weak_import));
volatile uint64_t cb_boot_id=0;

// Independent liveness channel: one fixed block at the end of the authorized
// window, written directly and never through msgbuf/console. A console or
// msgbuf stall therefore cannot erase this record. The reader parses it as a
// separate heartbeat.json; it is not part of the header/msg/slot structures.
// Producers own disjoint sub-rings so a concurrent tick/marker/step write can
// never overwrite another producer's record. 31 free blocks: 22 tick, 6 step,
// 3 auto-reset. The reader groups by tag and filters by the header boot id.
enum {HbTickSlots = 22, HbStepSlots = 6, HbResetSlots = 3, HbSlots = HbTickSlots + HbStepSlots + HbResetSlots};
static void heartbeat_record(uint8_t *buf, const char *tag, uint64_t seq, unsigned base, unsigned count) {
  if (!buf || !boot_log_blocks || !cb_boot_id || !boot_log_ticks_start || boot_log_state != 2) return;
  const uint64_t now = boot_log_freq ? (log_ticks() - boot_log_ticks_start) / boot_log_freq : 0;
  memset(buf, 0, LogBlock);
  int line = snprintf((char *)buf, LogBlock,
           "CBHB0001 boot=%016llx seq=%llu sec=%llu tag=%s state=%u rounds=%u",
           (unsigned long long)cb_boot_id, (unsigned long long)seq, (unsigned long long)now,
           tag, (unsigned)boot_log_state, (unsigned)boot_log_rounds);
  // The tick also carries the GPU user-client state so it survives a snapshot
  // stop: markers/last marker, opens, the last refused open stage and ABI opens.
  if (line > 0 && line < LogBlock && tag && !strcmp(tag, "tick")) {
    snprintf((char *)buf + line, LogBlock - (size_t)line,
             " m=%llu l=%llx o=%llu s=%d a=%llu f=%d/%u/%x/%x/%u/%x/%x/%u",
             &native_adreno_service_stats[0] ? native_adreno_service_stats[16] : 0,
             &native_adreno_service_stats[0] ? native_adreno_service_stats[17] : 0,
             &native_adreno_service_stats[0] ? native_adreno_service_stats[0] : 0,
             &native_adreno_open_stage ? native_adreno_open_stage : -1,
             &native_adreno_abi_opens ? native_adreno_abi_opens : 0,
             &native_adreno_submit_rc ? (int)native_adreno_submit_rc : -1,
             &native_adreno_submit_stage ? native_adreno_submit_stage : 0,
             &native_adreno_submit_irq ? native_adreno_submit_irq : 0,
             &native_adreno_submit_cpirq ? native_adreno_submit_cpirq : 0,
             &native_adreno_submit_seq ? native_adreno_submit_seq : 0,
             &native_adreno_submit_preirq ? native_adreno_submit_preirq : 0,
             &native_adreno_submit_precpirq ? native_adreno_submit_precpirq : 0,
             &native_adreno_submit_fenced ? native_adreno_submit_fenced : 0);
    size_t used = strnlen((char *)buf, LogBlock - 1);
    if (used + 1 < LogBlock) { ((char *)buf)[used] = '\n'; ((char *)buf)[used + 1] = 0; }
  }
  // Use every block after the slot area as a small heartbeat ring so a fault
  // boot still keeps the ticks leading up to the stop, not only the last one.
  uint64_t lba;
  if (boot_log_hb_count >= HbSlots) {
    if (base + count > HbSlots) return;
    lba = boot_log_hb_first + base + (seq % count);
  } else {
    lba = boot_log_blocks - 1;
  }
  if (native_ufs_log_write(lba, 1, buf)) native_ufs_log_sync();
}
void cheeseburger_bootlog_heartbeat(void) {
  static uint64_t seq;
  heartbeat_record(boot_log_heartbeat_buffer, "tick", ++seq, 0, HbTickSlots);
}
void cheeseburger_bootlog_autoreset(const char *tag) {
  static uint64_t seq;
  heartbeat_record(boot_log_autoreset_buffer, tag ? tag : "mark", ++seq, HbTickSlots + HbStepSlots, HbResetSlots);
}
// Reset/start step markers: the probe calls this around each GPU register step
// so a stall leaves the last reached step in the heartbeat ring.
void cheeseburger_bootlog_step(const char *tag) {
  static uint64_t seq;
  heartbeat_record(boot_log_step_buffer, tag ? tag : "step", ++seq, HbTickSlots, HbStepSlots);
}
static void heartbeat_worker(void *arg, wait_result_t result) {
  (void)arg; (void)result;
  thread_set_thread_name(current_thread(), "gpu_heartbeat");
  for (;;) {
    IOSleep(10000);
    cheeseburger_bootlog_heartbeat();
  }
}
extern volatile uint64_t cb_rails[24] __attribute__((weak_import));
extern volatile uint64_t cb_pmic_dump[24] __attribute__((weak_import));
extern volatile uint64_t cb_pmic_pages[8] __attribute__((weak_import));
extern volatile int native_legacy_unit_scale_status;
extern volatile uint32_t native_legacy_unit_scale_calls,native_legacy_chosen_scale,native_legacy_chosen_rotation;
extern volatile uint32_t armcpu_smp_running,armcpu_core_timer[8],armcpu_core_sgi[8],armcpu_smp_step[8],armcpu_smp_fail[8],armcpu_smp_mpidr[8];
extern volatile int32_t armcpu_smp_psci_rc[8];
extern volatile uint32_t native_touch_hid_reports;
extern uint32_t processor_avail_count;
#include "PlatformLogReports.inc"
static size_t compose(char *out,size_t cap,uint32_t slot,unsigned now) {
 size_t at=0;char head[96];snprintf(head,sizeof(head),"#slot %u t=%us tag=cheeseburger-P21",slot,now);at=put(out,at,cap,head,sizeof(head));
 snprintf(head,sizeof(head),"DISPLAY stage=%llu meta=%x reg=%llu clients=%llu frames=%llu",p20_observer[7],native_legacy_meta_status,p20_registry_seen,p20_display_clients,p20_display_frames);at=put(out,at,cap,head,sizeof(head));
 snprintf(head,sizeof(head),"SWAP check=%llu reject=%llu why=%llu transfer=%llu rc=%llx",p20_swap_stats[0],p20_swap_stats[2],p20_swap_stats[3],p20_swap_stats[7],p20_swap_stats[11]);at=put(out,at,cap,head,sizeof(head));
 snprintf(head,sizeof(head),"DISPLAY SCALE status=%d calls=%u chosen=%u rotation=%u",native_legacy_unit_scale_status,native_legacy_unit_scale_calls,native_legacy_chosen_scale,native_legacy_chosen_rotation);at=put(out,at,cap,head,sizeof(head));
 snprintf(head,sizeof(head),"WATCHDOG policy requested=%d status=%d applied=%d",cheeseburger_watchdog_policy_requested,cheeseburger_watchdog_policy_status,cheeseburger_watchdog_policy_applied);at=put(out,at,cap,head,sizeof(head));
 snprintf(head,sizeof(head),"UP1 policy requested=%d status=%d written=%d jobs=watchdogd/identityservicesd/locationd",cheeseburger_upstream_policy_requested,cheeseburger_upstream_policy_status,cheeseburger_upstream_policy_applied);at=put(out,at,cap,head,sizeof(head));
 for(unsigned cpu=0;cpu<8;++cpu){char core[144];snprintf(core,sizeof core,"CPU%u mpidr=%x step=%u fail=%u PSCI=%d timer=%u IPI=%u online=%u",cpu,armcpu_smp_mpidr[cpu],armcpu_smp_step[cpu],armcpu_smp_fail[cpu],armcpu_smp_psci_rc[cpu],armcpu_core_timer[cpu],armcpu_core_sgi[cpu],processor_avail_count);at=put(out,at,cap,core,sizeof core);}
 if(cb_touch[0]) {
  char touch[200];
  snprintf(touch,sizeof touch,"TOUCH stage=%llu error=%lld i2c=%llu/%llu F01=%llx F12=%llx frames=%llu contacts=%llu publish=%llu HID=%u",cb_touch[0],(int64_t)cb_touch[5],cb_touch[1],cb_touch[2],cb_touch[3],cb_touch[4],cb_touch[24],cb_touch[6],cb_touch[7],native_touch_hid_reports);at=put(out,at,cap,touch,sizeof touch);
  snprintf(touch,sizeof touch,"TOUCH range=%llux%llu objects=%llu data=%llx/%llu irq=%llu polls=%llu mode=%llu raw=%llx mapped=%llx",cb_touch[8],cb_touch[9],cb_touch[20],cb_touch[21],cb_touch[22],cb_touch[10],cb_touch[11],cb_touch[23],cb_touch[25],cb_touch[26]);at=put(out,at,cap,touch,sizeof touch);
  snprintf(touch,sizeof touch,"TOUCH hw=%llx clock=%llx reset=%llx gpio=%llx/%llx status=%llx errors=%llx op=%llx",cb_touch[12],cb_touch[13],cb_touch[14],cb_touch[15],cb_touch[16],cb_touch[17],cb_touch[18],cb_touch[19]);at=put(out,at,cap,touch,sizeof touch);
  snprintf(touch,sizeof touch,"TOUCH lines sda=%llu scl=%llu rst=%llu int=%llu",cb_touch[27]&1,(cb_touch[27]>>1)&1,(cb_touch[27]>>2)&1,(cb_touch[27]>>3)&1);at=put(out,at,cap,touch,sizeof touch);
  snprintf(touch,sizeof touch,"TOUCH2 post gcc=%llx rcg=%llx mode=%llx status=%llx op=%llx pins=%llx",
    cb_touch_extra[0],cb_touch_extra[1],cb_touch_extra[2],cb_touch_extra[3],cb_touch_extra[4],cb_touch_extra[5]);at=put(out,at,cap,touch,sizeof touch);
  snprintf(touch,sizeof touch,"TOUCH3 pre qup0state=%llx qupver=%llx gcc52004=%llx",
    cb_touch_extra[6],cb_touch_extra[7],cb_touch_extra[21]);at=put(out,at,cap,touch,sizeof touch);
  snprintf(touch,sizeof touch,"TOUCH4 pullup sda=%llu scl=%llu rst=%llu int=%llu after=%llx",
    cb_touch_extra[12]&1,(cb_touch_extra[12]>>1)&1,(cb_touch_extra[12]>>2)&1,(cb_touch_extra[12]>>3)&1,cb_touch_extra[13]);at=put(out,at,cap,touch,sizeof touch);
  snprintf(touch,sizeof touch,"TOUCH5 raw=%llx gpiopull=%llx muxed=%llx",
    cb_touch_extra[14],cb_touch_extra[15],cb_touch_extra[20]);at=put(out,at,cap,touch,sizeof touch);
  snprintf(touch,sizeof touch,"TOUCH6 cfg87=%llx cfg88=%llx cfg89=%llx cfg125=%llx",
    cb_touch_extra[16],cb_touch_extra[17],cb_touch_extra[18],cb_touch_extra[19]);at=put(out,at,cap,touch,sizeof touch);
  snprintf(touch,sizeof touch,"TOUCH7 post qup0=%llx qup400=%llx qup408=%llx qupc=%llx",
    cb_touch_extra[8],cb_touch_extra[9],cb_touch_extra[10],cb_touch_extra[11]);at=put(out,at,cap,touch,sizeof touch);
  snprintf(touch,sizeof touch,"TOUCH8 drive=%llx bc=%llx",cb_touch_extra[22],cb_touch_extra[23]);at=put(out,at,cap,touch,sizeof touch);
  snprintf(touch,sizeof touch,"TOUCH10 pdt=%llx/%llx/%llx",cb_touch_extra[26],cb_touch_extra[27],cb_touch_extra[28]);at=put(out,at,cap,touch,sizeof touch);
  snprintf(touch,sizeof touch,"TOUCH12 ack=%llx,%llx w=%lld r=%lld b=%llx page=%lld",
    cb_touch_extra[32],cb_touch_extra[39],(long long)(int32_t)cb_touch_extra[33],(long long)(int32_t)cb_touch_extra[34],cb_touch_extra[35],(long long)(int32_t)cb_touch_extra[45]);at=put(out,at,cap,touch,sizeof touch);
  snprintf(touch,sizeof touch,"TOUCH13 st=%llx op=%llx err=%llx status=%llx",
    cb_touch_extra[36]&0xffff,(cb_touch_extra[36]>>16)&0xffff,(cb_touch_extra[36]>>32)&0xffff,cb_touch_extra[37]);at=put(out,at,cap,touch,sizeof touch);
  snprintf(touch,sizeof touch,"TOUCH14 fifo=%llu/%llu blk=%llu/%llu pre=%llx pdt=%lld cnt=%llx/%llx",
    cb_touch_extra[40],cb_touch_extra[41],cb_touch_extra[42],cb_touch_extra[43],cb_touch_extra[44],
    (long long)(int32_t)cb_touch_extra[46],cb_touch_extra[38]&0xffffffff,cb_touch_extra[38]>>32);at=put(out,at,cap,touch,sizeof touch);
  snprintf(touch,sizeof touch,"TOUCH15 pg0=%llx pg1=%llx one=%02llx ff=%llx split=%llx",
    cb_touch_extra[48],cb_touch_extra[49],cb_touch_extra[50]&0xff,cb_touch_extra[62],cb_touch_extra[63]);at=put(out,at,cap,touch,sizeof touch);
  snprintf(touch,sizeof touch,"TOUCH17 dd=%llx dd2=%llx dd3=%llx map=%llx%08llx",
    cb_touch_extra[26],cb_touch_extra[60],cb_touch_extra[61],cb_touch_extra[57],cb_touch_extra[56]);at=put(out,at,cap,touch,sizeof touch);
  snprintf(touch,sizeof touch,"TOUCH18 e9=%llx e92=%llx map=%llx%08llx",
    cb_touch_extra[27],cb_touch_extra[28],cb_touch_extra[59],cb_touch_extra[58]);at=put(out,at,cap,touch,sizeof touch);
  snprintf(touch,sizeof touch,"TOUCH16 f12q=%llx f12c=%llx at f01n=%llx f12n=%llx f01r=%llx f12r=%llx",
    cb_touch_extra[53],cb_touch_extra[54],
    cb_touch_extra[55]&0xff,(cb_touch_extra[55]>>8)&0xff,(cb_touch_extra[55]>>16)&0xff,(cb_touch_extra[55]>>24)&0xff);at=put(out,at,cap,touch,sizeof touch);
  snprintf(touch,sizeof touch,"TOUCH19 nz=%llu p0=%llx p1=%llx f01=%llx sink=%llu send=%llu nodrv=%llu rep=%llu pend=%llu",
    cb_touch_extra[72],cb_touch_extra[64],cb_touch_extra[65],cb_touch_extra[73],
    cb_touch_extra[74],cb_touch_extra[75],cb_touch_extra[76],cb_touch_extra[77],cb_touch_extra[78]);at=put(out,at,cap,touch,sizeof touch);
 }
 if (&cb_usb[0] && cb_usb[112]) {
  char usb[240];
  snprintf(usb,sizeof usb,"USB0 gdsc=%08llx mclk=%08llx slp=%08llx utmi=%08llx aux=%08llx pipe=%08llx noc=%08llx aggr=%08llx ref=%08llx ref2=%08llx ahb=%08llx fl=%llx",
    cb_usb[0]&0xffffffff,cb_usb[1]&0xffffffff,cb_usb[2]&0xffffffff,cb_usb[3]&0xffffffff,cb_usb[4]&0xffffffff,cb_usb[5]&0xffffffff,cb_usb[6]&0xffffffff,cb_usb[7]&0xffffffff,cb_usb[8]&0xffffffff,cb_usb[9]&0xffffffff,cb_usb[10]&0xffffffff,cb_usb[11]);at=put(out,at,cap,usb,sizeof usb);
  snprintf(usb,sizeof usb,"USB1 pre gdsc=%08llx mclk=%08llx slp=%08llx utmi=%08llx aux=%08llx pipe=%08llx noc=%08llx aggr=%08llx ref=%08llx ref2=%08llx ahb=%08llx",
    cb_usb[12]&0xffffffff,cb_usb[13]&0xffffffff,cb_usb[14]&0xffffffff,cb_usb[15]&0xffffffff,cb_usb[16]&0xffffffff,cb_usb[17]&0xffffffff,cb_usb[18]&0xffffffff,cb_usb[19]&0xffffffff,cb_usb[20]&0xffffffff,cb_usb[21]&0xffffffff,cb_usb[22]&0xffffffff);at=put(out,at,cap,usb,sizeof usb);
  for(unsigned i=32;i<128;i+=16){
   int n=snprintf(usb,sizeof usb,"USB%u",i/16+1);
   for(unsigned k=0;k<16 && i+k<128;++k){
    if(n<0||(size_t)n>sizeof usb-12)n=(int)sizeof usb-12;
    n+=snprintf(usb+n,sizeof usb-n," %08llx",cb_usb[i+k]&0xffffffffull);
   }
   at=put(out,at,cap,usb,sizeof usb);
  }
 }
 if (&cb_disp[0] && (cb_disp[124] || cb_disp[125])) {
  char disp[240];
  snprintf(disp,sizeof disp,"DISP layout: 0 mdss_hw,1 mdp_hw,2-6 ctl top,7-11 ctl flush,12-16 ctl op,17-21 ctl start,22-27 lm out_size,28-33 lm blend0,34-45 pp tear/sync/height,46-51 intf,52-75 pipes addr/stride/fmt,76-99 pipes xy/out/opmode,100-102 dsi0,120-122 gop");
  at=put(out,at,cap,disp,sizeof disp);
  for(unsigned i=0;i<112;i+=16){
   int n=snprintf(disp,sizeof disp,"DISPu%u",i/16+1);
   for(unsigned k=0;k<16 && i+k<112;++k){
    if(n<0||(size_t)n>sizeof disp-12)n=(int)sizeof disp-12;
    n+=snprintf(disp+n,sizeof disp-n," %08llx",cb_disp[i+k]&0xffffffffull);
   }
   at=put(out,at,cap,disp,sizeof disp);
  }
  snprintf(disp,sizeof disp,"DISP gop=%llx stride=%llx w/h=%llx stage=%llu call=%llu flip=%llu/%llu",cb_disp[120],(cb_disp[121]>>32)&0xffffffff,cb_disp[122],cb_disp[124],cb_disp[125],cb_disp[126],cb_disp[127]);at=put(out,at,cap,disp,sizeof disp);
  snprintf(disp,sizeof disp,"DISP timing wr=%llx/%llx ic=%llx/%llx fps=%llu/%llu intf_en=%llx cfg=%llx vp=%llx",
    cb_disp[103],cb_disp[104],cb_disp[105],cb_disp[106],cb_disp[107],cb_disp[108],cb_disp[109],cb_disp[110],cb_disp[111]);at=put(out,at,cap,disp,sizeof disp);
 }
 if (&native_data_persist_state) {
  char data[240];
  snprintf(data,sizeof data,"DATA persist state=%u header=%u gpt=%u/%u pages=%llu loaded=%llu writes=%llu syncs=%llu err=%llu first=%llu present=%llu",
    native_data_persist_state,native_data_persist_header,native_data_persist_gpt,native_data_persist_gpt_after,
    native_data_persist_pages,native_data_persist_loaded,native_data_persist_writes,native_data_persist_syncs,
    native_data_persist_errors,native_data_persist_first_lba,native_data_persist_present);
  at=put(out,at,cap,data,sizeof data);
 }
 if (&cb_gpu[0] && cb_gpu[63]) {
  char gpu[240];
  snprintf(gpu,sizeof gpu,"GPU0 gcc ahb=%08llx bimc=%08llx misc=%08llx bimc2=%08llx>%08llx stage=%llu flags=%llx err=%llx",
    cb_gpu[0]&0xffffffff,cb_gpu[1]&0xffffffff,cb_gpu[2]&0xffffffff,cb_gpu[70]&0xffffffff,cb_gpu[71]&0xffffffff,cb_gpu[63],cb_gpu[62],cb_gpu[61]);at=put(out,at,cap,gpu,sizeof gpu);
  for(unsigned i=4;i<28;i+=12){int n=snprintf(gpu,sizeof gpu,"GPU%u gpucc",(i-4)/12+1);
   for(unsigned k=0;k<12 && i+k<28;++k){if(n<0||(size_t)n>sizeof gpu-12)n=(int)sizeof gpu-12;n+=snprintf(gpu+n,sizeof gpu-n," %08llx",cb_gpu[i+k]&0xffffffffull);}
   at=put(out,at,cap,gpu,sizeof gpu);}
  {int n=snprintf(gpu,sizeof gpu,"GPU3 gpu");
   for(unsigned k=0;k<17;++k){if(n<0||(size_t)n>sizeof gpu-12)n=(int)sizeof gpu-12;n+=snprintf(gpu+n,sizeof gpu-n," %08llx",cb_gpu[28+k]&0xffffffffull);}
   at=put(out,at,cap,gpu,sizeof gpu);}
  if (&native_smmu_irq[0]) {
   char irq[160];snprintf(irq,sizeof irq,"SIRQ count=%llu attach=%x rb=%x route=%llx id=%llu ready=%llu bid=%llx",
    native_smmu_irq[0],(unsigned)native_smmu_irq[1],(unsigned)native_smmu_irq[2],native_smmu_irq[3],native_smmu_irq[4],native_smmu_irq[5],cb_boot_id);
   at=put(out,at,cap,irq,sizeof irq);
  }
  {int n=snprintf(gpu,sizeof gpu,"GSMMU");
   for(unsigned k=0;k<15;++k){if(n<0||(size_t)n>sizeof gpu-12)n=(int)sizeof gpu-12;n+=snprintf(gpu+n,sizeof gpu-n," %08llx",cb_gpu[45+k]&0xffffffffull);}
   at=put(out,at,cap,gpu,sizeof gpu);}
  snprintf(gpu,sizeof gpu,"GPU6 hlos1=%08llx/%08llx hlos2=%08llx/%08llx d4=%08llx/%08llx ahb=%08llx bimc=%08llx",
    cb_gpu[64]&0xffffffffull,cb_gpu[65]&0xffffffffull,cb_gpu[66]&0xffffffffull,
    cb_gpu[67]&0xffffffffull,cb_gpu[68]&0xffffffffull,cb_gpu[69]&0xffffffffull,
    cb_gpu[0]&0xffffffffull,cb_gpu[1]&0xffffffffull);
  at=put(out,at,cap,gpu,sizeof gpu);
 }
 if (&cb_smmu[0] && cb_smmu[15]==0x5e7) {
  char sm[220];
  snprintf(sm,sizeof sm,"SSETUP st=%llu root=%llx sctlr=%llx smr=%llx s2cr=%llx cb=%llx tcr=%llx fsr=%llx ids=%llx/%llx/%llx",
    cb_smmu[0],cb_smmu[1],cb_smmu[2],cb_smmu[3],cb_smmu[4],cb_smmu[5],cb_smmu[8],cb_smmu[9],
    cb_smmu[10],cb_smmu[11],cb_smmu[12]);at=put(out,at,cap,sm,sizeof sm);
  snprintf(sm,sizeof sm,"SMICRO before=%llx after=%llx impl0=%llx result=%lld lt=%llu ht=%llu bid=%llx",
    cb_smmu[16],cb_smmu[17],cb_smmu[18],(long long)cb_smmu[19],cb_smmu[22],cb_smmu[23],cb_boot_id);
  at=put(out,at,cap,sm,sizeof sm);
  snprintf(sm,sizeof sm,"SMPOOL pa=%llx bytes=%llu used=%llu bid=%llx",
    cb_smmu[20],cb_smmu[21],cb_smmu[14],cb_boot_id);at=put(out,at,cap,sm,sizeof sm);
  if (&cb_map[0] && cb_map[0]!=0xdeadbeef) {
   snprintf(sm,sizeof sm,"SMAP st=%llu va=%llx pa=%llx pte=%llx",
     cb_map[0],cb_map[1],cb_map[2],cb_map[3]);at=put(out,at,cap,sm,sizeof sm);
  }
 }
 if (&cb_cp[0] && cb_cp[31]==0x0c9) {
  char cp[240];
  snprintf(cp,sizeof cp,"GCP0 st=%llu fw=%llu pm4=%llu pfp=%llu gpmu=%llu ring=%llu map=%llx r=%llx bid=%llx",
    cb_cp[0],cb_cp[1],cb_cp[2],cb_cp[3],cb_cp[4],cb_cp[5],cb_cp[8],cb_cp[30],cb_boot_id);at=put(out,at,cap,cp,sizeof cp);
  snprintf(cp,sizeof cp,"GCP1 pa ring=%llx pm4=%llx pfp=%llx gpmu=%llx gbytes=%llu",
    cb_cp[23],cb_cp[20],cb_cp[21],cb_cp[22],cb_cp[24]);at=put(out,at,cap,cp,sizeof cp);
  snprintf(cp,sizeof cp,"GCP2 me=%llx vbif=%llx/%llx rb=%llx/%llx/%llx/%llx st=%llx irq=%llx trust=%llx sec=%llx",
    cb_cp[9],cb_cp[10],cb_cp[11],cb_cp[12],cb_cp[13],cb_cp[14],cb_cp[15],cb_cp[16],cb_cp[17],cb_cp[18],cb_cp[19]);at=put(out,at,cap,cp,sizeof cp);
  snprintf(cp,sizeof cp,"GCP3 va ring=%llx pm4=%llx pfp=%llx gpmu=%llx fence=%llx",
    cb_cp[25],cb_cp[26],cb_cp[27],cb_cp[28],cb_cp[29]);at=put(out,at,cap,cp,sizeof cp);
  snprintf(cp,sizeof cp,"GCP4 gready=%llx gerr=%llx cm3rst=%llx prot=%llx swrst=%llx ahb=%llx bid=%llx",
    cb_cp[32],cb_cp[33],cb_cp[34],cb_cp[35],cb_cp[36],cb_cp[37],cb_boot_id);at=put(out,at,cap,cp,sizeof cp);
  if (cb_cp[38]) {
   snprintf(cp,sizeof cp,"GCP5 stage=%llu rc=%llx vbif=%llx me=%llx r=%llx w=%llx irq=%llx cpirq=%llx st=%llx st3=%llx bid=%llx",
     cb_cp[38],cb_cp[39],cb_cp[40],cb_cp[41],cb_cp[42],cb_cp[43],cb_cp[44],cb_cp[45],cb_cp[47],cb_cp[46],cb_boot_id);
   at=put(out,at,cap,cp,sizeof cp);
   snprintf(cp,sizeof cp,"GCP6 trust=%llx cpcnl=%llx cphw=%llx cpprot=%llx cphb=%llx smfsr=%llx smg=%llx bid=%llx",
     cb_cp[48],cb_cp[49],cb_cp[50],cb_cp[51],cb_cp[52],cb_cp[53],cb_cp[54],cb_boot_id);
   at=put(out,at,cap,cp,sizeof cp);
   snprintf(cp,sizeof cp,"GCP7 cx=%llx gx=%llx gfx=%llx st=%llx me=%llx vbif=%llx ry=%llu bid=%llx",
     cb_cp[56],cb_cp[57],cb_cp[58],cb_cp[59],cb_cp[60],cb_cp[61],cb_cp[62],cb_boot_id);
   at=put(out,at,cap,cp,sizeof cp);
   snprintf(cp,sizeof cp,"GCP8 me=%llx/%llx pfp=%llx/%llx rb=%llx/%llx ctl=%llx am=%llx prot=%llx bid=%llx",
     cb_cp[64],cb_cp[65],cb_cp[66],cb_cp[67],cb_cp[68],cb_cp[69],cb_cp[70],cb_cp[71],cb_cp[72],cb_boot_id);
   at=put(out,at,cap,cp,sizeof cp);
   snprintf(cp,sizeof cp,"GCP9 sctlr=%llx tcr=%llx ttbr=%llx/%llx smr=%llx s2cr=%llx pte=%llx bid=%llx",
     cb_cp[73],cb_cp[74],cb_cp[75],cb_cp[76],cb_cp[77],cb_cp[78],cb_cp[79],cb_boot_id);
   at=put(out,at,cap,cp,sizeof cp);
   snprintf(cp,sizeof cp,"GCP10 ttbr=%x/%x smr=%x s2cr=%x pte=%llx root=%llx ringpa=%llx fsr=%x gfsr=%x bid=%llx",
     (uint32_t)(cb_cp[75]),(uint32_t)(cb_cp[76]),(uint32_t)(cb_cp[77]),(uint32_t)(cb_cp[78]),
     cb_cp[79],cb_cp[80],cb_cp[81],(uint32_t)(cb_cp[53]),(uint32_t)(cb_cp[54]),cb_boot_id);
   at=put(out,at,cap,cp,sizeof cp);
   snprintf(cp,sizeof cp,"GCP11 rbccu=%llx/%llx sp=%llx/%llx smr2=%llx s2cr2=%llx cb1=%llx cb2=%llx bid=%llx",
     cb_cp[83],cb_cp[84],cb_cp[85],cb_cp[86],cb_cp[89],cb_cp[90],cb_cp[91],cb_cp[92],cb_boot_id);
   at=put(out,at,cap,cp,sizeof cp);
   snprintf(cp,sizeof cp,"GCP12 rptr=%llx cpctl=%llx ucode=%llx bid=%llx",
     cb_cp[93],cb_cp[94],cb_cp[95],cb_boot_id);
   at=put(out,at,cap,cp,sizeof cp);
   snprintf(cp,sizeof cp,"GCP13 gfsr=%llx s2smr=%llx s2s2cr=%llx cb1=%llx cb2=%llx cb0n=%llx/%llx rw=%llx/%llx fpte=%llx bid=%llx",
     cb_cp[54],cb_cp[89],cb_cp[90],cb_cp[91],cb_cp[92],cb_cp[103],cb_cp[104],
     cb_cp[106],cb_cp[105],cb_cp[108],cb_boot_id);
   at=put(out,at,cap,cp,sizeof cp);
   snprintf(cp,sizeof cp,"GCP14 atos par=%llx rc=%lld expect=%llx ctl=%llx haltctl=%llx atsr=%llx fsr=%llx fsynr=%llx far=%llx sctlr=%llx resume=%llx bid=%llx",
     cb_cp[109],(long long)cb_cp[110],cb_cp[111],cb_cp[112],cb_cp[113],cb_cp[114],
     cb_cp[115],cb_cp[116],cb_cp[117],cb_cp[119],cb_cp[118],cb_boot_id);
   at=put(out,at,cap,cp,sizeof cp);
   snprintf(cp,sizeof cp,"GCP15 fsrb=%llx fsynrb=%llx farb=%llx ttbr=%llx/%llx tcr=%llx root=%llx bid=%llx",
     cb_cp[120],cb_cp[121],cb_cp[122],cb_cp[123],cb_cp[124],cb_cp[125],cb_cp[126],cb_boot_id);
   at=put(out,at,cap,cp,sizeof cp);
   /* Kernel user-path control fence after the probe: rc/stage and the fence
    * page word (the CP's RPTR shadow shares dword 0). Only printed once the
    * control actually ran, so a zero slot cannot read as a success. */
   if (cb_cp[127]) {
    snprintf(cp,sizeof cp,"GCTL rc=%d stage=%llu fence=%llx bid=%llx",
      (int)(int16_t)(uint16_t)(cb_cp[127]&0xffffu),cb_cp[127]>>48,
      (cb_cp[127]>>16)&0xffffffffu,cb_boot_id);
    at=put(out,at,cap,cp,sizeof cp);
   }
   snprintf(cp,sizeof cp,"KEY bid=%llx clk=%x/%x/%x trust=%x stg=%llu rc=%llx r=%llx w=%llx cpfsr=%llx gfsr=%llx hberr=%u frames=%llu",
     cb_boot_id,(uint32_t)(cb_cp[56]),(uint32_t)(cb_cp[57]),(uint32_t)(cb_cp[58]),(uint32_t)(cb_cp[48]),
     cb_cp[38],cb_cp[39],cb_cp[42],cb_cp[43],(uint32_t)(cb_cp[53]),(uint32_t)(cb_cp[54]),
     (unsigned)boot_log_errors,(unsigned long long)p20_display_frames);
   at=put(out,at,cap,cp,sizeof cp);
  }
 }
 if (&native_adreno_service_stats[0] && native_adreno_service_stats[0]!=0xdeadbeef) {
  /* User-client submission (sheng NativeAdreno ABI): opens/closes, BOs,
   * maps, submits, fences, waits, errors, last status/selector, BO bytes,
   * pages, metadata bytes and calls. The daemon's own result line follows. */
  char svc[288];
  snprintf(svc,sizeof svc,"GPSVC ready=%d open=%llu close=%llu bo=%llu free=%llu map=%llu unmap=%llu sub=%llu fence=%llu wait=%llu err=%llu st=%llx sel=%llx bytes=%llu pages=%llu meta=%llu calls=%llu uid=%llx bid=%llx",
    (int)native_adreno_service_ready,
    native_adreno_service_stats[0],native_adreno_service_stats[1],native_adreno_service_stats[2],
    native_adreno_service_stats[3],native_adreno_service_stats[4],native_adreno_service_stats[5],
    native_adreno_service_stats[6],native_adreno_service_stats[8],native_adreno_service_stats[7],
    native_adreno_service_stats[9],native_adreno_service_stats[10],native_adreno_service_stats[11],
    native_adreno_service_stats[12],native_adreno_service_stats[13],native_adreno_service_stats[14],
    native_adreno_service_stats[15],&native_adreno_client_uid?native_adreno_client_uid:~0ull,cb_boot_id);
  at=put(out,at,cap,svc,sizeof svc);
  char txt[112];
  snprintf(txt,sizeof txt,"GPSTXT %s bid=%llx",native_adreno_service_text,cb_boot_id);
  at=put(out,at,cap,txt,sizeof txt);
  char v2[160];
  snprintf(v2,sizeof v2,"GPSV2 markers=%llu last=%llx mapopt=%llx refused=%llu bid=%llx",
    native_adreno_service_stats[16],native_adreno_service_stats[17],
    native_adreno_service_stats[18],native_adreno_service_stats[19],cb_boot_id);
  at=put(out,at,cap,v2,sizeof v2);
  char pr[160];
  snprintf(pr,sizeof pr,"GPROBE exec=%llu markers=%llu refused=%llu open=%llu calls=%llu bid=%llx",
    &native_adreno_probe_exec?native_adreno_probe_exec:0,
    native_adreno_service_stats[16],native_adreno_service_stats[19],
    native_adreno_service_stats[0],native_adreno_service_stats[15],cb_boot_id);
  at=put(out,at,cap,pr,sizeof pr);
 }
 if (&cb_gpu_paths[0] && cb_gpu_paths[7]==0x5a5a) {
  /* Existence probes for the launchd and kext directories (0 exists, positive
   * errno, 0xdeadbeef not probed): plain paths and the SystemOS cryptex. */
  char jobs[200];
  snprintf(jobs,sizeof jobs,"GJOBS ld=%lld ldb=%lld cx=%lld cxb=%lld ext=%lld extx=%lld pre=%lld bid=%llx",
    (long long)cb_gpu_paths[0],(long long)cb_gpu_paths[1],(long long)cb_gpu_paths[2],
    (long long)cb_gpu_paths[3],(long long)cb_gpu_paths[4],(long long)cb_gpu_paths[5],
    (long long)cb_gpu_paths[6],cb_boot_id);
  at=put(out,at,cap,jobs,sizeof jobs);
 }
 if (&native_gpu_addon_files) {
  /* GPU addon install/mount status from the boot-time exec hook: files found
   * and written, the Extensions (plugin) mount, LaunchDaemons mount and the
   * wallpaper overlay. A negative status is the errno of the refused step. */
  char add[200];
  snprintf(add,sizeof add,"GADDON files=%d written=%d same=%d failed=%d err=%d ext=%d/%d daem=%d/%d wp=%d/%d bid=%llx",
    (int)native_gpu_addon_files,(int)native_gpu_addon_written,(int)native_gpu_addon_same,
    (int)native_gpu_addon_failed,(int)native_gpu_addon_error,(int)native_gpu_addon_stage,
    (int)native_gpu_addon_status,(int)native_gpu_addon2_stage,(int)native_gpu_addon2_status,
    (int)native_gpu_addon3_stage,(int)native_gpu_addon3_status,cb_boot_id);
  at=put(out,at,cap,add,sizeof add);
  char add2[160];
  snprintf(add2,sizeof add2,"GADDON2 flags=%llx orig=%d own=%d rb=%d bid=%llx",
    &native_gpu_addon_mount_flags?native_gpu_addon_mount_flags:0,
    &native_gpu_addon_orig?(int)native_gpu_addon_orig:-1,
    &native_gpu_addon_own?(int)native_gpu_addon_own:-1,
    &native_gpu_addon_rollback?(int)native_gpu_addon_rollback:-1,cb_boot_id);
  at=put(out,at,cap,add2,sizeof add2);
  char add3[220];
  snprintf(add3,sizeof add3,"GADDON3 data=%llx/%llx/%llx mnt=%llx/%llx/%llx orig=%llx/%llx/%llx boot=%d bid=%llx",
    &native_gpu_addon_data_stat[0]?native_gpu_addon_data_stat[0]:~0ull,
    &native_gpu_addon_data_stat[0]?native_gpu_addon_data_stat[1]:~0ull,
    &native_gpu_addon_data_stat[0]?native_gpu_addon_data_stat[2]:~0ull,
    &native_gpu_addon_mount_stat[0]?native_gpu_addon_mount_stat[0]:~0ull,
    &native_gpu_addon_mount_stat[0]?native_gpu_addon_mount_stat[1]:~0ull,
    &native_gpu_addon_mount_stat[0]?native_gpu_addon_mount_stat[2]:~0ull,
    &native_gpu_addon_orig_stat[0]?native_gpu_addon_orig_stat[0]:~0ull,
    &native_gpu_addon_orig_stat[0]?native_gpu_addon_orig_stat[1]:~0ull,
    &native_gpu_addon_orig_stat[0]?native_gpu_addon_orig_stat[2]:~0ull,
    &native_gpu_addon_boot_mount?(int)native_gpu_addon_boot_mount:-1,cb_boot_id);
  at=put(out,at,cap,add3,sizeof add3);
  if (&native_gpu_plugin_reads[0]) {
   /* The original discovery reads MetalPluginName, then the plugin class and
    * the sandbox grant name. All zero means it never looked at this accelerator. */
   char plug[120];
   snprintf(plug,sizeof plug,"GPLUG mpn=%llu cls=%llu grant=%llu bid=%llx",
     native_gpu_plugin_reads[0],native_gpu_plugin_reads[1],native_gpu_plugin_reads[2],cb_boot_id);
   at=put(out,at,cap,plug,sizeof plug);
  }
  if (&native_adreno_submit_rc) {
   /* The first real submit failure: internal rc/stage and the first captured
    * error IRQs, so the generic user-space IOReturn is not the only record. */
   char sub[192];
   snprintf(sub,sizeof sub,"GSUB rc=%d stage=%u irq=%x cpirq=%x pre=%x/%x fenced=%u seq=%u post=%u pf=%x/%u fr=%x stg=%u bid=%llx",
     (int)native_adreno_submit_rc,native_adreno_submit_stage,native_adreno_submit_irq,
     native_adreno_submit_cpirq,
     &native_adreno_submit_preirq?native_adreno_submit_preirq:0,
     &native_adreno_submit_precpirq?native_adreno_submit_precpirq:0,
     &native_adreno_submit_fenced?native_adreno_submit_fenced:0,
     native_adreno_submit_seq,native_adreno_submit_post,
     &native_adreno_submit_prefence?native_adreno_submit_prefence:0xdeadbeefu,
     &native_adreno_submit_prefence_valid?native_adreno_submit_prefence_valid:0,
     &native_adreno_submit_fence_raw?native_adreno_submit_fence_raw:0xdeadbeefu,
     &native_adreno_submit_staged?native_adreno_submit_staged:0xdeadbeefu,
     cb_boot_id);
   at=put(out,at,cap,sub,sizeof sub);
   /* Staged gate: A ring-inline write, B the same write through an indirect
    * buffer, C a CP-side read of the client's page. Each shows its rc/stage
    * and sentinels; cp vs kv are the CP's and the kernel's first client word. */
   if (&native_adreno_gate[0] && (native_adreno_gate[0]|native_adreno_gate[5]|native_adreno_gate[10]|
       native_adreno_gate[18]|native_adreno_gate[20])) {
    char gate[220];
    snprintf(gate,sizeof gate,"GATE k0=%d/%u k1=%d/%u a=%d/%u %x/%x/%x b=%d/%u %x/%x/%x c=%d/%u cp=%x/%x kv=%x bid=%llx",
      (int)(int16_t)(uint16_t)native_adreno_gate[18],native_adreno_gate[19],
      (int)(int16_t)(uint16_t)native_adreno_gate[20],native_adreno_gate[21],
      (int)(int16_t)(uint16_t)native_adreno_gate[0],native_adreno_gate[1],
      native_adreno_gate[2],native_adreno_gate[3],native_adreno_gate[4],
      (int)(int16_t)(uint16_t)native_adreno_gate[5],native_adreno_gate[6],
      native_adreno_gate[7],native_adreno_gate[8],native_adreno_gate[9],
      (int)(int16_t)(uint16_t)native_adreno_gate[10],native_adreno_gate[11],
      native_adreno_gate[12],native_adreno_gate[13],native_adreno_gate[16],cb_boot_id);
    at=put(out,at,cap,gate,sizeof gate);
   }
   /* A540 2D path: the CP_BLIT clear of the offscreen target and the optional
    * scanout band. `match` compares each quadrant with the value the stream
    * wrote; edge is a sample inside quadrant 0 near its right edge. */
   if (&native_adreno_clear[0] && native_adreno_clear[2]) {
    char clr[320];
    snprintf(clr,sizeof clr,"CLR rc=%d seq=%u w=%u px=%x/%x/%x/%x edge=%x match=%u/4 scan=%d/%u sm=%u/4 spx=%x/%x/%x/%x prep=%d pe=%u/%u/%u/%u ib=%x tgt=%x bid=%llx",
      (int)(int16_t)(uint16_t)native_adreno_clear[0],native_adreno_clear[1],native_adreno_clear[2],
      native_adreno_clear[3],native_adreno_clear[4],native_adreno_clear[5],native_adreno_clear[6],
      native_adreno_clear[13],native_adreno_clear[7],
      (int)(int16_t)(uint16_t)native_adreno_clear[10],native_adreno_clear[11],native_adreno_clear[12],
      native_adreno_clear[14],native_adreno_clear[15],native_adreno_clear[16],native_adreno_clear[17],
      (int)(int16_t)(uint16_t)native_adreno_clear[18],
      native_adreno_clear[20],native_adreno_clear[21],native_adreno_clear[22],native_adreno_clear[23],
      native_adreno_clear[8],native_adreno_clear[9],cb_boot_id);
    at=put(out,at,cap,clr,sizeof clr);
   }
   /* GPU frame handed to the display pipe: the four bands were rendered by
    * the 2D engine at stride 4352 (the firmware GOP's 4320 is not 64-byte
    * aligned, so the engines cannot write it directly). drc is the display
    * driver's flip return; pipe/ctl identify the MDP pipe it switched. */
   if (&native_adreno_flip[0] && (native_adreno_flip[0] || native_adreno_flip[1])) {
    char flip[200];
    snprintf(flip,sizeof flip,"FLIP rc=%d pa=%x stride=%u px=%x/%x/%x/%x drc=%d pipe=%x ctl=%x bid=%llx",
      (int)(int16_t)(uint16_t)native_adreno_flip[0],native_adreno_flip[1],native_adreno_flip[2],
      native_adreno_flip[3],native_adreno_flip[4],native_adreno_flip[5],native_adreno_flip[6],
      (int)(int16_t)(uint16_t)native_adreno_flip[7],native_adreno_flip[8],native_adreno_flip[9],
      cb_boot_id);
    at=put(out,at,cap,flip,sizeof flip);
   }
   /* Compute shader band on the firmware scanout: the SSBO store is byte
    * addressed, so it writes the real 4320-byte pitch that the 2D/3D engines
    * cannot encode. Samples are id<<8 at four positions of the band. */
   if (&native_adreno_compute[0] && (native_adreno_compute[0] || native_adreno_compute[2])) {
    char cmpt[200];
    snprintf(cmpt,sizeof cmpt,"CMPT rc=%d seq=%u w=%u px=%x/%x/%x/%x match=%u/4 band=%x bytes=%u l=%u g=%u bid=%llx",
      (int)(int16_t)(uint16_t)native_adreno_compute[0],native_adreno_compute[1],native_adreno_compute[2],
      native_adreno_compute[3],native_adreno_compute[4],native_adreno_compute[5],native_adreno_compute[6],
      native_adreno_compute[7],native_adreno_compute[8],native_adreno_compute[9],
      native_adreno_compute[10],native_adreno_compute[11],cb_boot_id);
    at=put(out,at,cap,cmpt,sizeof cmpt);
   }
   /* CS variant matrix: late/immediate dword0 of the private probe page per
    * variant and its dispatch rc. A page that stays zero means that CS
    * configuration launched no writing shader. */
   if (&native_adreno_cprobe[0] && (native_adreno_cprobe[8]|native_adreno_cprobe[9]|
       native_adreno_cprobe[10]|native_adreno_cprobe[11]|native_adreno_cprobe[12]|
       native_adreno_cprobe[13]|native_adreno_cprobe[14])) {
    char cpr[260];
    snprintf(cpr,sizeof cpr,"CPRB late=%x/%x/%x/%x/%x/%x/%x imm=%x/%x/%x/%x/%x/%x/%x rc=%d/%d/%d/%d/%d/%d/%d bid=%llx",
      native_adreno_cprobe[0],native_adreno_cprobe[1],native_adreno_cprobe[2],
      native_adreno_cprobe[3],native_adreno_cprobe[4],native_adreno_cprobe[5],
      native_adreno_cprobe[6],
      native_adreno_cprobe[16],native_adreno_cprobe[17],native_adreno_cprobe[18],
      native_adreno_cprobe[19],native_adreno_cprobe[20],native_adreno_cprobe[21],
      native_adreno_cprobe[22],
      (int)(int16_t)(uint16_t)native_adreno_cprobe[8],(int)(int16_t)(uint16_t)native_adreno_cprobe[9],
      (int)(int16_t)(uint16_t)native_adreno_cprobe[10],(int)(int16_t)(uint16_t)native_adreno_cprobe[11],
      (int)(int16_t)(uint16_t)native_adreno_cprobe[12],(int)(int16_t)(uint16_t)native_adreno_cprobe[13],
      (int)(int16_t)(uint16_t)native_adreno_cprobe[14],cb_boot_id);
    at=put(out,at,cap,cpr,sizeof cpr);
   }
   if (&native_adreno_copy2d[0] && (native_adreno_copy2d[0] || native_adreno_copy2d[2])) {
    char cpy[220];
    snprintf(cpy,sizeof cpy,"CPY rc=%d seq=%u w=%u a=%x/%x/%x/%x am=%u/4 b=%x/%x/%x/%x bm=%u/4 pa=%u pb=%u bid=%llx",
      (int)(int16_t)(uint16_t)native_adreno_copy2d[0],native_adreno_copy2d[1],native_adreno_copy2d[2],
      native_adreno_copy2d[3],native_adreno_copy2d[4],native_adreno_copy2d[5],native_adreno_copy2d[6],
      native_adreno_copy2d[11],
      native_adreno_copy2d[7],native_adreno_copy2d[8],native_adreno_copy2d[9],native_adreno_copy2d[10],
      native_adreno_copy2d[12],native_adreno_copy2d[13],native_adreno_copy2d[14],cb_boot_id);
    at=put(out,at,cap,cpy,sizeof cpy);
   }
   if (&native_adreno_highpa[0] && (native_adreno_highpa[0] || native_adreno_highpa[1])) {
    char hpa[200];
    snprintf(hpa,sizeof hpa,"HPA rc=%d seq=%u min=%x%08x max=%x%08x high=%u px=%x/%x/%x/%x match=%u/4 bid=%llx",
      (int)(int16_t)(uint16_t)native_adreno_highpa[0],native_adreno_highpa[1],
      native_adreno_highpa[3],native_adreno_highpa[2],
      native_adreno_highpa[5],native_adreno_highpa[4],native_adreno_highpa[6],
      native_adreno_highpa[8],native_adreno_highpa[9],native_adreno_highpa[10],
      native_adreno_highpa[11],native_adreno_highpa[7],cb_boot_id);
    at=put(out,at,cap,hpa,sizeof hpa);
   }
   if (&native_adreno_highcp[0] && (native_adreno_highcp[0]|native_adreno_highcp[1]|native_adreno_highcp[4])) {
    char hcp[160];
    snprintf(hcp,sizeof hcp,"HCP ib=%d/%u sent=%x/%x src=%d/%u val=%x pa=%x bid=%llx",
      (int)(int16_t)(uint16_t)native_adreno_highcp[0],native_adreno_highcp[1],
      native_adreno_highcp[2],native_adreno_highcp[3],
      (int)(int16_t)(uint16_t)native_adreno_highcp[4],native_adreno_highcp[5],
      native_adreno_highcp[6],native_adreno_highcp[7],cb_boot_id);
    at=put(out,at,cap,hcp,sizeof hcp);
   }
   if (&native_adreno_bigbo[0] && (native_adreno_bigbo[0]|native_adreno_bigbo[1]|native_adreno_bigbo[2])) {
    char big[200];
    snprintf(big,sizeof big,"BIG rc=%d seq=%u w=%u runs=%u min=%x%08x max=%x%08x px=%x/%x/%x/%x match=%u/4 bid=%llx",
      (int)(int16_t)(uint16_t)native_adreno_bigbo[0],native_adreno_bigbo[1],native_adreno_bigbo[2],
      native_adreno_bigbo[3],native_adreno_bigbo[5],native_adreno_bigbo[4],
      native_adreno_bigbo[7],native_adreno_bigbo[6],
      native_adreno_bigbo[9],native_adreno_bigbo[10],native_adreno_bigbo[11],
      native_adreno_bigbo[12],native_adreno_bigbo[8],cb_boot_id);
    at=put(out,at,cap,big,sizeof big);
   }
   if (&native_adreno_surface[0] && (native_adreno_surface[0]|native_adreno_surface[1]|native_adreno_surface[2])) {
    char srf[200];
    snprintf(srf,sizeof srf,"SURF rc=%d seq=%u w=%u runs=%u stride=%u fmt=%x px=%x/%x/%x/%x match=%u/4 bid=%llx",
      (int)(int16_t)(uint16_t)native_adreno_surface[0],native_adreno_surface[1],native_adreno_surface[2],
      native_adreno_surface[3],native_adreno_surface[4],native_adreno_surface[6],
      native_adreno_surface[8],native_adreno_surface[9],native_adreno_surface[10],native_adreno_surface[11],
      native_adreno_surface[7],cb_boot_id);
    at=put(out,at,cap,srf,sizeof srf);
   }
   /* Last submission as the CP reads it: seq, start, end and the first words
    * of the stream, so a fence that never fires can be compared with what was
    * actually placed in the ring. */
   if (&native_adreno_ring_log540_count && native_adreno_ring_log540_count) {
    const volatile uint32_t *ring=&native_adreno_ring_log540[(native_adreno_ring_log540_count-1)&3][0];
    char rgl[200];
    snprintf(rgl,sizeof rgl,"GRNG seq=%u %u..%u n=%u w=%x/%x/%x/%x/%x/%x %x/%x/%x/%x bid=%llx",
      ring[0],ring[1],ring[2],ring[3],ring[4],ring[5],ring[6],ring[7],ring[8],ring[9],
      ring[10],ring[11],ring[12],ring[13],cb_boot_id);
    at=put(out,at,cap,rgl,sizeof rgl);
   }
   /* Hardware walk of the user IB before its doorbell: -3 is a translated
    * page that does not match the software leaf, other codes are ATOS faults. */
   if (&native_adreno_submit_atos_rc && (native_adreno_submit_atos_rc || native_adreno_submit_atos_expect)) {
    char sat[176];
    snprintf(sat,sizeof sat,"GSAT atos=%d par=%llx expect=%llx pte=%llx bid=%llx",
      (int)native_adreno_submit_atos_rc,native_adreno_submit_atos_par,
      native_adreno_submit_atos_expect,native_adreno_submit_atos_pte,cb_boot_id);
    at=put(out,at,cap,sat,sizeof sat);
   }
   if (&native_adreno_ib_head[0] && native_adreno_ib_head[0]) {
    char ibh[160];
    snprintf(ibh,sizeof ibh,"GIBH %x %x %x %x %x %x %x %x bid=%llx",
      native_adreno_ib_head[0],native_adreno_ib_head[1],native_adreno_ib_head[2],
      native_adreno_ib_head[3],native_adreno_ib_head[4],native_adreno_ib_head[5],
      native_adreno_ib_head[6],native_adreno_ib_head[7],cb_boot_id);
    at=put(out,at,cap,ibh,sizeof ibh);
   }
   /* Late kernel-page write control, same allocation and moment as a user BO
    * but without the user mapping. */
   if (&native_adreno_late_test[0] && native_adreno_late_test[1]) {
    char lte[192];
    snprintf(lte,sizeof lte,"GLTE rc=%d pa=%llx map=%u tgt=%x/%x/%x seq=%u bid=%llx",
      (int)(int16_t)(uint16_t)native_adreno_late_test[0],
      ((uint64_t)native_adreno_late_test[2]<<32)|native_adreno_late_test[1],
      native_adreno_late_test[6],native_adreno_late_test[3],native_adreno_late_test[4],
      native_adreno_late_test[5],native_adreno_late_test[7],cb_boot_id);
    at=put(out,at,cap,lte,sizeof lte);
   }
   /* CP_MEM_TO_MEM read of the write target's page: rc, sequence, the copied
    * words and the source address. A successful read with 0xdeadbeef proves
    * the CP sees user pages while their GPU writes still fail. */
   if (&native_adreno_copy_test[0] && native_adreno_copy_test[4]) {
    char cpy[176];
    snprintf(cpy,sizeof cpy,"GCPY rc=%d seq=%u src=%x dst=%x/%x bid=%llx",
      (int)(int16_t)(uint16_t)native_adreno_copy_test[0],native_adreno_copy_test[1],
      native_adreno_copy_test[4],native_adreno_copy_test[2],native_adreno_copy_test[3],cb_boot_id);
    at=put(out,at,cap,cpy,sizeof cpy);
   }
   /* Hardware translation of every mapped BO, including the write target that
    * is never part of the submission. */

  if (&native_adreno_tunnel[0] && (native_adreno_tunnel[0]|native_adreno_tunnel[1])) {
   char tun[96];
   snprintf(tun,sizeof tun,"GTUN calls=%llu probes=%llu bid=%llx",
     (unsigned long long)native_adreno_tunnel[0],(unsigned long long)native_adreno_tunnel[1],cb_boot_id);
   at=put(out,at,cap,tun,sizeof tun);
  }
  if (&native_adreno_bo_atos[0] && native_adreno_bo_atos[16]) {
    unsigned rows=native_adreno_bo_atos[16]<2?native_adreno_bo_atos[16]:2;
    for (unsigned i=0;i<rows;++i) {
     const volatile uint32_t *r=&native_adreno_bo_atos[i*8];
     char boa[192];
     snprintf(boa,sizeof boa,"GBOA%u iova=%x pa=%llx pte=%llx rc=%d par=%llx bid=%llx",
       i,r[0],((uint64_t)r[2]<<32)|r[1],((uint64_t)r[4]<<32)|r[3],
       (int)(int16_t)(uint16_t)r[7],((uint64_t)r[6]<<32)|r[5],cb_boot_id);
     at=put(out,at,cap,boa,sizeof boa);
    }
   }
   /* BO content read back by the kernel after the submission: slot and the
    * first three words, so a memory write that landed without a fence shows. */
   if (&native_adreno_bo_dump[0] && native_adreno_bo_dump[15]) {
    char bod[200];
    snprintf(bod,sizeof bod,"GBOD n=%u %u:%x/%x/%x %u:%x/%x/%x %u:%x/%x/%x bid=%llx",
      native_adreno_bo_dump[15],
      native_adreno_bo_dump[0],native_adreno_bo_dump[1],native_adreno_bo_dump[2],native_adreno_bo_dump[3],
      native_adreno_bo_dump[4],native_adreno_bo_dump[5],native_adreno_bo_dump[6],native_adreno_bo_dump[7],
      native_adreno_bo_dump[8],native_adreno_bo_dump[9],native_adreno_bo_dump[10],native_adreno_bo_dump[11],
      cb_boot_id);
    at=put(out,at,cap,bod,sizeof bod);
   }
  }
  if (&native_adreno_selftest[0] && native_adreno_selftest[6]) {
   /* Kernel IB self-test in a page the CP already fetched: rc/stage, the fence
    * word and the three sentinel words the IB was told to write. */
   char sib[160];
   snprintf(sib,sizeof sib,"GSIB rc=%d stage=%u fenced=%u fence=%x tgt=%x/%x/%x ib=%x bid=%llx",
     (int)native_adreno_selftest[0],native_adreno_selftest[1],native_adreno_selftest[5],
     native_adreno_selftest[7],native_adreno_selftest[2],native_adreno_selftest[3],
     native_adreno_selftest[4],native_adreno_selftest[6],cb_boot_id);
   at=put(out,at,cap,sib,sizeof sib);
  }
  if (&native_adreno_pa_test[0] && native_adreno_pa_test[1]) {
   /* Write test aimed at a user-owned physical page through the proven
    * window: map rc, physical page, sequence and submit result. */
   char pat[160];
   snprintf(pat,sizeof pat,"GPAT map=%d pa=%llx seq=%u rc=%d bid=%llx",
     (int)(int16_t)(uint16_t)native_adreno_pa_test[0],
     ((uint64_t)native_adreno_pa_test[2]<<32)|native_adreno_pa_test[1],
     native_adreno_pa_test[3],(int)(int16_t)(uint16_t)native_adreno_pa_test[4],cb_boot_id);
   at=put(out,at,cap,pat,sizeof pat);
  }
  if (&native_adreno_rtmap[0] && native_adreno_rtmap[7]) {
   /* Buffer mapped after the CP started, written by a known IB: a failure
    * here means runtime mappings never reached the CP's DMA. */
   char rtm[192];
   snprintf(rtm,sizeof rtm,"GRTM rc=%d map=%d pte=%llx tgt=%x/%x/%x iova=%x bid=%llx",
     (int)(int16_t)(uint16_t)native_adreno_rtmap[0],(int)(int16_t)(uint16_t)native_adreno_rtmap[1],
     ((uint64_t)native_adreno_rtmap[6]<<32)|native_adreno_rtmap[5],
     native_adreno_rtmap[2],native_adreno_rtmap[3],native_adreno_rtmap[4],
     native_adreno_rtmap[7],cb_boot_id);
   at=put(out,at,cap,rtm,sizeof rtm);
  }
  if (&native_adreno_rtmap2[0] && native_adreno_rtmap2[7]) {
   /* Same runtime test, but mapped inside the user window. */
   char rt2[192];
   snprintf(rt2,sizeof rt2,"GRT2 rc=%d map=%d pte=%llx tgt=%x/%x/%x iova=%x bid=%llx",
     (int)(int16_t)(uint16_t)native_adreno_rtmap2[0],(int)(int16_t)(uint16_t)native_adreno_rtmap2[1],
     ((uint64_t)native_adreno_rtmap2[6]<<32)|native_adreno_rtmap2[5],
     native_adreno_rtmap2[2],native_adreno_rtmap2[3],native_adreno_rtmap2[4],
     native_adreno_rtmap2[7],cb_boot_id);
   at=put(out,at,cap,rt2,sizeof rt2);
  }
  if (&native_adreno_pwr[0]) {
   /* Pre-kick core power and CP liveness, sampled while the bus was healthy. */
   char pw[224];
   snprintf(pw,sizeof pw,"GPWR pre=%x/%x sp=%x/%x post=%x/%x me/vbif=%x st=%x micro=%x/%d gpmu=%x/%x cpirq=%x irq=%x bid=%llx",
     native_adreno_pwr[0],native_adreno_pwr[1],native_adreno_pwr[2],native_adreno_pwr[3],
     native_adreno_pwr[4],native_adreno_pwr[5],native_adreno_pwr[6],native_adreno_pwr[7],
     native_adreno_pwr[8],(int)native_adreno_pwr[9],native_adreno_pwr[10],native_adreno_pwr[11],
     native_adreno_pwr[12],native_adreno_pwr[13],cb_boot_id);
   at=put(out,at,cap,pw,sizeof pw);
  }
  if (&native_adreno_client_name[0]) {
   char opener[96];
   snprintf(opener,sizeof opener,"GOPEN name=%s uid=%llx ostg=%d abi=%llu bid=%llx",
     native_adreno_client_name,&native_adreno_client_uid?native_adreno_client_uid:~0ull,
     &native_adreno_open_stage?native_adreno_open_stage:-1,
     &native_adreno_abi_opens?native_adreno_abi_opens:~0ull,cb_boot_id);
   at=put(out,at,cap,opener,sizeof opener);
  }
  if (&native_gpu_addon_cache_stage) {
   /* The system domain's daemons come from the sealed service cache; this row
    * shows whether the owner job was merged into a copy and whether the union
    * that serves it was verified and kept. */
   char cache[240];
   snprintf(cache,sizeof cache,"GCACHE stage=%d err=%d in=%llu out=%llu obj=%d dmn=%d ldr=%d mnt=%d/%d rb=%d bid=%llx",
     (int)native_gpu_addon_cache_stage,(int)native_gpu_addon_cache_status,
     &native_gpu_addon_cache_input?native_gpu_addon_cache_input:0,
     &native_gpu_addon_cache_output?native_gpu_addon_cache_output:0,
     &native_gpu_addon_cache_objects?(int)native_gpu_addon_cache_objects:-1,
     &native_gpu_addon_cache_daemons?(int)native_gpu_addon_cache_daemons:-1,
     &native_gpu_addon_cache_loader?(int)native_gpu_addon_cache_loader:-1,
     (int)native_gpu_addon_cache_mount_stage,(int)native_gpu_addon_cache_mount_status,
     &native_gpu_addon_cache_rollback?(int)native_gpu_addon_cache_rollback:-1,cb_boot_id);
   at=put(out,at,cap,cache,sizeof cache);
  }
 }
 if (&cb_zap[0] && cb_zap[0]!=0xdeadbeef && cb_zap[1]) {
  char zap[240];
  snprintf(zap,sizeof zap,"ZAP0 rc=%llx md=%llx img=%llx al=%llx m=%llu i=%llx/%llx s=%llx/%llx a=%llx/%llx t=%llx->%llx sup=%llx bid=%llx",
    cb_zap[0],cb_zap[9],cb_zap[10],cb_zap[6],cb_zap[11],cb_zap[19],cb_zap[20],
    cb_zap[22],cb_zap[23],cb_zap[25],cb_zap[26],cb_zap[30],cb_zap[31],cb_zap[17],cb_boot_id);
  at=put(out,at,cap,zap,sizeof zap);
 }
 if (&cb_mapret[0] && cb_mapret[2]) {
  char md[200];
  snprintf(md,sizeof md,"MAPD stage=%llu info=%llx iova=%llx pa=%llx ret=%llu tlb=%llx bid=%llx",
    cb_mapdiag[0],cb_mapdiag[1],cb_mapdiag[2],cb_mapdiag[3],cb_mapret[0],cb_mapret[1],cb_boot_id);at=put(out,at,cap,md,sizeof md);
 }
 if (&cb_reset[0] && cb_reset[0]) {
  char rst[200];
  snprintf(rst,sizeof rst,"AUTORESET requested=%llu elapsed=%llu main=%llu mrc=%llx mstart=%llu gstart=%llu gfire=%llu grc=%llx mhold=%llu ghold=%llu",
    cb_reset[1],cb_reset[2],cb_reset[3],cb_reset[4],cb_reset[5],cb_reset[6],cb_reset[7],cb_reset[8],cb_reset[9],cb_reset[10]);
  at=put(out,at,cap,rst,sizeof rst);
 }
 if (&cb_rails[0] && cb_rails[0]) {
  char rails[200];
  snprintf(rails,sizeof rails,"PMIC entries=%llu sid0=%llu pages=%llu core0=%llx core1=%llx",
    cb_rails[1],cb_rails[2],cb_rails[3],cb_pmic_dump[0],cb_pmic_dump[1]);
  at=put(out,at,cap,rails,sizeof rails);
  for(unsigned i=8;i<8+cb_rails[2] && i<20;i+=6)snprintf(rails,sizeof rails,"PMIC sid0ppid %llx %llx %llx %llx %llx %llx",
      cb_pmic_dump[i],cb_pmic_dump[i+1],cb_pmic_dump[i+2],cb_pmic_dump[i+3],cb_pmic_dump[i+4],cb_pmic_dump[i+5]),
    at=put(out,at,cap,rails,sizeof rails);
  for(unsigned i=0;i<8;++i){uint64_t v=cb_pmic_pages[i];if(!v)continue;
    snprintf(rails,sizeof rails,"PMIC page ppid=%llx vset=%02llx en=%02llx",v&0xfffull,(v>>12)&0xffull,(v>>20)&0xffull);
    at=put(out,at,cap,rails,sizeof rails);}
 }
 at=put(out,at,cap,"[display-clients-live]",32);
 char clients[6][91],aks[2][91];p21_client_rows(clients);p21_aks_rows(aks);
 for(unsigned i=0;i<6;++i)if(clients[i][0])at=put(out,at,cap,clients[i],91);
 for(unsigned i=0;i<2;++i)if(aks[i][0])at=put(out,at,cap,aks[i],91);
 p21_vsync_row(display_report,sizeof(display_report));at=put_report(out,at,cap,display_report,sizeof(display_report));
 at=put(out,at,cap,"[first-ui-faults]",32);
 for(unsigned i=0;i<24;++i)if(p20_ui_rows[i][0])at=put(out,at,cap,p20_ui_rows[i],91);
 at=put(out,at,cap,"[exits]",16);for(unsigned i=0;i<5;++i)at=put(out,at,cap,p20_exit_rows[i],120);
 at=put(out,at,cap,"[processes]",16);for(unsigned i=0;i<24;++i)at=put(out,at,cap,boot_process_rows[i],120);
 at=put(out,at,cap,"[ui-thread-stacks-live-bounded-24x16]",48);
 for(unsigned i=0;i<360;++i)if(p21_wait_rows[i][0])at=put(out,at,cap,p21_wait_rows[i],91);
 at=put(out,at,cap,"[iokit-service-matching]",32);
 p21_match_dump(match_report,sizeof match_report);at=put_report(out,at,cap,match_report,sizeof match_report);
 at=put(out,at,cap,"[official-component-reports]",32);
 size_t official_n=compose_official(official_report,sizeof official_report,slot,now);
 at=put_report(out,at,cap,official_report,official_n);
 at=platform_reports(out,at,cap);
 at=put(out,at,cap,"[coverage] absent weak exports mean component not selected; live samples are not atomic",100);
 at=put(out,at,cap,"[visible-console-tail]",32);
 unsigned end=__atomic_load_n(&p8_console_count,__ATOMIC_RELAXED),n=end<8192?end:8192;
 for(unsigned i=end-n;i<end && at+1<cap;++i){unsigned char c=p8_console_ring[i%32768];if(c==10||c==13||c==9||(c>=32&&c<127))out[at++]=c;}
 out[at]=0;return at;
}

// The message buffer, oldest byte first, NUL-padded to LogMsgBlocks blocks; returns the bytes copied.
static uint32_t copy_msgbuf(uint8_t *out) {
  memset(out, 0, LogMsgBlocks * LogBlock);
  struct boot_log_msgbuf *m = msgbufp;
  if (!m || !m->msg_bufc || m->msg_size <= 0) return 0;
  int size = m->msg_size, x = m->msg_bufx;
  if (x < 0 || x >= size) x = 0;
  uint32_t n = 0;
  for (int i = 0; i < size && n < LogMsgBlocks * LogBlock; ++i) {
    char c = m->msg_bufc[(x + i) % size];
    if (c) out[n++] = (uint8_t)c;
  }
  return n;
}

static void writer(void *arg, wait_result_t result) {
  (void)arg; (void)result;
  thread_set_thread_name(current_thread(), "boot_log");
  uint64_t first = 0, blocks = 0;
  const char *why = "";
  boot_log_state = 1;
  // The window exists once NativeUfs has read the root layout; until then the open is refused ("no root layout").
  while (!native_ufs_log_open(&first, &blocks, &why)) {
    if (strcmp(why, "no root layout")) {boot_log_state = 3; return;}
    IOSleep(1000);
  }
  boot_log_first_lba = first;
  boot_log_blocks = blocks;
  const uint32_t slot_count = (uint32_t)((blocks - LogSlotFirst) / LogSlotBlocks);
  boot_log_hb_first = LogSlotFirst + (uint64_t)slot_count * LogSlotBlocks;
  boot_log_hb_count = blocks > boot_log_hb_first ? (uint32_t)(blocks - boot_log_hb_first) : 0;
  struct boot_log_header h;
  memset(&h, 0, sizeof h);
  memcpy(h.magic, "SHLOG001", 8);
  h.version = 3; h.header_bytes = sizeof h;
  h.export_blocks = 0; h.export_below = 0; h.boot_id = log_ticks(); h.first_lba = first; h.window_blocks = (uint32_t)blocks;
  cb_boot_id = h.boot_id;
  h.msg_first = LogMsgFirst; h.msg_blocks = LogMsgBlocks; h.slot_first = LogSlotFirst; h.slot_blocks = LogSlotBlocks; h.slot_count = slot_count;
  h.gpt_open = h.gpt_last = &native_ufs_store_gpt_check ? native_ufs_store_gpt_check() : 0xff;
  strlcpy(h.tag,"cheeseburger-P21",sizeof h.tag);
  uint64_t f = 0; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f)); if (!f) f = 19200000;
  const uint64_t start = log_ticks();
  boot_log_ticks_start = start; boot_log_freq = f;
  uint32_t errors = 0;
  boot_log_state = 2;
  for (;;) {
    const unsigned now = (unsigned)((log_ticks() - start) / f);
    int ok = 1;
    // Text slot, then the message buffer, then the header that makes both visible.
    memset(boot_log_buffer, 0, LogSlotBlocks * LogBlock);
    compose((char *)boot_log_buffer, LogSlotBlocks * LogBlock, h.slots_written, now);
    ok &= native_ufs_log_write(LogSlotFirst + (uint64_t)(h.slots_written % slot_count) * LogSlotBlocks, LogSlotBlocks, boot_log_buffer);
    h.msg_bytes = copy_msgbuf(boot_log_buffer);
    for(unsigned offset=0;offset<LogMsgBlocks && ok;offset+=32)
      ok &= native_ufs_log_write(LogMsgFirst+offset,32,boot_log_buffer+offset*LogBlock);
    if (ok) ++h.slots_written;
    else ++h.write_errors;
    h.uptime_s = now; h.rounds = ++boot_log_rounds; h.state = boot_log_state;
    // Every 30 rounds the GPT is read back; any change stops the writer for good.
    if (&native_ufs_store_gpt_check && !(h.rounds % 30)) h.gpt_last = native_ufs_store_gpt_check();
    memset(boot_log_buffer, 0, LogBlock);
    memcpy(boot_log_buffer, &h, sizeof h);
    ok &= native_ufs_log_write(0, 1, boot_log_buffer);
    ok &= native_ufs_log_sync();
    if (!ok) {boot_log_errors = ++errors;} else {errors = 0;if(h.rounds==1)printf("CB persistent log: first commit synced, boot=%llx\n",h.boot_id);}
    if (errors >= LogMaxErrors) {boot_log_state = 3; return;}
    if (h.gpt_last != h.gpt_open) {boot_log_state = 4; return;}
    IOSleep(LogPeriodMs);
  }
}

// Called by the board page's monitor once a second; starts the writer the first time when sheng-log=1.
void boot_log_tick(unsigned now) {
  (void)now;
  if (boot_log_started) return;
  boot_log_started = 1;
  uint32_t mode = 0;
  if (!cb_board_arg("sheng-log", &mode, sizeof mode) || mode != 1) return;
  if (!&native_ufs_log_open || !&native_ufs_log_write || !&native_ufs_log_sync) return;
  boot_log_buffer = (uint8_t *)IOMallocAligned(LogMsgBlocks * LogBlock, LogBlock);
  if (!boot_log_buffer) return;
  boot_log_heartbeat_buffer = (uint8_t *)IOMallocAligned(LogBlock, LogBlock);
  boot_log_autoreset_buffer = (uint8_t *)IOMallocAligned(LogBlock, LogBlock);
  boot_log_step_buffer = (uint8_t *)IOMallocAligned(LogBlock, LogBlock);

  thread_t thread;
  if (kernel_thread_start(writer, 0, &thread) == KERN_SUCCESS) thread_deallocate(thread);
  thread_t heartbeat;
  if (kernel_thread_start(heartbeat_worker, 0, &heartbeat) == KERN_SUCCESS) thread_deallocate(heartbeat);
}
