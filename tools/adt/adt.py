"""Apple flattened device-tree encoder/strict decoder (little-endian property ABI)."""
import struct
def encode(name,properties=None,children=()):
 props={'name':name.encode()+b'\0',**(properties or {})};result=struct.pack('<II',len(props),len(children))
 for key,value in props.items():
  if len(key.encode())>=32:raise ValueError('ADT property name exceeds 31 bytes')
  result+=struct.pack('<32sI',key.encode(),len(value))+value+bytes(-len(value)%4)
 return result+b''.join(children)
def decode(data):
 def node(offset,depth):
  if depth>64 or offset+8>len(data):raise ValueError('Bad ADT node')
  count,children=struct.unpack_from('<II',data,offset);offset+=8;props={}
  if count>(len(data)-offset)//36:raise ValueError('Bad ADT property count')
  for i in range(count):
   key,size=struct.unpack_from('<32sI',data,offset);offset+=36
   if b'\0' not in key or offset+size>len(data):raise ValueError('Bad ADT property')
   key=key.split(b'\0')[0].decode()
   if key in props:raise ValueError('Duplicate ADT property')
   props[key]=data[offset:offset+size];offset=(offset+size+3)&~3
  if 'name' not in props:raise ValueError('Missing ADT node name')
  result=[]
  for i in range(children):
   child,offset=node(offset,depth+1);result.append(child)
  return {'name':props['name'].rstrip(b'\0').decode(),'properties':props,'children':result},offset
 tree,end=node(0,0)
 if end!=len(data):raise ValueError('Trailing ADT bytes')
 return tree
