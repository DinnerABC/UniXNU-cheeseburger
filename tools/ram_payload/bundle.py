"""Bounded read-only bundle ABI shared with P4RamPayload.c; CRC is not authentication."""
import struct,zlib,hashlib
H=struct.Struct('<8sQII');E=struct.Struct('<64sQQII');MAX_BYTES=29522547

def pack(files):
 if not 1<=len(files)<=8:raise ValueError('file count')
 data=bytearray(4096);entries=[]
 for name,blob in files.items():
  name=name.encode('ascii');assert name.startswith(b'\\') and 0<len(name)<64 and b'\0' not in name
  at=len(data);entries.append(E.pack(name,at,len(blob),zlib.crc32(blob),0));data+=blob;data+=bytes((-len(data))%4096)
 table=b''.join(entries);data[:H.size]=H.pack(b'P4BNDL01',len(data),len(files),zlib.crc32(table));data[H.size:H.size+len(table)]=table
 verify(data);return bytes(data)

def verify(data):
 if len(data)<H.size:raise ValueError('truncated header')
 magic,size,count,crc=H.unpack_from(data)
 if magic!=b'P4BNDL01' or size!=len(data) or not H.size+count*E.size<=size<=MAX_BYTES or not 1<=count<=8:raise ValueError('header')
 table=data[H.size:H.size+count*E.size]
 if zlib.crc32(table)!=crc:raise ValueError('table CRC')
 files={};ranges=[]
 for i in range(count):
  raw,off,n,crc,flags=E.unpack_from(table,i*E.size)
  if b'\0' not in raw or not raw.startswith(b'\\') or flags or off<H.size+len(table) or off>size or n>size-off:raise ValueError('entry')
  name=raw.split(b'\0')[0].decode('ascii')
  if name.casefold() in [k.casefold() for k in files] or any(off<b and a<off+n for a,b in ranges):raise ValueError('duplicate/overlap')
  blob=bytes(data[off:off+n])
  if zlib.crc32(blob)!=crc:raise ValueError('file CRC')
  files[name]=blob;ranges.append((off,off+n))
 return files

def manifest(data):return {name:{'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()} for name,b in verify(data).items()}
