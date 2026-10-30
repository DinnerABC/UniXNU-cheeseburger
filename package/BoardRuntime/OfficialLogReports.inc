// Read-only compose sections from UniXNU-sheng 0c2046b84dbee545e7e3923eb74d7bb1ce020978.
// Excludes absent GPU export state and unselected hardware fence memory readers.
static size_t compose_official(char *out, size_t cap, uint32_t slot, unsigned now) {
  size_t at = 0;
  char head[96];
  snprintf(head, sizeof head, "#official slot=%u t=%us tag=cheeseburger-P21", slot, now);
  at = put(out, at, cap, head, sizeof head);
  /* Not selected: boot_progress_page_text */
  /* Not selected: boot_progress_banner_rows, boot_progress_banner_text */
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
  /* Not selected: boot_progress_ext_wide */
  // Every traced process's stacks (the banner shows only what fits the panel): role 0..3 are the boot-ui-focus slots, main thread in full
  // (symbolicate with unslid = address - cache slide), then its thread groups.
  /* Not selected: boot_ui_trace_role_rows */
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
  /* Not selected: native_adreno_metal_rows */
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
  /* Not selected: boot_ui_trace_hot_rows */
  // The newest ring submissions (fence, ring range, then the words the CP reads) and the first words of their IBs.
  /* Not selected: native_adreno_ring_log, native_adreno_ring_log_count */
  /* Not selected: native_adreno_ib_log, native_adreno_ib_log_count */
  // Kernel GPU methods by number: calls, failures, last status and total time (the calls of the last 2 s are the difference of two slots).
  /* Not selected: native_adreno_method_stats */
  /* Not selected: native_adreno_vm_phase_us */
  /* Not selected: native_adreno_pool_stats, native_adreno_pool_status, native_adreno_pool_wc */
  // GPU clients: name opens/refused/closes, live slot (- none), highest GPU address its mappings reached (MiB), last refusal.
  /* Not selected: native_adreno_client_log */
  /* Not selected: native_adreno_metal_askers */
  // The IO-coherence proof (GpuPowerProbe coherenceProbe) and the maintenance passes it saved: map, unmap, submission, completion.
  /* Not selected: native_adreno_coherent_skips, native_adreno_io_coherent, native_gpu_coherence_row, native_gpu_idle_sleeps, native_gpu_idle_spins */
  // Edge contacts (drivers/NativeHidInput edge::): wrapper installed, contacts begun, of them from an edge, events skipped.
  if (&native_touch_edge_stats) {
    char row[100];
    snprintf(row, sizeof row, "[touch edge] installed=%u contacts=%u from_edge=%u skipped=%u", native_touch_edge_stats[0],
             native_touch_edge_stats[1], native_touch_edge_stats[2], native_touch_edge_stats[3]);
    at = put(out, at, cap, row, sizeof row);
  }
  // The submissions' write-combine work: audits, IB lines compared and found dirty, pages set back to write-combined, the re-assertion's
  // total time, and the CPU cache maintenance passes (count, total ms, MiB).
  /* Not selected: native_adreno_cache_stats, native_adreno_reassert_ticks, native_adreno_wc_audit */
  // CPU clusters and L3 as voted (CpuFreqProbe, refreshed every round) and the GPU's initial HFI perf/bandwidth vote.
  /* Not selected: native_cpufreq_row, native_gpu_perf_vote */
  // Touch strokes in panel pixels as the digitizer published them (y mirrored, 0 = the edge the UI calls top): where each finger came
  // down and lifted, how long and over how many frames, and the extent of every contact so far (edge gestures need the outer pixels).
  /* Not selected: native_touch_extent, native_touch_stroke_count, native_touch_strokes */
  /* Not selected: native_adreno_map_fail */
  /* Not selected: native_adreno_fence_wait, native_adreno_wait_outer */
  if (&native_gop_wait_stats && native_gop_wait_stats[0]) {
    char row[160];
    snprintf(row, sizeof row, "[scanout wait] calls=%llu timeouts=%llu asked=%llums last=%lluus min=%lluus max=%lluus mean=%lluus", native_gop_wait_stats[0],
             native_gop_wait_stats[1], native_gop_wait_stats[2], native_gop_wait_stats[3] / 1000, native_gop_wait_stats[4] / 1000,
             native_gop_wait_stats[5] / 1000, native_gop_wait_stats[1] ? native_gop_wait_stats[6] / native_gop_wait_stats[1] / 1000 : 0);
    at = put(out, at, cap, row, sizeof row);
  }
  /* Not selected: native_adreno_view_diag */
  /* Not selected: native_adreno_import_diag, native_adreno_import_fail_offset, native_adreno_import_fail_pa, native_adreno_import_stage */
  if (boot_log_reset_probe_text[0]) {
    at = put(out, at, cap, "[reset]", 8);
    at = put(out, at, cap, boot_log_reset_probe_text, sizeof boot_log_reset_probe_text);
    if (boot_log_reset_result[0]) at = put(out, at, cap, boot_log_reset_result, sizeof boot_log_reset_result);
  }
  // The sampling profile (BootProgress.c): which processes the interrupts landed in and where, at most 6 KiB of the slot.
  /* Not selected: boot_progress_profile_rows */
  /* Not selected: boot_progress_panic_text */
  return at;
}

