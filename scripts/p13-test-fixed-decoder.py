from pathlib import Path
import subprocess,hashlib,json,os
r=Path(__file__).resolve().parents[1];sdk=r.parent/'upstream/edk2-msm8998/Common/edk2/MdeModulePkg/Library/LzmaCustomDecompressLib/Sdk/C';o=r/'out/p13/compressed';binary=o/'fixed-sdk-decoder';decoded=o/'fixed-sdk-decoded.bundle'
cmd=['/usr/bin/clang','-O2','-Wall','-I'+str(sdk),str(r/'tools/p13/decode-lzma.c'),str(sdk/'LzmaDec.c'),'-o',str(binary)];subprocess.run(cmd,check=True)
subprocess.run([str(binary),str(o/'payload.lzma'),str(decoded)],check=True)
m=json.loads((o/'compression.json').read_text());assert hashlib.sha256(decoded.read_bytes()).hexdigest()==m['source_sha256'];assert decoded.stat().st_size==m['source_bytes'];decoded.unlink()
# Truncation and invalid declared output length must not produce a valid payload.
neg=o/'negative.lzma';b=(o/'payload.lzma').read_bytes();results={}
for name,data in [('truncated',b[:1000]),('wrong-output-length',b[:5]+(1).to_bytes(8,'little')+b[13:])]:
 neg.write_bytes(data);v=subprocess.run([str(binary),str(neg),str(decoded)],capture_output=True);assert v.returncode!=0;results[name]=v.returncode
neg.unlink();(o/'fixed-decoder-test.json').write_text(json.dumps(dict(result='FIXED_EDK2_SDK_HOST_PASS',sha256=m['source_sha256'],compile_command=cmd,negative_cases=results,target_uefi_execution=False,decoder_source_sha256=hashlib.sha256((sdk/'LzmaDec.c').read_bytes()).hexdigest()),indent=2)+'\n');print('Fixed SDK host decode and rejection tests PASS')
