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
extern volatile uint64_t cb_touch_extra[64] __attribute__((weak_import));
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
  const uint32_t slot_count = (uint32_t)((blocks - LogSlotFirst) / LogSlotBlocks);
  struct boot_log_header h;
  memset(&h, 0, sizeof h);
  memcpy(h.magic, "SHLOG001", 8);
  h.version = 3; h.header_bytes = sizeof h;
  h.export_blocks = 0; h.export_below = 0; h.boot_id = log_ticks(); h.first_lba = first; h.window_blocks = (uint32_t)blocks;
  h.msg_first = LogMsgFirst; h.msg_blocks = LogMsgBlocks; h.slot_first = LogSlotFirst; h.slot_blocks = LogSlotBlocks; h.slot_count = slot_count;
  h.gpt_open = h.gpt_last = &native_ufs_store_gpt_check ? native_ufs_store_gpt_check() : 0xff;
  strlcpy(h.tag,"cheeseburger-P21",sizeof h.tag);
  uint64_t f = 0; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f)); if (!f) f = 19200000;
  const uint64_t start = log_ticks();
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
  if (!PE_parse_boot_argn("sheng-log", &mode, sizeof mode) || mode != 1) return;
  if (!&native_ufs_log_open || !&native_ufs_log_write || !&native_ufs_log_sync) return;
  boot_log_buffer = (uint8_t *)IOMallocAligned(LogMsgBlocks * LogBlock, LogBlock);
  if (!boot_log_buffer) return;

  thread_t thread;
  if (kernel_thread_start(writer, 0, &thread) == KERN_SUCCESS) thread_deallocate(thread);
}
