// Cached counters from already selected board adapters; no fresh hardware accesses.
extern volatile uint32_t native_ufs_blocks_published,native_ufs_integrity_failures,native_ram_block_ready;
extern volatile uint64_t native_ufs_block_reads,native_ufs_block_failures,native_ufs_verified_blocks;
extern volatile uint64_t native_ram_block_bytes,native_ram_block_reads,native_ram_block_writes,native_ram_block_failures,native_ram_block_resident_bytes;
extern volatile uint64_t p11_pmgr_reads,p11_pmgr_writes;
extern volatile int native_ram_block_activation_status,native_owner_cache_stage,native_owner_cache_status,native_owner_cache_mount_status,native_owner_cache_authorized,native_owner_cache_readonly,native_owner_data_status;
extern volatile uint64_t cb_keys[13];
extern volatile unsigned native_hid_stage,native_hid_input_events[8];
extern volatile int native_hid_load_status,native_hid_input_results[8];
extern char p17_media_text[3072];
extern unsigned int vm_page_free_count,vm_page_wire_count,vm_page_active_count,vm_page_inactive_count;
extern uint64_t max_mem;
static size_t platform_reports(char *out,size_t at,size_t cap){
 char row[256];at=put(out,at,cap,"[platform-storage-root-data-input]",40);
 snprintf(row,sizeof row,"VM managed=%lluMiB free=%u wired=%u active=%u inactive=%u (4KiB pages)",max_mem>>20,vm_page_free_count,vm_page_wire_count,vm_page_active_count,vm_page_inactive_count);at=put(out,at,cap,row,sizeof row);
 snprintf(row,sizeof row,"UFS published=%u reads=%llu failures=%llu verified_blocks=%llu integrity_failures=%u",native_ufs_blocks_published,native_ufs_block_reads,native_ufs_block_failures,native_ufs_verified_blocks,native_ufs_integrity_failures);at=put(out,at,cap,row,sizeof row);
 snprintf(row,sizeof row,"RAM ready=%u bytes=%llu resident=%llu reads=%llu writes=%llu failures=%llu activation=%d Data_status=%d",native_ram_block_ready,native_ram_block_bytes,native_ram_block_resident_bytes,native_ram_block_reads,native_ram_block_writes,native_ram_block_failures,native_ram_block_activation_status,native_owner_data_status);at=put(out,at,cap,row,sizeof row);
 snprintf(row,sizeof row,"ROOT stage=%d status=%d mount=%d auth=%d ro=%d PMGR cached_reads=%llu cached_writes=%llu",native_owner_cache_stage,native_owner_cache_status,native_owner_cache_mount_status,native_owner_cache_authorized,native_owner_cache_readonly,p11_pmgr_reads,p11_pmgr_writes);at=put(out,at,cap,row,sizeof row);
 snprintf(row,sizeof row,"HID stage=%u load=%x reports=%u rc=%x KEY stage=%llu reads=%llu errors=%llu raw=%llx edges=%llu delivered=%llu",native_hid_stage,native_hid_load_status,native_hid_input_events[4],native_hid_input_results[4],cb_keys[0],cb_keys[2],cb_keys[3],cb_keys[4],cb_keys[6],cb_keys[7]);at=put(out,at,cap,row,sizeof row);
 at=put_report(out,at,cap,p17_media_text,3072);return at;
}
