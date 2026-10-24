from pathlib import Path
import hashlib,json,sys
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools/p13'));from device import Device
o=r/'out/p13/size-probe';state=o/'boot-result.json';assert not state.exists();m=json.loads((o/'manifest.json').read_text());p=r/m['image'];assert hashlib.sha256(p.read_bytes()).hexdigest()==m['sha256'] and m['kernel_payload_present'] is False;assert json.loads((r/'out/p13/flash-result.json').read_text())['state']=='FASTBOOT_FLASH_SUCCEEDED'
d=Device('size-probe');assert 'product: msm8998' in d.run('fastboot',['getvar','product']);result={'state':'ATTEMPT_STARTED','attempts':1,'sha256':m['sha256'],'image':m['image'],'xnu_expected':False,'userdata_write':False,'run_dir':str(d.out.relative_to(r))};state.write_text(json.dumps(result,indent=2)+'\n')
try:d.run('fastboot',['boot',str(p)],timeout=90);result['state']='FASTBOOT_BOOT_ACCEPTED_SCREEN_PENDING'
except Exception as e:result.update(state='FAILED_STOP_NO_RETRY',error=str(e));raise
finally:state.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
