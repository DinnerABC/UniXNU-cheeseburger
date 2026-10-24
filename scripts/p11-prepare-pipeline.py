from pathlib import Path
import shutil,json
r=Path(__file__).resolve().parents[1]
for name in ['adt','trust','security-abi']:
 shutil.copytree(r/'out/p10'/name,r/'out/p11'/name,dirs_exist_ok=True)
p=r/'out/p11/inputs'
if not p.exists():p.symlink_to('../p10/inputs',target_is_directory=True)
for p in (r/'config/p10').glob('*'):
 if p.is_file():shutil.copy2(p,r/'config/p11'/p.name)
(r/'config/p11/unixnu.toml').write_text((r/'config/p10/unixnu.toml').read_text().replace('P10','P11'))
for name in ['official-prelink','audit-registration','qualify-prelink','check-loader','check-adt','build-payload','pack-image']:
 src=r/'scripts'/('p10-'+name+'.py');s=src.read_text()
 s=s.replace('out/p10','out/p11').replace('config/p10','config/p11').replace('P10_PRELINK_VARIANT','P11_PRELINK_VARIANT').replace('prelink-v4','prelink-v1').replace('boot-p10-xnu.img','boot-p11-xnu.img')
 if name=='qualify-prelink':
  old="assert '_source_corecrypto14_bootstrap' not in symbols and '__ZN20IOBlockStorageDevice10gMetaClassE' not in symbols"
  assert old in s;s=s.replace(old,"assert '_source_corecrypto14_bootstrap' not in symbols\n# A checked N_ABS alias imports the real original class; it is not a second class.\nnb=(r/'out/p11/native/kernel.macho').read_bytes();nm=np.macho(nb);sc=next(at for cmd,at,_ in nm['commands'] if cmd==2);so,nc,st,_=struct.unpack_from('<4I',nb,sc+8);found=[]\nfor ix in range(nc):\n si,kind,sect,desc,value=struct.unpack_from('<IBBHQ',nb,so+ix*16);name=nb[st+si:nb.index(b'\\0',st+si)].decode()\n if name=='__ZN20IOBlockStorageDevice10gMetaClassE':found.append((kind&0xe,value))\nassert found==[(2,0xfffffff006eb01d0)],('Original storage metaclass must be a single audited absolute import',found)")
  s=s.replace('native_ufs_requires_original_storage_binding=True','native_ufs_original_storage_binding_restored=True')
 (r/'scripts'/('p11-'+name+'.py')).write_text(s)
# Keep generated comments accurate without weakening the inherited P9 hash names.
p=r/'scripts/p11-build-payload.py';s=p.read_text().replace('Generated from verified P9 inputs','Generated from verified P11 inputs').replace('Actual P9 kernel, ADT','Actual P11 kernel, ADT');p.write_text(s)
