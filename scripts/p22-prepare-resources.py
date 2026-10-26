#!/usr/bin/env python3
"""Stage one 8-core/4GiB candidate from P22 without changing its artifacts."""
from pathlib import Path
import hashlib,json,shutil,struct,sys,copy
R=Path(__file__).resolve().parents[1];O=R/'out/p22/r2';B=R/'out/p22';O.mkdir(parents=True,exist_ok=True)
for name in ['scripts','adt','config','data-seed','source','tests']:
 shutil.copytree(B/name,O/name,dirs_exist_ok=True)
for name in ['logs','uefi','kernel','native']:(O/name).mkdir(exist_ok=True)
for p in (O/'scripts').glob('*'):
 if p.is_file():
  s=p.read_text().replace('parents[3]','parents[4]').replace('out/p22','out/p22/r2')
  s=s.replace('boot-p22-synaptics-touch.img','boot-p22-r2-8cpu-4g.img')
  if p.name in ['guest-overlay.sh','prepare-uefi.py']:
   s=s.replace('unixnu-p22','unixnu-p22r2').replace('.p22-overlay-marker','.p22r2-overlay-marker')
   if p.name=='guest-overlay.sh':s=s.replace('LOWER=/home/dinnerabc.guest/unixnu-p21p21up1','LOWER=/home/dinnerabc.guest/unixnu-p22/upper:/home/dinnerabc.guest/unixnu-p21p21up1')
  p.write_text(s)
p=O/'config/unixnu.toml';s=p.read_text().replace('cpus=1','cpus=8 sheng-smp=1 armcpu-self-ipi=1').replace('P22 SYNAPTICS S3320 TOUCH','P22 R2 8CPU 4GiB TOUCH');p.write_text(s)
sys.path.insert(0,str(R/'tools/adt'));from adt import decode,encode
n=decode((B/'adt/kernel.macho.adt').read_bytes());old=copy.deepcopy(n)
def child(name):return next(c for c in n['children'] if c['name']==name)
cpus=child('cpus');cpu0=copy.deepcopy(cpus['children'][0]);assert struct.unpack('<I',cpu0['properties']['reg'])[0]==0
# ArmCpu resolves /psci at startCPU; the single-core baseline omitted it.
# Conduit comes from the captured cheeseburger FDT, not a different board.
if not any(c['name']=='psci' for c in n['children']):
 n['children'].append({'name':'psci','properties':{'name':b'psci\0','compatible':b'arm,psci-1.0\0','method':b'smc\0'},'children':[]})
cpus['children']=[]
for i,mpidr in enumerate([0,1,2,3,0x100,0x101,0x102,0x103]):
 c=copy.deepcopy(cpu0);c['name']='cpu'+str(i);c['properties']['name']=c['name'].encode()+b'\0';c['properties']['reg']=struct.pack('<I',mpidr);c['properties']['state']=b'running\0' if i==0 else b'stopped\0';cpus['children'].append(c)
child('chosen')['properties']['darwinboot-memory-size']=struct.pack('<Q',4*1024**3)
child('defaults')['properties']['kern.maxvnodes']=struct.pack('<I',66560)
def emit(c):return encode(c['name'],{k:v for k,v in c['properties'].items() if k!='name'},[emit(x) for x in c['children']])
raw=emit(n);assert decode(raw)==n
p=O/'adt/kernel.macho.adt';p.write_bytes(raw)
v=json.loads((B/'adt/validation.json').read_text());v.update(sha256=hashlib.sha256(raw).hexdigest(),bytes=len(raw),cpu0_only=False,cpu_count=8,owned_memory_bytes=4*1024**3,maxvnodes=66560,hardware_tested=False);(O/'adt/validation.json').write_text(json.dumps(v,indent=2)+'\n')
# Preserve every unrelated node/property, including reservations and signed fixes.
for name in ['arm-io','memory','reserved-memory']:
 assert next((c for c in n['children'] if c['name']==name),None)==next((c for c in old['children'] if c['name']==name),None)
oldchosen=next(c for c in old['children'] if c['name']=='chosen');assert {k:v for k,v in child('chosen')['properties'].items() if k!='darwinboot-memory-size'}=={k:v for k,v in oldchosen['properties'].items() if k!='darwinboot-memory-size'}
(O/'resource-plan.json').write_text(json.dumps({'physical_ram':'6GiB SKU, captured Recovery MemTotal5855888KiB','configured_cpus':8,'affinities':[0,1,2,3,256,257,258,259],'owned_memory_bytes':4*1024**3,'maxvnodes':66560,'contiguous_constraint':'fixed official AllocateWindow; no secure carveout reclamation, allocation must succeed against current UEFI map','all_physical_ram_exposed':False,'smp_runtime_validated':False,'baseline':'out/p22/uefi/boot-p22-synaptics-touch.img','memory_reservations_preserved':True},indent=2)+'\n')
print('P22 R2 ADT/config staged: 8 CPUs, 4GiB request, vnode66560')
