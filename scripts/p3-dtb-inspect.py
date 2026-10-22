#!/usr/bin/env python3
"""Inspect observed FDT properties without filling missing MSM8998 configuration."""
from pathlib import Path
import struct,json,hashlib
r=Path(__file__).resolve().parents[1]
def parse(b):
 magic,total,off,strings,_,version,last,_,strsize,stsize=struct.unpack_from('>10I',b)
 assert magic==0xd00dfeed and total<=len(b)
 names=b[strings:strings+strsize];i=off;stack=[];props={}
 while i<off+stsize:
  token=struct.unpack_from('>I',b,i)[0];i+=4
  if token==1:
   end=b.index(0,i);stack.append(b[i:end].decode());i=(end+4)&~3
  elif token==2:stack.pop()
  elif token==3:
   size,nameoff=struct.unpack_from('>II',b,i);i+=8;end=names.index(0,nameoff);name=names[nameoff:end].decode();path='/'.join(stack) or '/';props.setdefault(path,{})[name]=b[i:i+size];i=(i+size+3)&~3
  elif token==4:pass
  elif token==9:break
  else:raise ValueError('Invalid FDT token')
 return props
def inspect(p):
 b=p.read_bytes();nodes=parse(b);root=nodes['/'];summary=dict(bytes=len(b),sha256=hashlib.sha256(b).hexdigest(),root={})
 for key in ['model','compatible','qcom,msm-id','qcom,board-id','qcom,pmic-id']:
  if key in root:
   v=root[key];summary['root'][key]=v.rstrip(b'\0').decode().split('\0') if key in ['model','compatible'] else list(struct.unpack('>'+str(len(v)//4)+'I',v))
 selected={}
 for name,props in nodes.items():
  if name.startswith('/memory') or name.startswith('/reserved-memory') or name.startswith('/cpus') or 'interrupt-controller' in props or 'timer' in name:
   selected[name]={k:v.hex() for k,v in props.items() if k in ['reg','compatible','device_type','enable-method','interrupts','#address-cells','#size-cells']}
 return summary,selected
if __name__=='__main__':
 reports={}
 for name,p in [('runtime',r/'out/p3/private/recovery-device.dtb'),('uefi',r/'out/p2/baseline/Android-cheeseburger.dtb')]:
  reports[name],nodes=inspect(p)
  (r/('out/p3/private/'+name+'-dt-nodes.json')).write_text(json.dumps(nodes,indent=2)+'\n')
 (r/'out/p3/dtb-comparison.json').write_text(json.dumps(reports,indent=2)+'\n');print(json.dumps(reports,indent=2))
