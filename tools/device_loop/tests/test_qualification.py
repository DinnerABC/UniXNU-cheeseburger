import unittest,sys,struct,zlib,hashlib,tempfile
from pathlib import Path
R=Path(__file__).resolve().parents[3]
sys.path[:0]=[str(R/'tools/device_loop'),str(R/'tools/ram_payload')]
from loop import verify_image,LoopError,command_failed
from bundle import pack,verify,H,E
class Qualification(unittest.TestCase):
 def test_remote_failure_even_exit_zero(self):
  self.assertTrue(command_failed(0,'FAILED (remote: not allowed)'))
  self.assertFalse(command_failed(0,'OKAY'))
 def test_crc_corruption(self):
  original=pack({'\\kernel':b'actual bounded fixture'})
  for offset in (0,H.size+10,len(original)-4096):
   b=bytearray(original);b[offset]^=1
   with self.assertRaises((ValueError,UnicodeError)):verify(b)
 def test_out_of_bounds_with_valid_table_crc(self):
  b=bytearray(pack({'\\kernel':b'123'}));name,off,n,crc,flags=E.unpack_from(b,H.size)
  E.pack_into(b,H.size,name,2**64-1,n,crc,flags);struct.pack_into('<I',b,20,zlib.crc32(b[H.size:H.size+E.size]))
  with self.assertRaises(ValueError):verify(b)
 def test_truncation(self):
  b=pack({'\\kernel':b'123'})
  for size in (0,8,H.size,len(b)-1):
   with self.assertRaises(ValueError):verify(b[:size])
 def test_real_ram_probe_and_hash_gate(self):
  p=R/'out/p4/ram-payload-uefi/boot-ram-probe.img';sha=hashlib.sha256(p.read_bytes()).hexdigest()
  self.assertTrue(verify_image(p,sha)['dtb_identity_match'])
  with self.assertRaises(LoopError):verify_image(p,'0'*64)
if __name__=='__main__':unittest.main()
