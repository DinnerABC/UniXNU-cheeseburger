#!/usr/bin/env python3
"""Export only board integration deltas on top of the official ordered stack."""
from pathlib import Path
import difflib,json,hashlib
R=Path(__file__).resolve().parents[1];O=R/'out/p21/official-adaptation';T=O/'resolved/cheeseburger/ipados-17.7.11-21H461/cheeseburger/tree';X=T/'.cache/xnu-work';patches=[];changes=[]
def change(rel,fn):
 p=X/rel;old=p.read_text();new=fn(old);assert old!=new,rel
 patches.extend(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/'+rel,tofile='b/'+rel));changes.append({'path':rel,'before':hashlib.sha256(old.encode()).hexdigest(),'after':hashlib.sha256(new.encode()).hexdigest()})
def startup(s):
 needle='\tkernel_bootstrap_log("mac_policy_initmach");';assert s.count(needle)==1
 return s.replace(needle,'''\textern void cheeseburger_ufs_register(void),native_ram_block_register(void),cheeseburger_pmgr_startup(void);
\tkernel_bootstrap_log("cheeseburger_ufs_register"); cheeseburger_ufs_register();
\tkernel_bootstrap_log("native_ram_block_register"); native_ram_block_register();
\tkernel_bootstrap_log("cheeseburger_pmgr_startup"); cheeseburger_pmgr_startup();
'''+needle)
change('osfmk/kern/startup.c',startup)
def tables(s):
 old=(R/'out/p17/r3/source/iokit/KernelConfigTables.cpp').read_text();a=old.index('    "("\n')+len('    "("\n');b=old.index('    "   {"',a);extra=old[a:b];assert 'UniXNUOwnerData' in extra and 'CheeseburgerPlatformPower' in extra and 'qcom,ufshc' in extra
 return s.replace('    "("\n','    "("\n'+extra,1)
change('iokit/KernelConfigTables.cpp',tables)
def bsd(s):
 s='#undef XNU_OWNER_ROOT_AUDIT\n#define XNU_OWNER_ROOT_AUDIT 1\n#undef XNU_OWNER_ROOT_TRUST\n#define XNU_OWNER_ROOT_TRUST 1\n#include <BoardRuntime/RootDecision.h>\n'+s
 old='int owner = readonly && device_vnode && native_verified_block_root_authorized(device);';assert old in s
 s=s.replace(old,'''extern volatile unsigned native_verified_block_root_media_matches;
                int verified = readonly && device_vnode && device == rootdev && native_verified_block_root_authorized(device);
                int owner = p14RootDecision(readonly,(uint32_t)device,(uint32_t)rootdev,(uint32_t)NODEV,
                    native_verified_block_root_media_matches,verified);''')
 needle='\tnative_legacy_display_late_probe();';assert s.count(needle)==1
 return s.replace(needle,needle+'\n\textern void p20_observer_start(void); p20_observer_start();')
change('bsd/kern/bsd_init.c',bsd)
def exec_(s):
 flags={'XNU_NATIVE_RAM_BLOCK':'1','XNU_OWNER_DATA_MOUNT':'1','XNU_OWNER_DATA_VOLUME_UUID':'"D2420479-F58E-44C5-AB79-1815EC32C686"','XNU_OWNER_BOOT_MANIFEST':'1','XNU_OWNER_CACHE_MOUNT':'1','XNU_OWNER_CACHE_STARTUP':'1','XNU_OWNER_CRYPTEX_FILES':'0','XNU_OWNER_UNION_SYSTEMOS':'1'}
 s=''.join('#undef '+k+'\n#define '+k+' '+v+'\n' for k,v in flags.items())+s
 # Independent authenticated SystemOS query, same retained UFS-only owner policy.
 s=s.replace('native_verified_block_root_authorized','native_p15_verified_block_root_authorized').replace('native_verified_block_volume_device','native_p15_verified_block_volume_device')
 needle='\tint data_error = native_owner_data_prepare(p, (user_addr_t)scratch_addr);';assert s.count(needle)==1
 s=s.replace(needle,'''\textern int native_ram_block_activate(const char *);
\tint activation_error = native_ram_block_activate(XNU_OWNER_DATA_VOLUME_UUID);
\tif (activation_error) panic("owner Data activation failed: %d",activation_error);
'''+needle)
 # Reuse the board watchdogd job isolation policy, scoped to ephemeral Data.
 needle='\tint cache_error = native_owner_cache_prepare';at=s.index(needle)
 # Run only after Data is mounted; do not write into the immutable OS root.
 end='\tint data_error = native_owner_data_prepare(p, (user_addr_t)scratch_addr);'
 pos=s.index(end)+len(end)
 # Keep original Data error handling intact. Attach after its complete if block.
 rest=s[pos:];marker='\n#endif';q=rest.index(marker)+len(marker) # after the progress-only conditional
 rest=rest[:q]+'''\n\tif (!data_error) {
\t\textern int cheeseburger_boot_watchdog_policy(void *,uint64_t);
\t\tint policy_error=cheeseburger_boot_watchdog_policy(p,(uint64_t)scratch_addr);
\t\tif (policy_error) printf("cheeseburger watchdog policy refused: %d\\n",policy_error);
\t}
'''+rest[q:];s=s[:pos]+rest
 return s
change('bsd/kern/kern_exec.c',exec_)
def exits(s):
 needle='\tos_reason_t exit_reason = OS_REASON_NULL;\n\tkern_return_t kr = KERN_SUCCESS;';assert s.count(needle)==1
 s=s.replace(needle,needle+'''\n\textern void p20_abort_payload(unsigned,uint64_t,user_addr_t,unsigned,user_addr_t,uint64_t,unsigned);
\tp20_abort_payload(reason_namespace,reason_code,payload,payload_size,reason_string,reason_flags,internal_flags);
''')
 needle='\tif (p->p_crash_behavior != 0 || p == initproc) {';assert s.count(needle)==1
 return s.replace(needle,'''\textern void p20_process_exit(int,const char *,int,unsigned,uint64_t);
\tp20_process_exit(proc_getpid(p),p->p_name,rv,
\t    p->p_exit_reason ? p->p_exit_reason->osr_namespace : 0,
\t    p->p_exit_reason ? p->p_exit_reason->osr_code : 0);
'''+needle)
change('bsd/kern/kern_exit.c',exits)
def traps(s):
 a='\tboot_progress_user_breakpoint(state, esr);';b='\tboot_progress_user_abort(state, esr, fault_addr);';assert a in s and b in s
 return s.replace(a,a+'\n\textern void p21_user_breakpoint(void *,unsigned); p21_user_breakpoint(state,esr);').replace(b,b+'\n\textern void p21_user_abort(void *,unsigned,uint64_t); p21_user_abort(state,esr,fault_addr);')
change('osfmk/arm64/sleh.c',traps)
p=R/'patches/xnu/0001-cheeseburger-platform-integration.patch';p.write_text(''.join(patches));(R/'patches/xnu/series').write_text('0001-cheeseburger-platform-integration.patch\n');(O/'board-patch-export.json').write_text(json.dumps({'changes':changes,'source':'new official arch/OS stack','board_context':'retained qualified startup/root/Data ordering; no source-object copy'},indent=2)+'\n')
p=R/'package/BoardRuntime/BootFaultProbe.c';s=p.read_text().replace('void boot_progress_user_breakpoint(','void p21_user_breakpoint(').replace('void boot_progress_user_abort(','void p21_user_abort(');p.write_text(s)
p=R/'package/BoardRuntime/CompactObserver.c';s=p.read_text().replace('P21 R7 obs=','P21 OFFICIAL obs=').replace('R7 calls+wait','official calls+wait');p.write_text(s)
print('Board integration exported',len(changes),'files')
