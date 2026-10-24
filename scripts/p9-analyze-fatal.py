#!/usr/bin/env python3
"""Audit a reported LR against the immutable, actually tested P9 kernel."""
from pathlib import Path
import hashlib,json,re,struct,subprocess
r=Path(__file__).resolve().parents[1];o=r/'out/p9/fatal-analysis';o.mkdir(exist_ok=True)
k=r/'out/p9/milestones/diagnostic-input/kernel/kernel.macho'
data=k.read_bytes();assert hashlib.sha256(data).hexdigest()=='4f2837307c2237b2d0b54abe31fbf629acf7c2cf3feeee4f1e60e597b82ed58a'
lr=0xfffffff007a1a5b0;pc=lr-4
offset=32;segments=[]
for _ in range(struct.unpack_from('<I',data,16)[0]):
 cmd,n=struct.unpack_from('<II',data,offset)
 if cmd==0x19:
  name,va,size,fileoff,filesize=struct.unpack_from('<16s4Q',data,offset+8);segments.append((va,filesize,fileoff))
 offset+=n
def backed(va,n):
 rows=[off+va-base for base,size,off in segments if base<=va and va+n<=base+size];assert len(rows)==1
 return data[rows[0]:rows[0]+n]
def word(va):return struct.unpack('<I',backed(va,4))[0]
def signed(v,b):return v-(1<<b) if v&(1<<(b-1)) else v
bl=word(pc);assert bl&0xfc000000==0x94000000;target=pc+(signed(bl&0x3ffffff,26)<<2)
symbols=subprocess.run(['xcrun','nm','-nm',str(k)],capture_output=True,text=True,check=True).stdout
rows=sorted((int(x.split()[0],16),x.rsplit(' ',1)[-1]) for x in symbols.splitlines() if re.match(r'^[0-9a-f]+ ',x) and '(__TEXT_EXEC,__text)' in x)
owner=max((a,n) for a,n in rows if a<=pc);panic=next(a for a,n in rows if n=='_panic');assert target==panic
# Decode actual X0 ADRP/ADD pair immediately before this BL, not a source-only guess.
a,b=word(pc-8),word(pc-4);assert a&0x9f00001f==0x90000000 and b&0xffc003ff==0x91000000
imm=signed(((a>>5)&0x7ffff)<<2|((a>>29)&3),21)
fmt=((pc-8)&~4095)+(imm<<12)+((b>>10)&4095)
message=backed(fmt,128).split(b'\0',1)[0].decode();assert message.startswith('image4 interface not available')
cmd=['/opt/homebrew/opt/llvm/bin/llvm-objdump','-d','--start-address='+hex(owner[0]),'--stop-address='+hex(lr+32),str(k)]
d=subprocess.run(cmd,capture_output=True,text=True,check=True);(o/'caller-window.asm').write_text(d.stdout)
result=dict(result='CALLSITE_VERIFIED_FOR_NORMALIZED_OCR_LR',reported_caller='0xfffffff007ala5b0',normalized_candidate=hex(lr),normalization='OCR l treated as 1; user supplied text, not an electronic register dump',return_instruction=hex(pc),function=owner[1],panic_target=hex(target),format_address=hex(fmt),compiled_panic_message=message,last_bootstrap_reported='trust_cache_init',hardware_message_body_captured=False,source_condition='trust_cache_runtime_init: img4if == NULL',kernel_sha256=hashlib.sha256(data).hexdigest(),security_check_removed=False)
(o/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
