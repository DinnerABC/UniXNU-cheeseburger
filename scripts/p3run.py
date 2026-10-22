#!/usr/bin/env python3
"""Save actual device command status without printing hardware identifiers."""
import os,sys,json,subprocess,time
from pathlib import Path
r=Path(__file__).resolve().parents[1];label=sys.argv[1];cmd=sys.argv[2:]
if cmd and cmd[0]=='--':cmd=cmd[1:]
p=r/'out/p3/private';p.mkdir(parents=True,exist_ok=True);os.chmod(p,0o700)
env=dict(os.environ,ADB_LIBUSB='1',ANDROID_USER_HOME=str(r/'.work/android'),ADB_VENDOR_KEYS=str(r/'.work/android/adbkey'),ADB_SERVER_SOCKET='tcp:localhost:5038',TMPDIR=str(r/'.work/tmp'),PATH=str(r/'.work/p3-tools-35/platform-tools')+':'+os.environ['PATH'])
t=time.time()
try:
 x=subprocess.run(cmd,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=40)
 code=x.returncode;data=x.stdout
except subprocess.TimeoutExpired as e:code=124;data=(e.stdout or b'')+b'\nTIMEOUT\n'
except OSError as e:code=127;data=str(e).encode()
(p/(label+'.log')).write_bytes(data);os.chmod(p/(label+'.log'),0o600)
(p/(label+'.json')).write_text(json.dumps({'command':cmd,'exit_code':code,'started_epoch':t,'duration_seconds':time.time()-t},indent=2)+'\n')
print(label+': exit '+str(code))
sys.exit(code)
