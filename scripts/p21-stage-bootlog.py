from pathlib import Path
import re,hashlib,json
R=Path(__file__).resolve().parents[1];source=R.parent/'upstream/UniXNU-sheng/package/BootLog.c';original=source.read_text();s=original
# Keep official storage format, kernel msgbuf capture, writer scheduling and commit ordering.
start=s.index('static size_t compose(');end=s.index('// The message buffer,',start)
s=s[:start]+'''extern char p20_ui_rows[24][91],p20_exit_rows[5][120],boot_process_rows[10][120];
extern char p8_console_ring[32768];extern volatile unsigned p8_console_count;
static size_t compose(char *out,size_t cap,uint32_t slot,unsigned now) {
 size_t at=0;char head[96];snprintf(head,sizeof(head),"#slot %u t=%us tag=cheeseburger-P21",slot,now);at=put(out,at,cap,head,sizeof(head));
 at=put(out,at,cap,"[first-ui-faults]",32);
 for(unsigned i=0;i<24;++i)if(p20_ui_rows[i][0])at=put(out,at,cap,p20_ui_rows[i],91);
 at=put(out,at,cap,"[exits]",16);for(unsigned i=0;i<5;++i)at=put(out,at,cap,p20_exit_rows[i],120);
 at=put(out,at,cap,"[processes]",16);for(unsigned i=0;i<10;++i)at=put(out,at,cap,boot_process_rows[i],120);
 at=put(out,at,cap,"[visible-console-tail]",32);
 unsigned end=__atomic_load_n(&p8_console_count,__ATOMIC_RELAXED),n=end<24576?end:24576;
 for(unsigned i=end-n;i<end && at+1<cap;++i){unsigned char c=p8_console_ring[i%32768];if(c==10||c==13||c==9||(c>=32&&c<127))out[at++]=c;}
 out[at]=0;return at;
}

'''+s[end:]
start=s.index('  if (&native_ufs_export_blocks && &native_ufs_export_write) {',s.index('static void writer'))
end=s.index('  struct boot_log_header h;',start);s=s[:start]+s[end:]
s=s.replace('  if (&boot_progress_tag) strlcpy(h.tag, boot_progress_tag(), sizeof h.tag);','  strlcpy(h.tag,"cheeseburger-P21",sizeof h.tag);')
start=s.index('  // The reboot-into-fastboot path,',s.index('static void writer'));end=s.index('  uint32_t errors = 0;',start);s=s[:start]+s[end:]
start=s.index('    // The newest complete export is already on the medium;',s.index('static void writer'));end=s.index('    h.uptime_s =',start);s=s[:start]+s[end:]
start=s.index('    if (autoreset &&',s.index('static void writer'));end=s.index('    IOSleep(LogPeriodMs);',start);s=s[:start]+s[end:]
s=s.replace('  export_lock = IOLockAlloc();','')
s=s.replace('    if (!ok) {boot_log_errors = ++errors;} else errors = 0;','    if (!ok) {boot_log_errors = ++errors;} else {errors = 0;if(h.rounds==1)printf("CB persistent log: first commit synced, boot=%llx\\n",h.boot_id);}')
# Remove the unused export API: this board deliberately enables only the log window.
start=s.index('// The export: boot_log_export_put');end=s.index('static uint64_t log_ticks',start);s=s[:start]+s[end:]
# Unused weak diagnostic declarations are harmless declarations, not storage bindings.
(R/'package/BoardRuntime/BootLog.c').write_text(s)
O=R/'out/p21/r14';(O/'bootlog-source.json').write_text(json.dumps({'upstream':str(source),'source_sha256':hashlib.sha256(original.encode()).hexdigest(),'adapted_sha256':hashlib.sha256(s.encode()).hexdigest(),'preserved':['SHLOG001 version3 header','official msgbuf copy','slot/msgbuf/header/sync ordering','2s worker period','error stop logic'],'changes':['replace sheng/GPU snapshots with board UI/console snapshots','remove all export and auto-reset code','tag cheeseburger-P21','no PMIC implementation copied']},indent=2)+'\n')
p=R/'scripts/p21-build-board-runtime.py';s=p.read_text().replace("['BootWatchdogPolicy','verified_root','xnu_crypto']","['BootWatchdogPolicy','BootLog','verified_root','xnu_crypto']").replace("['BootWatchdogPolicy','no-store-provider'","['BootLog','BootWatchdogPolicy','no-store-provider'");p.write_text(s)
p=R/'package/BoardRuntime/CompactObserver.c';s=p.read_text().replace('extern void boot_process_capture(void)', 'extern void boot_log_tick(unsigned);\nextern volatile uint32_t boot_log_state,boot_log_rounds,boot_log_errors;\nextern void boot_process_capture(void)');s=s.replace('stage(32,"R13 first UI fault retained / one page / 5s / original exit policy");','snprintf(b,sizeof(b),"PERSIST state=%u rounds=%u errors=%u / 32MiB / sync+readback",boot_log_state,boot_log_rounds,boot_log_errors);stage(32,b);');s=s.replace('    p20_diag_enabled = 1;','    p20_diag_enabled = 1;\n    boot_log_tick(0);');s=s.replace('P21 R13 obs=','P21 R14 obs=');p.write_text(s)
