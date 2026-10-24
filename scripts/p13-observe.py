#!/usr/bin/env python3
"""Bounded read-only USB transport observation; never restarts or rewrites."""
from pathlib import Path
import json,sys,time
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools/p13'));from device import Device
d=Device('usb-observe');rows=[]
for i in range(4):
 row={'epoch':time.time(),'adb_online':False,'fastboot_online':False}
 try:
  row['adb_online']=d.run('adb',['get-state'],timeout=3).strip()=='device'
  if row['adb_online']:row['adb_mode']=d.shell('getprop ro.bootmode')
 except RuntimeError:pass
 try:row['fastboot_online']=d.serial in d.run('fastboot',['devices','-l'],timeout=3)
 except RuntimeError:pass
 rows.append(row);(r/'out/p13/usb-observation.json').write_text(json.dumps({'run_dir':str(d.out.relative_to(r)),'observations':rows,'recovery_attempts':0,'screen_evidence_required':True,'kernel_execution_not_inferred_from_usb':True},indent=2)+'\n')
 if i<3:time.sleep(15)
print('Read-only USB observation complete; no automatic recovery or retry')
