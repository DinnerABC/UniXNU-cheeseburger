from pathlib import Path
import difflib,json
R=Path(__file__).resolve().parents[1];O=R/'out/p21/r9';O.mkdir(parents=True,exist_ok=True)
X=R/'out/p21/official-adaptation/resolved/cheeseburger/ipados-17.7.11-21H461/cheeseburger/tree/.cache/xnu-work';OLD=R/'.work/p4-unixnu/.cache/xnu-work';changes=[]
def edit(rel,fn):
 p=X/rel;before=p.read_text();after=fn(before);assert before!=after; p.write_text(after);changes.extend(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/'+rel,tofile='b/'+rel))
old=(OLD/'osfmk/arm64/start.s').read_text();mapping=old[old.index('// P7: map only'):old.index('#endif',old.index('// P7: map only'))]
mapping=mapping.replace('EXT(p6_bad_bootargs)','Lp21_fb_guard_failed').replace('str x7, [x8, #128] // publish only after complete mapping','str x7, [x8, #128] // publish only after complete mapping\n\tstr x7, [x8, #232] // valid framebuffer mapping before VM replacement')
def asm(s):
 assert '// P21 R9 visible early console' not in s
 insert='// P21 R9 visible early console: restore qualified bounded board framebuffer map.\n'+mapping+'\n\tb Lp21_fb_guard_done\nLp21_fb_guard_failed:\n\tb Lp21_fb_guard_failed\nLp21_fb_guard_done:\n'
 return s.replace('Lbootstrap_tables_ready:\n','Lbootstrap_tables_ready:\n'+insert,1)
edit('osfmk/arm64/start.s',asm)
old=(OLD/'osfmk/console/serial_console.c').read_text();console=old[old.index('/* P8 persistent,'):old.index('\nvoid\nconsole_write(',old.index('/* P8 persistent,'))]
console=console.replace('#if defined(ARM64_BOARD_CONFIG_MSM8998)\n','').replace('#endif\n','');console=console.replace('static boolean_t p8_console_ready','boolean_t p8_console_ready').replace('static void p8_console_char','void p8_console_char')
def serial(s):
 s=s.replace('void\nconsole_write(char *str, int size)\n{',console+'\nvoid\nconsole_write(char *str, int size)\n{\n if (p8_console_ready()) { for (int i=0;i<size;i++) p8_console_char(str[i]); return; }',1)
 s=s.replace('console_write_unbuffered(char c)\n{','console_write_unbuffered(char c)\n{\n if (p8_console_ready()) { p8_console_char(c); return; }',1)
 return s
edit('osfmk/console/serial_console.c',serial)
def arm(s):
 helper='''\n/* P21 R9 board diagnostic checkpoints; no reset or storage writes. */
extern volatile uint64_t p6_diag_record[];
extern void p8_console_char(char);
static void p21_visible_stage(const char *s) { while (*s) p8_console_char(*s++); p8_console_char('\\n'); }
'''
 s=s.replace('__startup_func\nvoid\narm_init(',helper+'\n__startup_func\nvoid\narm_init(',1)
 needle='\tPE_init_platform(FALSE, args);'
 at=s.index(needle);s=s[:at]+'\tp21_visible_stage("P21 R9: MMU on, arm_init reached; PE_init_platform enter");\n'+s[at:]
 s=s.replace('\tboot_progress_init();','\tp21_visible_stage("P21 R9: PE_init_platform returned");\n\tboot_progress_init();',1)
 needle='\tarm_vm_init(xmaxmem, args);'
 replace='''\tp21_visible_stage("P21 R9: arm_vm_init enter");
 uint64_t p21_fb_phys=p6_diag_record[16],p21_fb_bytes=p6_diag_record[17];
 p6_diag_record[16]=0; /* Do not use the physical alias during VM replacement. */
'''+needle+'''
 if(p21_fb_phys && p21_fb_bytes && p21_fb_bytes<=0x1000000ULL) {
  p6_diag_record[16]=(uint64_t)ml_io_map((vm_offset_t)p21_fb_phys,(vm_size_t)p21_fb_bytes);
  p6_diag_record[29]=p21_fb_phys;
 }
 p21_visible_stage("P21 R9: arm_vm_init returned; visible console mapped");'''
 assert needle in s;s=s.replace(needle,replace,1);return s
edit('osfmk/arm/arm_init.c',arm)
# Route early platform printf into the same existing board console.
def kprintf(s):
 needle='serial_putc(char c)\n{';assert needle in s
 s=s.replace(needle,needle+'\n extern boolean_t p8_console_ready(void); extern void p8_console_char(char);\n if(p8_console_ready()) { p8_console_char(c); return; }',1);return s
edit('pexpert/arm/pe_kprintf.c',kprintf)
p=R/'patches/xnu/0003-cheeseburger-visible-early-console.patch';p.write_text(''.join(changes));(O/'source-change.json').write_text(json.dumps({'change':'Restore qualified bounded framebuffer mapping and existing allocation-free console; add non-reset checkpoints','patch':str(p.relative_to(R)),'full_audit_requested':False},indent=2)+'\n')
print('Visible console source enabled; no device action')
