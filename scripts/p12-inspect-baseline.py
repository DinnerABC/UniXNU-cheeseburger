from pathlib import Path
import hashlib,json,struct,zlib,subprocess
r=Path(__file__).resolve().parents[1];o=r/'out/p12';o.mkdir(exist_ok=True)
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(8<<20),b''):h.update(b)
 return h.hexdigest()
m=json.loads((r/'out/p11/manifest.json').read_text());pins={}
for name,rec in m['files'].items():
 p=r/'out/p11'/name;assert sha(p)==rec['sha256'];pins[name]=rec
(o/'p11-pins.json').write_text(json.dumps(pins,indent=2)+'\n')
gm=json.loads((r/'backups/p3/gpt-20261022-023658/manifest.json').read_text())[0]
for f in gm['files']:assert sha(r/f['file'])==f['sha256']
b=(r/gm['files'][0]['file']).read_bytes();header=bytearray(b[4096:8192]);assert header[:8]==b'EFI PART'
size=struct.unpack_from('<I',header,12)[0];crc=struct.unpack_from('<I',header,16)[0];struct.pack_into('<I',header,16,0);assert zlib.crc32(header[:size])==crc
lba,count,es,ec=struct.unpack_from('<QIII',header,72);entries=b[lba*4096:lba*4096+count*es];assert len(entries)==count*es and zlib.crc32(entries)==ec
parts=[]
for i in range(count):
 e=entries[i*es:(i+1)*es]
 if not any(e[:16]):continue
 first,last=struct.unpack_from('<QQ',e,32);name=e[56:128].decode('utf-16-le').split('\0')[0];parts.append(dict(name=name,first_lba=first,last_lba=last,block_bytes=4096,capacity_bytes=(last-first+1)*4096))
u=next(x for x in parts if x['name']=='userdata');assert (u['first_lba'],u['last_lba'])==(2441472,14145279)
(o/'geometry.json').write_text(json.dumps(dict(userdata=u,lun_bytes=gm['lun_bytes'],primary_crc_verified=True,partition_array_crc_verified=True,backup_hash_verified=True,partitions=parts,source='P3 read-only GPT backup; no P12 hardware query'),indent=2)+'\n')
s=(r/'drivers/p11/NativeUfs/NativeUfs.cpp').read_text();status=dict(p11_has_rootlayout='findRootLayout' in s,p11_has_nativeufsblock='class NativeUfsBlock' in s,p11_has_verifiedroot='vr_verify_block' in s,live_manifest_status=None,live_manifest_status_reason='P11 never compiled/invoked RootLayout; no current userdata header dump exists',historical_userdata='P3 Recovery mounted /dev/block/sda14 as ext4; no project phase wrote SHNGROOT',current_disk_manifest_observed=False,no_deployed_root_input_high_confidence=True,iomedia_runtime_proven=False,p11_lun_services=6,root_failure='No verified OS/SystemOS root windows and no qualified root-volume UUID; exact IORegistry last gap unobserved')
(o/'baseline.json').write_text(json.dumps(status,indent=2)+'\n')
c=json.loads((r/'out/p10/inputs/A/candidate.json').read_text())
import plistlib
bm=plistlib.loads((r/'out/p10/inputs/A/BuildManifest.plist').read_bytes());identity=next(x for x in bm['BuildIdentities'] if x['Info']['DeviceClass']=='j120ap' and x['Info']['RestoreBehavior']=='Erase')
board=f'''# P12 offline reference only. Never an iPhone installation candidate.
[board]
name = "cheeseburger-p12-reference"
soc = "MSM8998"
[reference]
product = "iPad7,1"
iphone_target_unchanged = true
install_allowed = false
[ipsw]
url = "{c['url']}"
product = "iPad7,1"
version = "17.7.11"
build = "21H461"
device_class = "j120ap"
chip_id = {int(identity['ApChipID'],16)}
board_id = {int(identity['ApBoardID'],16)}
[userdata]
first_lba = {u['first_lba']}
last_lba = {u['last_lba']}
lba_bytes = 4096
windows = [{{name="OS", component="OS"}}, {{name="SystemOS", component="Cryptex1,SystemOS"}}]
'''
(r/'config/p12/board.toml').write_text(board)
print(json.dumps(dict(geometry=u,root_path=status),indent=2))
