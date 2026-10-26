from pathlib import Path
import difflib
R=Path(__file__).resolve().parents[1];X=R/'out/p21/official-adaptation/resolved/cheeseburger/ipados-17.7.11-21H461/cheeseburger/tree/.cache/xnu-work';patch=[]
def edit(rel,fn):
 p=X/rel;s=p.read_text();t=fn(s);assert t!=s;p.write_text(t);patch.extend(difflib.unified_diff(s.splitlines(True),t.splitlines(True),fromfile='a/'+rel,tofile='b/'+rel))
def video(s):
 n='vcputc_options(char c, __unused bool poll)\n{';assert n in s
 return s.replace(n,n+'\n extern boolean_t p8_console_ready(void); extern void p8_console_char(char);\n if(p8_console_ready()) { p8_console_char(c); return; }',1)
edit('osfmk/console/video_console.c',video)
def kprintf(s):
 n='serial_putc_crlf(char c)\n{';assert n in s
 return s.replace(n,n+'\n extern boolean_t p8_console_ready(void); extern void p8_console_char(char);\n if(p8_console_ready()) { p8_console_char(c); return; }',1)
edit('pexpert/arm/pe_kprintf.c',kprintf)
def arm(s):
 for call in ['PE_consistent_debug_inherit();','kernel_startup_initialize_upto(STARTUP_SUB_KPRINTF);','PE_create_console();','PE_init_printf(FALSE);','PE_init_platform(TRUE, &BootCpuData);','cpu_timebase_init(TRUE);']:
  needle='\t'+call;assert needle in s
  label=call.split('(')[0]
  s=s.replace(needle,'\tp21_visible_stage("P21 R11: '+label+' enter");\n'+needle+'\n\tp21_visible_stage("P21 R11: '+label+' returned");',1)
 needle='\tconst unsigned int serial_exists = serial_init();';assert needle in s
 s=s.replace(needle,'\tp21_visible_stage("P21 R11: serial_init enter");\n'+needle+'\n\tp21_visible_stage("P21 R11: serial_init returned");',1)
 return s
edit('osfmk/arm/arm_init.c',arm)
p=R/'patches/xnu/0004-cheeseburger-console-handoff.patch';p.write_text(''.join(patch));series=R/'patches/xnu/series';s=series.read_text();series.write_text(s if p.name in s else s.rstrip()+'\n'+p.name+'\n')
