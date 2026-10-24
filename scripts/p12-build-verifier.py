from pathlib import Path
import json,subprocess
r=Path(__file__).resolve().parents[1];o=r/'out/p12/verifier';o.mkdir(exist_ok=True)
record=json.loads((r/'.work/p4-unixnu/build/xnu/ios-obj/DEVELOPMENT_ARM64_MSM8998/bsd/DEVELOPMENT/bsd_init.o.json').read_text().strip().rstrip(','))
for source in [r/'.work/p4-unixnu/kits/VerifiedRoot/src/verified_root.c',r/'.work/p4-unixnu/kits/VerifiedRoot/adapters/xnu_crypto.c']:
 a=record['arguments'];cmd=[];i=0
 while i<len(a):
  v=a[i]
  if v==record['file']:cmd.append(str(source))
  elif v=='-o':cmd+=['-o',str(o/(source.stem+'.o'))];i+=1
  elif v in ['-MF','-MJ','-MT']:i+=1
  elif v not in ['-MD','-MMD','-MP']:cmd.append(v)
  i+=1
 cmd+=['-I'+str(r/'.work/p4-unixnu/kits/VerifiedRoot/include')]
 (o/(source.stem+'-command.json')).write_text(json.dumps(cmd,indent=2)+'\n');subprocess.run(cmd,cwd=record['directory'],check=True)
subprocess.run(['xcrun','ar','rcs',str(o/'libVerifiedRoot.a'),str(o/'verified_root.o'),str(o/'xnu_crypto.o')],check=True)
print('Real kernel crypto adapter and VerifiedRoot C archive compiled with recorded XNU recipe')
