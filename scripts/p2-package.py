#!/usr/bin/env python3
"""Build the P2 package without overwriting P1 outputs."""
import argparse,json,os,sys
from pathlib import Path
sys.dont_write_bytecode=True
import p1
p=argparse.ArgumentParser();p.add_argument('--upstream',type=Path,required=True);p.add_argument('--edk2',type=Path,required=True);a=p.parse_args()
p1.OUT=p1.ROOT/'out/p2/unixnu-package';p1.OUT.mkdir(parents=True,exist_ok=True)
(p1.OUT/'tmp').mkdir(exist_ok=True);os.environ['TMPDIR']=str(p1.OUT/'tmp')
try:
 result=p1.package(a.upstream.resolve(),a.edk2.resolve());result.update(result='PASS',hardware_validated=False)
except Exception as e:
 p1.write_json(p1.OUT/'result.json',{'result':'FAIL','error':str(e),'hardware_validated':False});raise
p1.write_json(p1.OUT/'result.json',result)
print(json.dumps(result,indent=2))
