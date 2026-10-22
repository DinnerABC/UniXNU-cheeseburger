#!/usr/bin/env python3
"""Create MSM8998 ADT only from captured FDT and an observed UEFI boot affinity."""
import argparse,hashlib,json,os,runpy,struct,sys
from pathlib import Path
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools/adt'));from adt import encode,decode
p=argparse.ArgumentParser();p.add_argument('--boot-mpidr',type=lambda x:int(x,0),required=True);p.add_argument('--output',type=Path,default=r/'out/cheeseburger/adt/kernel.macho.adt');p.add_argument('--owned-memory-size',type=lambda x:int(x,0),default=0x20000000);a=p.parse_args()
if not 0x10000000<=a.owned_memory_size<=0x200000000 or a.owned_memory_size%0x200000:raise SystemExit('Invalid loader allocation request')
fdt=r/'out/p3/private/recovery-device.dtb';raw=fdt.read_bytes();nodes=runpy.run_path(str(r/'scripts/p3-dtb-inspect.py'))['parse'](raw)
def u32(b):return int.from_bytes(b,'big')
def cells(path,key,default=None):
 if key in nodes[path]:return u32(nodes[path][key])
 if default is not None:return default
 raise ValueError('Missing '+path+'/'+key)
def regions(path):
 parent=path.rsplit('/',1)[0] or '/';ac=cells(parent,'#address-cells',2);sc=cells(parent,'#size-cells',1);reg=nodes[path].get('reg',b'');width=(ac+sc)*4
 if len(reg)%width:raise ValueError('Invalid actual reg')
 return [(int.from_bytes(reg[i:i+ac*4],'big'),int.from_bytes(reg[i+ac*4:i+width],'big')) for i in range(0,len(reg),width)]
cpu=[]
for name,props in nodes.items():
 if name.startswith('/cpus/') and props.get('device_type')==b'cpu\0':cpu.append((int.from_bytes(props['reg'],'big'),name))
cpu.sort();boot=a.boot_mpidr&0xff00ffffff
if boot not in [i for i,_ in cpu]:raise SystemExit('Observed boot affinity absent from actual FDT')
gics=[name for name,props in nodes.items() if b'arm,gic-v3\0' in props.get('compatible',b'')];assert len(gics)==1
gic=gics[0];gr=regions(gic);assert len(gr)==2
stride=u32(nodes[gic]['redistributor-stride']);assert stride in [0x20000,0x30000,0x40000]
timer=next(name for name,props in nodes.items() if b'arm,armv8-timer\0' in props.get('compatible',b''));frequency=u32(nodes[timer]['clock-frequency']);assert frequency>0
ints=list(struct.unpack('>'+str(len(nodes[timer]['interrupts'])//4)+'I',nodes[timer]['interrupts']));assert len(ints)==12 and ints[6]==1
virtual_ppi=16+ints[7];assert 16<=virtual_ppi<32
psci=nodes['/psci'];assert psci['method'] in [b'smc\0',b'hvc\0']
ram=regions('/memory');reserved=[]
for path,props in nodes.items():
 if path.startswith('/reserved-memory/') and 'reg' in props:reserved.append((path,regions(path)))
# Use the exact encompassing GIC window; child offsets are derived, never copied.
bus=min(base for base,size in gr);end=max(base+size for base,size in gr)
cpunodes=[encode('cpu'+str(i),{'state':b'running\0' if affinity==boot else b'waiting\0','reg':struct.pack('<I',affinity),'device_type':b'cpu\0','timebase-frequency':struct.pack('<I',frequency)}) for i,(affinity,path) in enumerate(cpu)]
resnodes=[encode(path.split('/')[-1],{'reg':b''.join(struct.pack('<QQ',base,size) for base,size in rs)}) for path,rs in reserved]
tree=encode('device-tree',{'model':b'OnePlus5\0','compatible':b'device-tree\0qcom,msm8998\0','#address-cells':struct.pack('<I',2),'#size-cells':struct.pack('<I',2)},[
 encode('chosen',{'random-seed':os.urandom(64),'darwinboot-memory-size':struct.pack('<Q',a.owned_memory_size)},[encode('memory-map'),encode('manifest-properties')]),
 encode('cpus',children=cpunodes),encode('psci',{'compatible':psci['compatible'],'method':psci['method']}),
 encode('memory',{'device_type':b'memory\0','reg':b''.join(struct.pack('<QQ',base,size) for base,size in ram)}),
 encode('reserved-memory',children=resnodes),encode('defaults',{'sleep-disabled':struct.pack('<I',1)}),encode('options'),
 encode('product',{'product-name':b'OnePlus 5\0','model':b'cheeseburger\0'}),
 encode('arm-io',{'device_type':b'arm-io\0','#address-cells':struct.pack('<I',2),'#size-cells':struct.pack('<I',2),'ranges':struct.pack('<QQQ',0,bus,end-bus)},[
  encode('intc',{'compatible':b'arm,gic-v3\0','reg':b''.join(struct.pack('<QQ',base-bus,size) for base,size in gr),'redistributor-stride':struct.pack('<Q',stride)}),
  encode('timer',{'compatible':b'arm,armv8-timer\0','clock-frequency':struct.pack('<I',frequency),'interrupts':struct.pack('<I',virtual_ppi)})])])
decode(tree);a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(tree)
report=dict(file=str(a.output.relative_to(r)),sha256=hashlib.sha256(tree).hexdigest(),bytes=len(tree),fdt_sha256=hashlib.sha256(raw).hexdigest(),cpu_affinities=[hex(i) for i,_ in cpu],boot_affinity=hex(boot),frequency=frequency,virtual_timer_intid=virtual_ppi,gic_regions=gr,redistributor_stride=stride,ram=ram,reserved_regions=reserved,psci_method=psci['method'].rstrip(b'\0').decode(),owned_memory_request=a.owned_memory_size,allocation_policy='Request only: DarwinBoot must allocate a real conventional-memory window; not a claim of free RAM',hardware_boot_validated=False,driver_adapter_required='ArmCpu hardcodes timer INTID27; apply project timer-property patch before actual kernel use',unknowns=['Full EFI memory map not captured','Only boot CPU intended initially; secondary CPU startup unqualified','Dynamic reserved-memory nodes without reg retained only in source evidence','Apple platform identity and Trust Cache inputs not supplied'])
(r/'out/p4/adt-generation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
