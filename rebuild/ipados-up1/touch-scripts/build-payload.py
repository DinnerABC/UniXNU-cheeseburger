from pathlib import Path
import json,hashlib,sys,lzma,struct
r=Path(__file__).resolve().parents[4];sys.path.insert(0,str(r/'tools/ram_payload'));import bundle;bundle.MAX_BYTES=256*1024*1024
o=r/'out/p21/touch-up1/payload';o.mkdir(parents=True,exist_ok=True);files={};definitions=[]
for i,(name,p) in enumerate([('\\kernel.macho',r/'out/p21/touch-up1/kernel/kernel.macho'),('\\kernel.macho.adt',r/'out/p21/touch-up1/adt/kernel.macho.adt')]):
 b=p.read_bytes();v=json.loads(p.with_name('validation.json').read_text());assert v['result']=='PASS' and hashlib.sha256(b).hexdigest()==v['sha256'];files[name]=b
 definitions.append('STATIC CONST UINT8 P9'+('Kernel' if i==0 else 'Adt')+'Hash[32] = {'+','.join('0x'+hashlib.sha256(b).hexdigest()[j:j+2] for j in range(0,64,2))+'};');definitions.append('#define P9'+('Kernel' if i==0 else 'Adt')+'Bytes '+str(len(b)))
files['\\EFI\\UniXNU\\unixnu.toml']=(r/'out/p21/touch-up1/config/unixnu.toml').read_bytes().replace(b'P12 OFFLINE REFERENCE - NOT INSTALLABLE',b'P16 OWNER BOOT MANIFEST - REFERENCE')
seed=r/'out/p21/touch-up1/data-seed/owner-data-1g.dbr';sm=json.loads((seed.parent/'validation.json').read_text());assert sm['result']=='OFFICIAL_SKIP_SETUP_READONLY_REMOUNT_PASS';assert hashlib.sha256(seed.read_bytes()).hexdigest()=='f1a23b6ba8e1a5ed13e1b3e36c3666e5eb76c3a2d54ac1e1803ff2b1e52a06c4'
files['\\scratch-data.img']=seed.read_bytes()
files['\\EFI\\UniXNU\\unixnu.toml']=files['\\EFI\\UniXNU\\unixnu.toml'].replace(b'P16 OWNER BOOT MANIFEST - REFERENCE',b'P17 WRITABLE RAM DATA').replace(b'path = "/kernel.macho"',b'path = "/kernel.macho"\ninitrd = "/scratch-data.img"')
trust=(r/'out/p12/trust/original-static-system.bundle').read_bytes();assert hashlib.sha256(trust).hexdigest()=='7416ca4f8377146626bb284e2133c76bee6cb4e3ee0af5d648b186235a7323a7';files['\\kernel.macho.trustcache']=trust
raw=bundle.pack(files);sha=hashlib.sha256(raw).digest();(o/'payload.bundle').write_bytes(raw);packed=lzma.compress(raw,format=lzma.FORMAT_ALONE,preset=6);packed=packed[:5]+len(raw).to_bytes(8,'little')+packed[13:];assert lzma.decompress(packed,format=lzma.FORMAT_ALONE)==raw
container=struct.pack('<8sQQ32s',b'P13LZ001',len(packed),len(raw),sha)+packed;(o/'payload.container').write_bytes(container);(o/'payload.lzma').write_bytes(packed)
(r/'out/p21/touch-up1/config/Expected.h').write_text('/* Generated P14 kernel hash; fixed ADT and trust cache unchanged. */\n'+'\n'.join(definitions)+'\n')
# Identity constants only: preserve the complete P13 decoder, bounds, reservation and hash algorithm.
h=(r/'config/p13/CompressedPayload.h').read_text();old='0x21,0xaf,0x3f,0x6e,0xb4,0x18,0xc3,0x10,0x3a,0xb4,0xee,0xd4,0x73,0xcf,0x43,0xe1,0x48,0x98,0x85,0x68,0xe5,0x17,0x2f,0xd6,0x9c,0x64,0xb3,0xa3,0xdf,0x04,0x91,0x94';assert old in h;h=h.replace(old,','.join('0x%02x'%x for x in sha)).replace('124993536',str(len(raw))).replace('fixed P12 payload','fixed P14 payload').replace('expanded P12 payload','expanded P14 payload');(r/'out/p21/touch-up1/config/CompressedPayload.h').write_text(h)
# Decoder harness must use this candidate's actual declared output length.
harness=(r/'out/p16/source/decode-lzma.c').read_text().replace('125050880',str(len(raw)));(r/'out/p21/touch-up1/source/decode-lzma.c').write_text(harness)
(o/'manifest.json').write_text(json.dumps(dict(result='PAYLOAD_COMPRESSED_HOST_ROUNDTRIP_PASS',bytes=len(raw),sha256=sha.hex(),container_bytes=len(container),container_sha256=hashlib.sha256(container).hexdigest(),files=bundle.manifest(raw),compression_protocol_unchanged=True),indent=2)+'\n');print('P14 payload compressed; same P13 protocol, only pinned identity constants updated')
