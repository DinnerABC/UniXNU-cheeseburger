#!/usr/bin/env python3
"""Official kernel build API in a preparation stage; never prelink or boot here."""
from pathlib import Path
import os,json,subprocess,sys,time,shutil
R=Path(__file__).resolve().parents[1];U=R/'.work/p21-official-framework';O=R/'out/p21/official-adaptation'
sys.path.insert(0,str(U/'scripts'))
from unixnu.cli import parser
from unixnu.config import Context
from unixnu import feeds,kernel,patches
os.environ['DEVELOPER_DIR']='/Applications/Xcode.app/Contents/Developer'
os.environ['PATH']='/opt/homebrew/opt/python@3.13/libexec/bin:/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/bin:/opt/homebrew/opt/llvm/bin:/opt/homebrew/opt/lld/bin:/opt/homebrew/opt/unifdef/bin:'+os.environ['PATH']
for key in ['HTTP_PROXY','HTTPS_PROXY','ALL_PROXY','http_proxy','https_proxy','all_proxy']:os.environ.pop(key,None)
a=parser().parse_args(['tree','-D',str(R),'--out',str(O/'resolved'),'-j','2']);c=Context(a);m=feeds.resolve(c);c.modules=m
patches.prepare(c,m)
# Board-local native SMP integration; leave the fixed upstream checkout intact.
component=c.tree/'drivers/ArmCpu/ArmCpu.cpp';delta=R/'soc/msm8998/patches/components/ArmCpu-smp-bootstrap-controller.patch'
probe=subprocess.run(['patch','--dry-run','--forward','-p1','-i',str(delta)],cwd=component.parent,capture_output=True)
if probe.returncode==0:
 before=component.read_bytes();text=component.read_text();text=text.replace('  controller = new IOCPUInterruptController;', '  // The shared controller is boot-CPU-only; secondaries use direct handlers.\n  const unsigned controllerCpuCount = gSmpWanted ? 1U : cpuCount;\n  controller = new IOCPUInterruptController;').replace('controller->initCPUInterruptController (cpuCount, 1)','controller->initCPUInterruptController (controllerCpuCount, 1)');component.unlink();component.write_text(text)
else:
 assert 'controller->initCPUInterruptController (controllerCpuCount, 1)' in component.read_text(),probe.stderr

subprocess.run([sys.executable,R/'scripts/p21-restore-official-headers.py'],check=True)
shutil.copy2(R/'scripts/p21-build-board-runtime.py',c.tree/'scripts/p21-build-board-runtime.py')
assert c.example,'Image pipeline remains guarded until firmware/ABI qualification'
start=time.time();kernel.build(c,m)
O=R/'out/p22/r5';O.mkdir(parents=True,exist_ok=True)
report={'result':'OFFICIAL_KERNEL_SOURCE_BUILD_PASS','duration_seconds':time.time()-start,'board_guard_retained':c.example,'prelinked':False,'booted':False,'phone_operations':[],'tree':str(c.tree.relative_to(R))}
(O/'kernel-source-build.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
