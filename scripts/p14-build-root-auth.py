from pathlib import Path
import json,subprocess
r=Path(__file__).resolve().parents[1];o=r/'out/p14/root-auth';o.mkdir(exist_ok=True)
rec=json.loads((r/'.work/p4-unixnu/build/xnu/ios-obj/DEVELOPMENT_ARM64_MSM8998/iokit/DEVELOPMENT/IOCPU.cpo.json').read_text().strip().rstrip(','))
a=rec['arguments'];cmd=[];i=0
while i<len(a):
 x=a[i]
 if x==rec['file']:cmd.append(str(r/'drivers/p14/RootAuth/RootAuth.cpp'))
 elif x=='-o':cmd+=['-o',str(o/'RootAuth.o')];i+=1
 elif x in ['-MF','-MT','-MJ']:i+=1
 elif x not in ['-MD','-MMD','-MP']:cmd.append(x)
 i+=1
cmd+=['-Wno-error'];(o/'compile-command.json').write_text(json.dumps(cmd,indent=2)+'\n');subprocess.run(cmd,cwd=rec['directory'],check=True)
subprocess.run(['xcrun','ar','rcs',str(o/'libRootAuth.a'),str(o/'RootAuth.o')],check=True)
# Only the affected BSD unit is compiled using its actual P13 build recipe.
rec=json.loads((r/'out/p14/baseline/bsd-compile-record.json').read_text());cmd=rec['arguments'];(o/'bsd-compile-command.json').write_text(json.dumps(cmd,indent=2)+'\n');subprocess.run(cmd,cwd=rec['directory'],check=True)
print('P14 root query archive and bsd_init object compiled from actual XNU recipes')
