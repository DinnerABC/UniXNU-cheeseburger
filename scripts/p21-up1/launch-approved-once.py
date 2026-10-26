from pathlib import Path
import time,subprocess,sys
r=Path(__file__).resolve().parents[2];o=r/'out/p21/upstream-a'
for _ in range(240):
 if (o/'uefi/packaging.json').is_file():break
 if 'Traceback (most recent call last)' in (o/'finish-build.log').read_text():raise RuntimeError('Packaging failed; no device action')
 time.sleep(2)
else:raise RuntimeError('Packaging not ready; no device action')
subprocess.run([sys.executable,str(r/'scripts/p21-up1/check-inputs.py')],check=True,cwd=r)
subprocess.run([sys.executable,str(r/'scripts/p21-up1/gate.py')],check=True,cwd=r)
subprocess.run([sys.executable,str(r/'scripts/p21-up1/boot-once.py')],check=True,cwd=r)
