#!/usr/bin/env python3
import json,subprocess,sys
from pathlib import Path
r=Path(__file__).resolve().parents[1]
o=json.loads((r/'config/p4/observed-registers.json').read_text())
subprocess.run([sys.executable,str(r/'scripts/p4-make-adt.py'),'--boot-mpidr',o['mpidr']],check=True,cwd=r)
subprocess.run([sys.executable,str(r/'scripts/p4-check-adt.py'),str(r/'out/cheeseburger/adt/kernel.macho.adt')],check=True,cwd=r)
