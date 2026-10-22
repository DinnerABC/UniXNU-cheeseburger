#!/usr/bin/env python3
"""Read both GPTs using each LUN's observed sector size and CRC-checked headers."""
import hashlib, json, os, struct, subprocess, time, zlib
from pathlib import Path
r=Path(__file__).resolve().parents[1]
o=r/'backups/p3'/('gpt-'+time.strftime('%Y%m%d-%H%M%S'));o.mkdir(parents=True,mode=0o700)
env=dict(os.environ,ADB_LIBUSB='1',ADB_SERVER_SOCKET='tcp:localhost:5038',ADB_VENDOR_KEYS=str(r/'.work/android/adbkey'),ANDROID_USER_HOME=str(r/'.work/android'),TMPDIR=str(r/'.work/tmp'))
serial=json.loads((r/'out/p3/private/selected-device.json').read_text())['serial']
base=[str(r/'.work/p3-tools-35/platform-tools/adb'),'-s',serial]
records=[]
def read(command):
 v=subprocess.run(base+['exec-out',command],env=env,capture_output=True,timeout=120)
 if v.returncode:raise RuntimeError('Read failed '+command)
 return v.stdout
def integer(command):return int(read(command).strip())
def getheader(data,lba):
 if data[:8]!=b'EFI PART':raise RuntimeError('Missing GPT header')
 size=struct.unpack_from('<I',data,12)[0]
 if not 92<=size<=len(data):raise RuntimeError('Invalid GPT header size')
 hdr=bytearray(data[:size]);crc=struct.unpack_from('<I',hdr,16)[0];hdr[16:20]=b'\0'*4
 if zlib.crc32(hdr)!=crc:raise RuntimeError('GPT header CRC mismatch')
 current,alternate,first,last=struct.unpack_from('<4Q',data,24)
 entry_lba,count,entrysize,entrycrc=struct.unpack_from('<QIII',data,72)
 if current!=lba or entrysize<128 or entrysize%8 or count*entrysize>16*1024*1024:raise RuntimeError('Unexpected GPT layout')
 return dict(current_lba=current,alternate_lba=alternate,first_usable=first,last_usable=last,entries_lba=entry_lba,entry_count=count,entry_size=entrysize,entries_crc=entrycrc)
if integer('id -u')!=0:raise SystemExit('Root required')
for lun in ['sda','sdb','sdc','sdd','sde','sdf']:
 rec=dict(lun=lun,verified=False)
 try:
  dev='/dev/block/'+lun;ss=integer('blockdev --getss '+dev);size=integer('blockdev --getsize64 '+dev)
  if ss not in [512,4096] or size%ss:raise RuntimeError('Unexpected geometry')
  blocks=size//ss
  def sectors(start,count):
   if start<0 or count<=0 or start+count>blocks:raise RuntimeError('Out-of-range GPT read')
   command='dd if='+dev+' bs='+str(ss)+' skip='+str(start)+' count='+str(count)+' 2>/dev/null'
   data=read(command)
   if len(data)!=count*ss:raise RuntimeError('Short GPT read')
   return data,command
  primary=getheader(sectors(1,1)[0],1)
  if primary['alternate_lba']!=blocks-1:raise RuntimeError('Backup header not at observed LUN end')
  backup=getheader(sectors(blocks-1,1)[0],blocks-1)
  if backup['alternate_lba']!=1:raise RuntimeError('Backup header mismatch')
  tables=[]
  for header in [primary,backup]:
   n=header['entry_count']*header['entry_size'];data=sectors(header['entries_lba'],(n+ss-1)//ss)[0][:n]
   if zlib.crc32(data)!=header['entries_crc']:raise RuntimeError('GPT array CRC mismatch')
   tables.append(data)
  if tables[0]!=tables[1]:raise RuntimeError('Primary/backup arrays differ')
  ranges=[('primary',0,primary['entries_lba']+(len(tables[0])+ss-1)//ss),('backup',backup['entries_lba'],blocks-backup['entries_lba'])]
  files=[]
  for label,start,count in ranges:
   data,command=sectors(start,count)
   again,_=sectors(start,count)
   if data!=again:raise RuntimeError('GPT changed during read')
   f=o/(lun+'-'+label+'.bin');f.write_bytes(data);os.chmod(f,0o600)
   files.append(dict(file=str(f.relative_to(r)),start_lba=start,bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),command=base+['exec-out',command],exit_code=0,repeated_read_matches=True))
  rec.update(verified=True,logical_sector_size=ss,lun_bytes=size,primary=primary,backup=backup,files=files)
 except Exception as e:rec['error']=str(e)
 records.append(rec)
 temp=o/'manifest.json.partial';temp.write_text(json.dumps(records,indent=2)+'\n');os.chmod(temp,0o600);temp.replace(o/'manifest.json')
 print(lun+': '+('VERIFIED' if rec['verified'] else 'FAILED'),flush=True)
 if not rec['verified']:raise SystemExit(1)
