#!/usr/bin/env python3
from pathlib import Path
import json,subprocess
R=Path(__file__).resolve().parents[1];O=R/'out/p22/r2';B=R/'out/p21/official-adaptation/resolved/cheeseburger/ipados-17.7.11-21H461/cheeseburger/tree/build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837'
p=B/'osfmk/DEVELOPMENT/machine_routines.o.json';v=json.loads(p.read_text().strip().rstrip(','));cmd=[];a=v['arguments'];i=0
while i<len(a):
 if a[i] in ['-o','-MF','-MJ','-MT']:i+=2;continue
 if a[i] not in ['-c','-MD','-MMD','-MP']:cmd.append(a[i])
 i+=1
q=subprocess.run(cmd+['-E','-dM'],cwd=v['directory'],capture_output=True,text=True,check=True);defines={}
for line in q.stdout.splitlines():
 if line.startswith('#define '):
  words=line.split(maxsplit=2)
  if len(words)==3:defines[words[1]]=words[2]
for k,n in [('MAX_CPUS','8'),('MAX_CPU_CLUSTERS','2'),('HAS_CLUSTER','1'),('NO_CPU_OVRD','1')]:assert defines.get(k)==n,(k,defines.get(k))
source=R/'out/p21/official-adaptation/resolved/cheeseburger/ipados-17.7.11-21H461/cheeseburger/tree/.cache/xnu-work/osfmk/arm64/start.s';text=source.read_text();assert '#if HAS_CLUSTER' in text and '#0xFFFF' in text and '#if HAS_CLUSTER && !NO_CPU_OVRD' in text
report={'result':'MAX8_CLUSTER2_AFFINITY16_PASS','defines':{k:defines[k] for k in ['MAX_CPUS','MAX_CPU_CLUSTERS','HAS_CLUSTER','NO_CPU_OVRD']},'reset_entry':'XNU HAS_CLUSTER branch preserves MPIDR Aff0+Aff1; no Apple CPU_OVRD','runtime_online_count':'processor_avail_count, not requested cpus argument','hardware_validated':False};(O/'compiled-capacity.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
