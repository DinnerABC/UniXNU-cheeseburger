#!/usr/bin/env python3
"""Compile the upstream source-provider startup callback separately (all POST preserved)."""
from pathlib import Path
import json,subprocess,hashlib
r=Path(__file__).resolve().parents[1];u=r/'.work/p4-unixnu';o=r/'out/p4/crypto-bootstrap';o.mkdir(parents=True,exist_ok=True)
p=u/'patches/xnu/source-corecrypto14-bootstrap.patch';patch=p.read_text();body='\n'.join(x[1:] for x in patch.splitlines() if x.startswith('+') and not x.startswith('+++'))
body='#include <pexpert/pexpert.h>\n#include <kern/debug.h>\n'+body
source=o/'source_crypto_bootstrap.c';source.write_text(body)
rec=json.loads((u/'build/xnu/ios-obj/DEVELOPMENT_ARM64_MSM8998/osfmk/DEVELOPMENT/prng_random.o.json').read_text().strip().rstrip(','));obj=o/'bootstrap.o';args=rec['arguments'];cmd=[];i=0
while i<len(args):
 v=args[i]
 if v==rec['file']:cmd.append(str(source))
 elif v=='-o':cmd+=['-o',str(obj)];i+=1
 elif v in ['-MF','-MT','-MJ']:i+=1
 elif v in ['-MMD','-MD','-MP']:pass
 else:cmd.append(v)
 i+=1
cmd+=['-DXNU_SOURCE_CORECRYPTO14=1'];v=subprocess.run(cmd,cwd=rec['directory'],capture_output=True,text=True);(o/'compile.log').write_text(v.stdout+v.stderr)
report=dict(command=cmd,exit_code=v.returncode,upstream_patch_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),runtime_selftests_executed=False)
if not v.returncode:subprocess.run(['xcrun','ar','rcs',str(o/'libP4CryptoBootstrap.a'),str(obj)],check=True)
(o/'result.json').write_text(json.dumps(report,indent=2)+'\n');raise SystemExit(v.returncode)
