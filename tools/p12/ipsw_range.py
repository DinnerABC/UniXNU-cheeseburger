from pathlib import Path
import hashlib,json,time,urllib.request
from unixnu import ipsw
r=Path(__file__).resolve().parents[2]
cache=r/'out/p12/reference/range-cache';cache.mkdir(parents=True,exist_ok=True)
Original=ipsw.RangeFile
class PersistentRange(Original):
 def __init__(self,url,*a,**kw):super().__init__(url,block=8<<20,retries=2)
 def _block(self,index):
  p=cache/f'{index:08d}.bin';record=p.with_suffix('.json');n=min(self.block,self.size-index*self.block)
  if p.exists() and record.exists():
   b=p.read_bytes();j=json.loads(record.read_text())
   if len(b)==n and hashlib.sha256(b).hexdigest()==j['sha256']:return b
  start=index*self.block;end=start+n-1
  for attempt in range(2):
   try:
    req=urllib.request.Request(self.url,headers={'Range':f'bytes={start}-{end}'})
    with urllib.request.urlopen(req,timeout=60) as response:
     if response.status!=206 or response.headers.get('Content-Range')!=f'bytes {start}-{end}/{self.size}':raise ValueError('Range response mismatch; refuse whole IPSW')
     b=response.read(n+1)
    if len(b)!=n:raise ValueError('Bounded range length mismatch')
    break
   except Exception:
    if attempt:raise
    time.sleep(2)
  tmp=p.with_suffix('.partial');tmp.write_bytes(b);tmp.replace(p);record.write_text(json.dumps({'sha256':hashlib.sha256(b).hexdigest(),'bytes':len(b),'source_url':self.url})+'\n')
  print('range cached',index,len(b),flush=True);return b
