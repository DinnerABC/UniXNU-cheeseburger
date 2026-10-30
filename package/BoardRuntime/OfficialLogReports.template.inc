// Read-only compose sections from UniXNU-sheng 0c2046b84dbee545e7e3923eb74d7bb1ce020978.
// Excludes absent GPU export state and unselected hardware fence memory readers.
static size_t compose_official(char *out, size_t cap, uint32_t slot, unsigned now) {
  size_t at = 0;
  char head[96];
  snprintf(head, sizeof head, "#official slot=%u t=%us tag=cheeseburger-P21", slot, now);
  at = put(out, at, cap, head, sizeof head);
  if (&boot_progress_page_text) {
    at = put(out, at, cap, "[page]", 8);
    for (unsigned i = 0; i < 48; ++i) if (boot_progress_page_text[i][0]) at = put(out, at, cap, boot_progress_page_text[i], 120);
  }
  if (&boot_progress_banner_text && &boot_progress_banner_rows) {
    unsigned rows = boot_progress_banner_rows; if (rows > 49) rows = 49;
    at = put(out, at, cap, "[banner]", 10);
    for (unsigned i = 0; i < rows; ++i) at = put(out, at, cap, boot_progress_banner_text[i], 120);
  }
  if (&boot_progress_ext) {
    at = put(out, at, cap, "[ext]", 8);
    for (unsigned i = 0; i < 8; ++i) {char row[56]; snprintf(row, sizeof row, "X%u %.47s", i, boot_progress_ext[i]); at = put(out, at, cap, row, sizeof row);}
  }
  // The Data store's counters (NativeRamBlock), a row of their own: the ext slots they once shared are taken by other kits.
  if (&native_data_persist_state && native_data_persist_state) {
    char row[160];
    snprintf(row, sizeof row, "D st=%u hdr=%u gpt=%x/%x lba=%llx pres=%llu ld=%llu wr=%llu cm=%llu er=%llu pages=%llu", native_data_persist_state,
             native_data_persist_header, native_data_persist_gpt, native_data_persist_gpt_after, (unsigned long long)native_data_persist_first_lba,
             (unsigned long long)native_data_persist_present, (unsigned long long)native_data_persist_loaded, (unsigned long long)native_data_persist_writes,
             (unsigned long long)native_data_persist_syncs, (unsigned long long)native_data_persist_errors, (unsigned long long)native_data_persist_pages);
    at = put(out, at, cap, "[data]", 8);
    at = put(out, at, cap, row, sizeof row);
  }
  if (&boot_progress_ext_wide)
    for (unsigned i = 0; i < 2; ++i) {char row[120]; snprintf(row, sizeof row, "W%u %.111s", i, boot_progress_ext_wide[i]); at = put(out, at, cap, row, sizeof row);}
  // Every traced process's stacks (the banner shows only what fits the panel): role 0..3 are the boot-ui-focus slots, main thread in full
  // (symbolicate with unslid = address - cache slide), then its thread groups.
  if (&boot_ui_trace_role_rows) {
    static char rows[32][120];
    at = put(out, at, cap, "[stacks]", 10);
    for (unsigned role = 0; role < 4; ++role) {
      unsigned n = boot_ui_trace_role_rows(role, rows, 32, 1);
      for (unsigned i = 0; i < n; ++i) at = put(out, at, cap, rows[i], 120);
    }
  }
  if (native_legacy_stage>=10 && &native_legacy_state_words) {
    static char words[240];
    if (native_legacy_state_words(words, sizeof words)) {at = put(out, at, cap, "[display state]", 16); at = put(out, at, cap, words, sizeof words);}
  }
  if (&native_legacy_event_rows) {
    static char events[2560];
    unsigned n = native_legacy_event_rows(events, sizeof events);
    if (n) {at = put(out, at, cap, "[fb events]", 12); at = put_report(out, at, cap, events, n);}
  }
  if (&native_legacy_prepare_report) {
    char row[130];
    native_legacy_prepare_report(row, sizeof row);
    at = put(out, at, cap, "[scanout prepare]", 18); at = put(out, at, cap, row, sizeof row);
  }
  if (&native_legacy_swap_timeline) {
    static char timeline[4096];
    if (native_legacy_swap_timeline(timeline, sizeof timeline)) {
      at = put(out, at, cap, "[swap timeline]", 16);
      for (char *line = timeline; *line && at + 130 < cap;) {
        char *end = strchr(line, '\n');
        if (end) *end = 0;
        at = put(out, at, cap, line, 120);
        if (!end) break;
        line = end + 1;
      }
    }
  }
  if (&native_adreno_metal_rows && native_adreno_metal_rows[0][0]) {
    at = put(out, at, cap, "[metal table]", 14);
    for (unsigned r = 0; r < 48 && at + 130 < cap; ++r)
      if (native_adreno_metal_rows[r][0]) at = put(out, at, cap, (const char *)native_adreno_metal_rows[r], 96);
  }
  // The framebuffer user clients' notification slots (is the render server's vsync registered?).
  if (&native_legacy_clients_dump) {
    static char dump[4096];
    unsigned n = native_legacy_clients_dump(dump, sizeof dump);
    if (n) {
      at = put(out, at, cap, "[mfb clients]", 14);
      for (char *line = dump; *line && at + 130 < cap;) {
        char *end = strchr(line, '\n');
        if (end) *end = 0;
        at = put(out, at, cap, line, 120);
        if (!end) break;
        line = end + 1;
      }
    }
  }
  // The display object's properties now (refresh intervals, timing, scale): the lines the original driver published.
  if (native_legacy_stage>=10 && &native_legacy_properties_now) {
    static char props[7168];
    unsigned n = native_legacy_properties_now(props, sizeof props);
    if (n) {
      at = put(out, at, cap, "[display]", 10);
      // The XML is one long line; 116 characters per row with a prefix, and a tag per key on its own row so refresh/timing keys stand out.
      for (unsigned i = 0; i < n && at + 130 < cap; i += 116) {
        char row[124];
        snprintf(row, sizeof row, "D%03u %.116s", i / 116, props + i);
        at = put(out, at, cap, row, sizeof row);
      }
    }
  }
  // The traced processes' threads with the highest context-switch rate (they are idle in every stack sample but woken hundreds of times a second).
  if (&boot_ui_trace_hot_rows) {
    static char hot[16][120];
    unsigned n = boot_ui_trace_hot_rows(hot, 16);
    if (n) {char head[64]; snprintf(head, sizeof head, "[hot] kernel slide=%lx", (unsigned long)vm_kernel_slide); at = put(out, at, cap, head, sizeof head);}
    for (unsigned i = 0; i < n; ++i) at = put(out, at, cap, hot[i], 120);
  }
  // The newest ring submissions (fence, ring range, then the words the CP reads) and the first words of their IBs.
  if (&native_adreno_ring_log_count && native_adreno_ring_log_count) {
    const uint32_t newest = native_adreno_ring_log_count - 1;
    for (unsigned back = 0; back < 2 && back < native_adreno_ring_log_count; ++back) {
      const volatile uint32_t *r = native_adreno_ring_log[(newest - back) & 3];
      char row[130];
      snprintf(row, sizeof row, "[ring] fence=%u ring %x..%x words=%u", r[0], r[1], r[2], r[3]);
      at = put(out, at, cap, row, sizeof row);
      const uint32_t words = r[3] < 112 ? r[3] : 112;
      for (uint32_t i = 0; i < words; i += 8) {
        int n = snprintf(row, sizeof row, "R%03x", i);
        for (uint32_t k = i; k < i + 8 && k < words && n > 0 && n < 110; ++k) n += snprintf(row + n, sizeof row - n, " %08x", r[4 + k]);
        at = put(out, at, cap, row, sizeof row);
      }
    }
  }
  if (&native_adreno_ib_log_count && native_adreno_ib_log_count) {
    const uint32_t newest = native_adreno_ib_log_count - 1;
    for (unsigned back = 0; back < 2 && back < native_adreno_ib_log_count; ++back) {
      const volatile uint32_t *r = native_adreno_ib_log[(newest - back) & 3];
      char row[130];
      snprintf(row, sizeof row, "[ib] fence=%u iova=%x%08x bytes=%u", r[0], r[2], r[1], r[3]);
      at = put(out, at, cap, row, sizeof row);
      for (uint32_t i = 0; i < 28; i += 8) {
        int n = snprintf(row, sizeof row, "I%02x", i);
        for (uint32_t k = i; k < i + 8 && k < 28 && n > 0 && n < 110; ++k) n += snprintf(row + n, sizeof row - n, " %08x", r[4 + k]);
        at = put(out, at, cap, row, sizeof row);
      }
    }
  }
  // Kernel GPU methods by number: calls, failures, last status and total time (the calls of the last 2 s are the difference of two slots).
  if (&native_adreno_method_stats) {
    char row[130];
    int n = snprintf(row, sizeof row, "[gpu methods] n:calls/fail/us");
    for (unsigned m = 1; m < 20; ++m) {
      if (!native_adreno_method_stats[m][0]) continue;
      if (n > 100) {at = put(out, at, cap, row, sizeof row); n = snprintf(row, sizeof row, "[gpu methods+]");}
      n += snprintf(row + n, sizeof row - n, " %u:%u/%u/%u", m, native_adreno_method_stats[m][0], native_adreno_method_stats[m][1], native_adreno_method_stats[m][3]);
    }
    at = put(out, at, cap, row, sizeof row);
  }
  if (&native_adreno_vm_phase_us && (native_adreno_vm_phase_us[0][0] || native_adreno_vm_phase_us[1][0])) {
    char row[170];
    for (unsigned k = 0; k < 2; ++k) {
      const volatile uint64_t *p = native_adreno_vm_phase_us[k];
      if (!p[0]) continue;
      snprintf(row, sizeof row, "[vm %s] n=%llu us: observe1=%llu %s=%llu batch=%llu observe2=%llu %s=%llu max=%llu MiB=%llu", k ? "unmap" : "map",
               (unsigned long long)p[0], (unsigned long long)p[1], k ? "acquire" : "publish", (unsigned long long)p[2], (unsigned long long)p[3],
               (unsigned long long)p[4], k ? "retire" : "-", (unsigned long long)p[5], (unsigned long long)p[6], (unsigned long long)(p[7] >> 20));
      at = put(out, at, cap, row, sizeof row);
    }
  }
  if (&native_adreno_pool_stats) {
    char row[200];
    snprintf(row, sizeof row, "[bo pool] %u MiB (last reserve status %x, wc %x): %u BOs from it, %u fell back, in use %u MiB (peak %u) | scattered: %u BOs %u spans | import spans %u",
             native_adreno_pool_stats[0], native_adreno_pool_status, &native_adreno_pool_wc ? native_adreno_pool_wc : 0, native_adreno_pool_stats[1], native_adreno_pool_stats[2], native_adreno_pool_stats[3],
             native_adreno_pool_stats[4], native_adreno_pool_stats[5], native_adreno_pool_stats[6], native_adreno_pool_stats[7]);
    at = put(out, at, cap, row, sizeof row);
  }
  // GPU clients: name opens/refused/closes, live slot (- none), highest GPU address its mappings reached (MiB), last refusal.
  if (&native_adreno_client_log) {
    char row[400];
    size_t used = (size_t)snprintf(row, sizeof row, "[gpu clients]");
    for (unsigned i = 0; i < 8 && native_adreno_client_log[i].name[0] && used < sizeof row; ++i) {
      const struct native_adreno_client_record *r = &native_adreno_client_log[i];
      char slot[8];
      if (r->slot < 8) snprintf(slot, sizeof slot, "%u", r->slot); else snprintf(slot, sizeof slot, "-");
      used += (size_t)snprintf(row + used, sizeof row - used, " %.19s %u/%u/%u s%s %uM r%x", r->name, r->opens, r->refused, r->closes, slot,
                               r->peak_mib, r->last_refusal);
    }
    at = put(out, at, cap, row, sizeof row);
  }
  if (&native_adreno_metal_askers) {
    char row[400];
    size_t used = (size_t)snprintf(row, sizeof row, "[metal askers]");
    for (unsigned i = 0; i < 12 && native_adreno_metal_askers[i].name[0] && used < sizeof row; ++i)
      used += (size_t)snprintf(row + used, sizeof row - used, " %.19s:%u", native_adreno_metal_askers[i].name, native_adreno_metal_askers[i].reads);
    at = put(out, at, cap, row, sizeof row);
  }
  // The IO-coherence proof (GpuPowerProbe coherenceProbe) and the maintenance passes it saved: map, unmap, submission, completion.
  if (&native_gpu_coherence_row && native_gpu_coherence_row[0]) {
    char row[260];
    snprintf(row, sizeof row, "[coherence] %.110s | on=%u skipped map=%u unmap=%u submit=%u complete=%u | fence pauses slept=%llu spun=%llu",
             native_gpu_coherence_row,
             &native_adreno_io_coherent ? native_adreno_io_coherent : 0, &native_adreno_coherent_skips ? native_adreno_coherent_skips[0] : 0,
             &native_adreno_coherent_skips ? native_adreno_coherent_skips[1] : 0, &native_adreno_coherent_skips ? native_adreno_coherent_skips[2] : 0,
             &native_adreno_coherent_skips ? native_adreno_coherent_skips[3] : 0,
             (unsigned long long)(&native_gpu_idle_sleeps ? native_gpu_idle_sleeps : 0), (unsigned long long)(&native_gpu_idle_spins ? native_gpu_idle_spins : 0));
    at = put(out, at, cap, row, sizeof row);
  }
  // Edge contacts (drivers/NativeHidInput edge::): wrapper installed, contacts begun, of them from an edge, events skipped.
  if (&native_touch_edge_stats) {
    char row[100];
    snprintf(row, sizeof row, "[touch edge] installed=%u contacts=%u from_edge=%u skipped=%u", native_touch_edge_stats[0],
             native_touch_edge_stats[1], native_touch_edge_stats[2], native_touch_edge_stats[3]);
    at = put(out, at, cap, row, sizeof row);
  }
  // The submissions' write-combine work: audits, IB lines compared and found dirty, pages set back to write-combined, the re-assertion's
  // total time, and the CPU cache maintenance passes (count, total ms, MiB).
  if (&native_adreno_wc_audit && &native_adreno_reassert_ticks && &native_adreno_cache_stats) {
    uint64_t f = 0;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
    if (!f) f = 19200000;
    char row[200];
    snprintf(row, sizeof row, "[wc] audited=%u lines=%u dirty=%u reset=%u reassert_ms=%llu | maintenance passes=%llu ms=%llu MiB=%llu",
             native_adreno_wc_audit[0], native_adreno_wc_audit[1], native_adreno_wc_audit[2], native_adreno_wc_audit[5],
             (unsigned long long)(native_adreno_reassert_ticks * 1000 / f), (unsigned long long)native_adreno_cache_stats[3],
             (unsigned long long)(native_adreno_cache_stats[0] * 1000 / f), (unsigned long long)(native_adreno_cache_stats[1] >> 20));
    at = put(out, at, cap, row, sizeof row);
  }
  // CPU clusters and L3 as voted (CpuFreqProbe, refreshed every round) and the GPU's initial HFI perf/bandwidth vote.
  if (&native_cpufreq_row && native_cpufreq_row[0]) {
    char row[200];
    snprintf(row, sizeof row, "[clocks] %.118s | gpu perf %u bw %u", native_cpufreq_row, &native_gpu_perf_vote ? native_gpu_perf_vote[0] : 0,
             &native_gpu_perf_vote ? native_gpu_perf_vote[1] : 0);
    at = put(out, at, cap, row, sizeof row);
  }
  // Touch strokes in panel pixels as the digitizer published them (y mirrored, 0 = the edge the UI calls top): where each finger came
  // down and lifted, how long and over how many frames, and the extent of every contact so far (edge gestures need the outer pixels).
  if (&native_touch_stroke_count && &native_touch_strokes && &native_touch_extent && native_touch_stroke_count) {
    char row[160];
    const uint32_t n = native_touch_stroke_count;
    snprintf(row, sizeof row, "[touch] strokes=%u extent x %d..%d y %d..%d (px of 3048x2032)", n, native_touch_extent[0] / 10,
             native_touch_extent[1] / 10, native_touch_extent[2] / 10, native_touch_extent[3] / 10);
    at = put(out, at, cap, row, sizeof row);
    for (uint32_t k = n > 16 ? n - 16 : 0; k < n; ++k) {
      const volatile int32_t *r = native_touch_strokes[k & 15];
      snprintf(row, sizeof row, "TS%u t=%u.%03us %ums (%d,%d)->(%d,%d) f%d", k, (uint32_t)r[0] / 1000, (uint32_t)r[0] % 1000, (uint32_t)r[1],
               r[2] / 10, r[3] / 10, r[4] / 10, r[5] / 10, r[6]);
      at = put(out, at, cap, row, sizeof row);
    }
  }
  if (&native_adreno_map_fail && native_adreno_map_fail[0][1]) {
    char row[170];
    int n = snprintf(row, sizeof row, "[map fails]");
    for (unsigned k = 0; k < 5 && native_adreno_map_fail[k][1]; ++k) n += snprintf(row + n, sizeof row - n, " %x x%u", native_adreno_map_fail[k][0], native_adreno_map_fail[k][1]);
    snprintf(row + n, sizeof row - n, " | at last: spans=%u objects=%u iova_pfn=%x handle=%x", native_adreno_map_fail[5][0], native_adreno_map_fail[5][1], native_adreno_map_fail[6][0], native_adreno_map_fail[6][1]);
    at = put(out, at, cap, row, sizeof row);
  }
  if (&native_adreno_fence_wait && native_adreno_fence_wait[0]) {
    char row[160];
    snprintf(row, sizeof row, "[fence wait] calls=%u timeouts=%u last timed-out fence=%u gpu wrote=%u submitted=%u completed=%u no-job=%u refused fence=%u",
             native_adreno_fence_wait[0], native_adreno_fence_wait[1], native_adreno_fence_wait[2], native_adreno_fence_wait[3],
             native_adreno_fence_wait[4], native_adreno_fence_wait[5], native_adreno_fence_wait[6], native_adreno_fence_wait[7]);
    at = put(out, at, cap, row, sizeof row);
    // The callers' waits (int-147): the polls above run under the connection lock, the pauses between them outside it.
    if (&native_adreno_wait_outer && native_adreno_wait_outer[0]) {
      snprintf(row, sizeof row, "[fence wait] outer waits=%llu us=%llu pauses outside the lock=%llu longest=%lluus",
               (unsigned long long)native_adreno_wait_outer[0], (unsigned long long)native_adreno_wait_outer[1],
               (unsigned long long)native_adreno_wait_outer[2], (unsigned long long)native_adreno_wait_outer[3]);
      at = put(out, at, cap, row, sizeof row);
    }
  }
  if (&native_gop_wait_stats && native_gop_wait_stats[0]) {
    char row[160];
    snprintf(row, sizeof row, "[scanout wait] calls=%llu timeouts=%llu asked=%llums last=%lluus min=%lluus max=%lluus mean=%lluus", native_gop_wait_stats[0],
             native_gop_wait_stats[1], native_gop_wait_stats[2], native_gop_wait_stats[3] / 1000, native_gop_wait_stats[4] / 1000,
             native_gop_wait_stats[5] / 1000, native_gop_wait_stats[1] ? native_gop_wait_stats[6] / native_gop_wait_stats[1] / 1000 : 0);
    at = put(out, at, cap, row, sizeof row);
  }
  if (&native_adreno_view_diag && native_adreno_view_diag[0]) {
    char row[120];
    snprintf(row, sizeof row, "[bo views] calls=%u new=%u cached=%u refused=%u last=%x inside=%u", native_adreno_view_diag[0], native_adreno_view_diag[1],
             native_adreno_view_diag[2], native_adreno_view_diag[3], native_adreno_view_diag[4], native_adreno_view_diag[5]);
    at = put(out, at, cap, row, sizeof row);
  }
  if (&native_adreno_import_diag && native_adreno_import_diag[0]) {
    char row[160];
    at = put(out, at, cap, "[import]", 9);
    snprintf(row, sizeof row, "calls=%llu refused=%llu last: st=%llx stage=%llu bytes=%llx addr=%llx | first: st=%llx stage=%llu bytes=%llx addr=%llx",
             native_adreno_import_diag[0], native_adreno_import_diag[1], native_adreno_import_diag[2], native_adreno_import_diag[3],
             native_adreno_import_diag[4], native_adreno_import_diag[5], native_adreno_import_diag[6], native_adreno_import_diag[7],
             native_adreno_import_diag[8], native_adreno_import_diag[9]);
    at = put(out, at, cap, row, sizeof row);
    snprintf(row, sizeof row, "stage_now=%u refused_page pa=%llx offset=%llx", native_adreno_import_stage, native_adreno_import_fail_pa,
             native_adreno_import_fail_offset);
    at = put(out, at, cap, row, sizeof row);
  }
  if (boot_log_reset_probe_text[0]) {
    at = put(out, at, cap, "[reset]", 8);
    at = put(out, at, cap, boot_log_reset_probe_text, sizeof boot_log_reset_probe_text);
    if (boot_log_reset_result[0]) at = put(out, at, cap, boot_log_reset_result, sizeof boot_log_reset_result);
  }
  // The sampling profile (BootProgress.c): which processes the interrupts landed in and where, at most 6 KiB of the slot.
  if (&boot_progress_profile_rows) {
    static char profile[6144];
    const size_t n = boot_progress_profile_rows(profile, sizeof profile);
    if (n) {
      at = put(out, at, cap, "[profile]", 10);
      for (char *line = profile; *line;) {
        char *end = strchr(line, '\n');
        const size_t len = end ? (size_t)(end - line) : strlen(line);
        at = put(out, at, cap, line, len);
        if (!end) break;
        line = end + 1;
      }
    }
  }
  if (&boot_progress_panic_text && boot_progress_panic_text[0]) {
    at = put(out, at, cap, "[panic]", 8);
    at = put(out, at, cap, boot_progress_panic_text, 256);
  }
  return at;
}

