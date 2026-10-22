#!/usr/bin/env python3
"""Read-only P3 inventory; identifiers and raw command output remain private."""
from pathlib import Path
import os,json,subprocess,time,sys
r=Path(__file__).resolve().parents[1];p=r/'out/p3/private';p.mkdir(parents=True,exist_ok=True)
adb=str(r/'.work/p3-tools-35/platform-tools/adb')
env=dict(os.environ,ADB_LIBUSB='1',ANDROID_USER_HOME=str(r/'.work/android'),ADB_VENDOR_KEYS=str(r/'.work/android/adbkey'),ADB_SERVER_SOCKET='tcp:localhost:5038',TMPDIR=str(r/'.work/tmp'))
dev=subprocess.check_output([adb,'devices'],env=env,text=True)
devices=[x.split()[0] for x in dev.splitlines() if len(x.split())==2 and x.split()[1]=='device']
if len(devices)!=1:raise SystemExit('Exactly one authorized ADB device required')
serial=devices[0];(p/'selected-device.json').write_text(json.dumps({'serial':serial,'alias':'oneplus5-usb'},indent=2)+'\n');os.chmod(p/'selected-device.json',0o600)
def run(label,cmd):
 t=time.time();c=[adb,'-s',serial,'shell',cmd]
 try:
  v=subprocess.run(c,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=45);code=v.returncode;output=v.stdout
 except subprocess.TimeoutExpired as e:code=124;output=(e.stdout or b'').decode(errors='replace') if isinstance(e.stdout,bytes) else (e.stdout or '')
 (p/(label+'.log')).write_text(output);(p/(label+'.json')).write_text(json.dumps({'command':c,'exit_code':code,'started_epoch':t,'duration_seconds':time.time()-t},indent=2)+'\n')
 os.chmod(p/(label+'.log'),0o600);os.chmod(p/(label+'.json'),0o600)
 print(label,'exit',code)
 return output
props=['ro.product.device','ro.product.model','ro.product.manufacturer','ro.build.product','ro.hardware','ro.board.platform','ro.build.version.release','ro.build.version.sdk','ro.build.fingerprint','ro.boot.flash.locked','ro.boot.verifiedbootstate','ro.boot.device_state','ro.boot.slot_suffix','ro.build.ab_update','ro.boot.bootdevice','ro.bootmode','ro.twrp.boot','ro.debuggable','ro.secure','ro.soc.model']
run('device-properties','; '.join('echo '+x+'=$(getprop '+x+')' for x in props))
run('shell-id','id; uname -a; command -v su; command -v blockdev; command -v sha256sum')
run('root-probe','su -c id')
run('proc-hardware','cat /proc/cpuinfo; cat /proc/meminfo; cat /proc/iomem; cat /proc/partitions; cat /proc/cmdline')
run('partition-links','ls -l /dev/block/bootdevice/by-name; ls -l /dev/block/by-name; ls -l /dev/block/platform/*/by-name; cat /proc/mounts')
run('ufs-sysfs','for x in /sys/class/block/sd*; do [ -d "$x" ] || continue; echo BLOCK=$x; cat "$x/size" "$x/queue/logical_block_size" "$x/queue/physical_block_size" "$x/device/model" "$x/device/type" 2>/dev/null; readlink -f "$x/device"; done; ls -l /sys/class/scsi_host; ls -l /sys/bus/platform/drivers/ufshcd*')
run('display-input','wm size; wm density; cat /proc/bus/input/devices; ls -l /sys/class/graphics/fb0; cat /sys/class/graphics/fb0/virtual_size /sys/class/graphics/fb0/bits_per_pixel /sys/class/graphics/fb0/stride 2>/dev/null; ls -l /sys/firmware/fdt /proc/device-tree; cat /proc/device-tree/model 2>/dev/null')
