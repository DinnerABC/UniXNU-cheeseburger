"""Fixed official CLI; project-isolated, reference-only report and no flash command."""
from pathlib import Path
import json,sys,urllib.request,os,subprocess,plistlib,re
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'.work/p4-unixnu/scripts'));urllib.request.install_opener(urllib.request.build_opener(urllib.request.ProxyHandler({})))
from unixnu import cli,userdata,ipsw
sys.path.insert(0,str(r/'tools/p12'));from ipsw_range import PersistentRange
ipsw.RangeFile=PersistentRange
def attach(dmg):
 result=subprocess.check_output(['hdiutil','attach','-nomount','-readonly','-noautofsck',str(dmg)],text=True)
 devices=re.findall(r'^(/dev/disk[0-9]+)\s',result,re.M)
 if not devices:raise RuntimeError('No whole virtual image disk')
 device=devices[0];info=plistlib.loads(subprocess.check_output(['diskutil','info','-plist',device]))
 if info.get('BusProtocol')!='Disk Image' or info.get('Writable') is not False:raise RuntimeError('Not the read-only image device')
 return device
userdata.attach=attach
base=userdata.build
log=userdata.log
userdata.log=lambda s:log('REFERENCE ONLY: no device installation permitted' if s.startswith('flash it yourself') else s)
def build(ctx,dry_run=False):
 result=base(ctx,dry_run)
 if result:
  out,image,report=userdata.paths(ctx);j=json.loads(report.read_text());j.pop('flash_command',None);j.update(role='iPad7,1 same-build offline reference; not iPhone target',safe_to_flash=False,install_allowed=False,apple_signature_verified=False);report.write_text(json.dumps(j,indent=2)+'\n')
 return result
userdata.build=build
cli.main(sys.argv[1:])
