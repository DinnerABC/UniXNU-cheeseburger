#!/usr/bin/env python3
from pathlib import Path
import difflib,json,hashlib,shutil
r=Path(__file__).resolve().parents[1];k=r/'.work/p4-unixnu/.cache/xnu-work';o=r/'out/p9/source';o.mkdir(parents=True,exist_ok=True)
def edit(name,fn):
 p=k/name;s=p.read_text();after=fn(s)
 if after!=s:
  dst=r/'out/p9/baseline/source'/name
  if not dst.exists():dst.parent.mkdir(parents=True,exist_ok=True);dst.write_text(s)
  (r/'patches/p9'/(p.name+'.patch')).write_text(''.join(difflib.unified_diff(s.splitlines(True),after.splitlines(True),fromfile='a/'+name,tofile='b/'+name)));p.write_text(after)
 dst=o/name;dst.parent.mkdir(parents=True,exist_ok=True);dst.write_text(after)
def debug(s):
 if 'P9_FIRST_FATAL_CAPTURE' in s:return s
 sig='panic_trap_to_debugger(const char *panic_format_str, va_list *panic_args, unsigned int reason, void *ctx,\n    uint64_t panic_options_mask, void *panic_data_ptr, unsigned long panic_caller)\n{';assert sig in s
 s=s.replace(sig,sig+'''\n#if defined(ARM64_BOARD_CONFIG_MSM8998)
 /* P9_FIRST_FATAL_CAPTURE: preserve first format+Darwin va_list before any
  * debugger trap, panic sync, nested panic, platform callout or reset. */
 extern void p9_panic_capture(const char *, va_list *, unsigned, void *, unsigned long) __attribute__((noreturn));
 p9_panic_capture(panic_format_str, panic_args, reason, ctx, panic_caller);
#endif
''',1)
 sig='kdp_machine_reboot_type(unsigned int type, uint64_t debugger_flags)\n{';assert sig in s
 s=s.replace(sig,sig+'''\n#if defined(ARM64_BOARD_CONFIG_MSM8998)
 extern void p9_debugger_reboot(unsigned, unsigned, const char *, unsigned long) __attribute__((noreturn));
 p9_debugger_reboot(type, (unsigned)debugger_current_op, debugger_message, (unsigned long)__builtin_return_address(0));
#endif
''',1);return s
edit('osfmk/kern/debug.c',debug)
def machine(s):
 if 'p9_halt_capture' in s:return s
 sig='halt_all_cpus(boolean_t reboot)\n{';assert sig in s
 return s.replace(sig,sig+'''\n#if defined(ARM64_BOARD_CONFIG_MSM8998)
 /* A non-panic host reboot is recorded separately, never called a panic. */
 extern void p9_halt_capture(unsigned, unsigned long) __attribute__((noreturn));
 p9_halt_capture((unsigned)reboot, (unsigned long)__builtin_return_address(0));
#endif
''',1)
edit('osfmk/arm64/machine_routines.c',machine)
def startup(s):
 if 'p9_note_stage' in s:return s
 for sig in ['kernel_bootstrap_log(const char *message)\n{','kernel_bootstrap_thread_log(const char *message)\n{']:
  assert sig in s;s=s.replace(sig,sig+'''\n#if defined(ARM64_BOARD_CONFIG_MSM8998)
 extern void p9_note_stage(const char *);
 p9_note_stage(message);
#endif
''',1)
 # Reuse the existing bootstrap log helpers. Only two major boundaries,
 # no stage probes, timers, screen overlays or device-specific checkpoints.
 s=s.replace('\tPE_init_iokit();','\tPE_init_iokit();\n\tkernel_bootstrap_thread_log("PE_init_iokit returned");',1)
 s=s.replace('\tbsd_init();','\tkernel_bootstrap_thread_log("bsd_init");\n\tbsd_init();',1);return s
edit('osfmk/kern/startup.c',startup)
p=k/'iokit/KernelConfigTables.cpp';dst=r/'out/p9/baseline/source/iokit/KernelConfigTables.cpp';dst.parent.mkdir(parents=True,exist_ok=True)
if not dst.exists():shutil.copy2(p,dst)
print('P9 first panic funnel, distinct debugger/host reboot capture and existing bootstrap logger integrated')
