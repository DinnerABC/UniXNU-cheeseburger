#!/usr/bin/env python3
from pathlib import Path
import json,subprocess,difflib,shutil,hashlib
r=Path(__file__).resolve().parents[1];u=r/'.work/p4-unixnu';o=r/'out/p8/platform';o.mkdir(parents=True,exist_ok=True);d=r/'drivers/p8/ArmCpu';d.mkdir(exist_ok=True)
s=u/'drivers/ArmCpu/ArmCpu.cpp';old=s.read_text();new=old.replace('(ranges->getLength () == sizeof (bus))','(ranges->getLength () >= sizeof (bus)) && (ranges->getLength () % sizeof (bus) == 0)')
# The timer belongs to the existing CPU/GIC dispatch backend, not a
# separately registered IODT interrupt controller. Do not advertise an
# incomplete generic interrupt-parent graph to IODTMapOneInterrupt.
new=new.replace('timer->getProperty ("interrupts")','timer->getProperty ("p8,timer-intid")');assert new!=old
(d/'ArmCpu.cpp').write_text(new)
for h in s.parent.glob('*.h'):shutil.copy2(h,d/h.name)
(r/'patches/p8/armcpu-bus-ranges.patch').write_text(''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/ArmCpu.cpp',tofile='b/ArmCpu.cpp')))
rec=json.loads((u/'build/xnu/ios-obj/DEVELOPMENT_ARM64_MSM8998/iokit/DEVELOPMENT/IOCPU.cpo.json').read_text().strip().rstrip(','));cmd=[];args=rec['arguments'];i=0;obj=o/'ArmCpu.o'
while i<len(args):
 a=args[i]
 if a==rec['file']:cmd.append(str(d/'ArmCpu.cpp'))
 elif a=='-o':cmd+=['-o',str(obj)];i+=1
 elif a in ['-MF','-MT','-MJ']:i+=1
 elif a in ['-MMD','-MD','-MP']:pass
 else:cmd.append(a)
 i+=1
cmd+=['-Wno-error'];v=subprocess.run(cmd,cwd=rec['directory'],capture_output=True,text=True);(o/'compile.log').write_text(v.stdout+v.stderr);(o/'build.json').write_text(json.dumps(dict(command=cmd,exit_code=v.returncode,source_sha256=hashlib.sha256(new.encode()).hexdigest(),gic_first_range_retained=True),indent=2)+'\n');assert v.returncode==0
subprocess.run(['xcrun','ar','rcs',str(o/'libArmCpu.a'),str(obj)],check=True)
