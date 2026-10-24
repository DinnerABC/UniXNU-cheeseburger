"""Direct, restartable reference download using the fixed official IPSW reader."""
from pathlib import Path
import hashlib,json,plistlib,sys,urllib.request,tomllib,time
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'.work/p4-unixnu/scripts'))
from unixnu import ipsw
urllib.request.install_opener(urllib.request.build_opener(urllib.request.ProxyHandler({})))
o=r/'out/p12/reference';o.mkdir(parents=True,exist_ok=True);cache=o/'range-cache';cache.mkdir(exist_ok=True)
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
ipsw.RangeFile=PersistentRange
class C:
 board=tomllib.loads((r/'config/p12/board.toml').read_text());out_root=o
 def option_text(self,*a):return None
s=ipsw.Ipsw(C());bm=plistlib.dumps(s.manifest,fmt=plistlib.FMT_BINARY);expected=json.loads((r/'out/p10/inputs/A/candidate.json').read_text())
assert s.zip.read('BuildManifest.plist')==(r/'out/p10/inputs/A/BuildManifest.plist').read_bytes(),'Pinned donor manifest changed'
index={k:{'member':s.info(k)[0],'bytes':s.info(k)[1],'compressed_bytes':s.info(k)[2]} for k in ['OS','Cryptex1,SystemOS']};(o/'remote-index.json').write_text(json.dumps(index,indent=2)+'\n');print(json.dumps(index),flush=True)
report={}
for component in index:
 p=s.extract(component);h=hashlib.sha384();sha=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(8<<20),b''):h.update(b);sha.update(b)
 assert h.digest()==s.identity['Manifest'][component]['Digest'],component+' BuildManifest SHA384 mismatch'
 report[component]={'file':str(p.relative_to(r)),'bytes':p.stat().st_size,'sha256':sha.hexdigest(),'sha384':h.hexdigest(),'manifest_digest_match':True,'apple_signature_verified':False,'role':'same-build offline reference only'}
 (o/'components.json').write_text(json.dumps(report,indent=2)+'\n');print('VERIFIED',component,report[component],flush=True)
print('Reference member extraction complete; no phone operations',flush=True)
