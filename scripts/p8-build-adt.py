#!/usr/bin/env python3
from pathlib import Path
import sys,struct,json,hashlib,runpy
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools/adt'));from adt import decode,encode
source=r/'out/p7/adt/kernel.macho.adt';tree=decode(source.read_bytes());fdt=r/'out/p3/private/recovery-device.dtb';raw=fdt.read_bytes();assert hashlib.sha256(raw).hexdigest()=='743b9d0fee2a7aedb9db32dc57ddfb8197378b78ea003d8c653f492b7e5a2448'
nodes=runpy.run_path(str(r/'scripts/p3-dtb-inspect.py'))['parse'](raw);bus=next(c for c in tree['children'] if c['name']=='arm-io');uf=nodes['/soc/ufshc@1da4000'];assert uf['compatible']==b'qcom,ufshc\0' and 'iommus' not in uf and 'iommu-map' not in nodes['/soc']
added=[]
# Preserve the original GIC range and add bounded identity bus translations.
# A bus translation is not an MMU mapping of all intervening MMIO.
child,parent,span=struct.unpack('<QQQ',bus['properties']['ranges']);assert (child,parent,span)==(0,0x17a00000,0x200000)
bus['properties']['ranges']+=struct.pack('<QQQ',0x1da4000,0x1da4000,0x2500)+struct.pack('<QQQ',0x1da7000,0x1da7000,0xda8)
timer=next(c for c in bus['children'] if c['name']=='timer')
assert timer['properties']['interrupts']==struct.pack('<I',19)
timer['properties']['p8,timer-intid']=timer['properties'].pop('interrupts')
for path in ['/soc/ufshc@1da4000','/soc/ufsphy@1da7000']:
 p=nodes[path];base,size=struct.unpack('>II',p['reg']);name=path.rsplit('/',1)[1];props={'name':name.encode()+b'\0','compatible':p['compatible'],'reg':struct.pack('<QQ',base,size),'status':p.get('status',b'okay\0')}
 if path.startswith('/soc/ufshc'):
  assert (base,size)==(0x1da4000,0x2500)
  assert struct.unpack('>III',p['interrupts'])==(0,265,0)
  props.update({'p8,spi-intid':struct.pack('<I',297),'p8,hc-physical':struct.pack('<Q',base),'p8,msm8998-direct-dma':struct.pack('<I',1),'p8,read-only':b'','p8,warm-link-only':b'','clock-names':p['clock-names'],'phy-names':p['phy-names'],'lanes-per-direction':struct.pack('<I',2)})
  # Do not represent copied Linux phandles as valid Apple ADT references.
 bus['children'].append({'name':name,'properties':props,'children':[]});added.append(dict(path=path,base=hex(base),bytes=size))
def emit(n):return encode(n['name'],n['properties'],[emit(c) for c in n['children']])
b=emit(tree);assert decode(b)==tree;o=r/'out/p8/adt';o.mkdir(parents=True,exist_ok=True);(o/'kernel.macho.adt').write_bytes(b);(o/'validation.json').write_text(json.dumps(dict(result='PASS',bytes=len(b),sha256=hashlib.sha256(b).hexdigest(),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),fdt_sha256=hashlib.sha256(raw).hexdigest(),nodes=added,dma_binding='Captured FDT has no UFS IOMMU binding; physical DMA, no SMMU reprogramming; hardware reachability unproven',runtime_consumed=False),indent=2)+'\n');print('P8 ADT encoded and strictly roundtrip validated',len(b))
