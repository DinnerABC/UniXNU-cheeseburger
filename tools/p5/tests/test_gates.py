import unittest,sys,json,hashlib
from pathlib import Path
R=Path(__file__).resolve().parents[3];sys.path.insert(0,str(R/'tools/p5'))
from loop import verify_image,LoopError,command_failed
class Gates(unittest.TestCase):
 def test_actual_image(self):
  p=R/'out/p5/uefi/boot-p5-xnu.img';s=json.loads((p.parent/'packaging.json').read_text())['sha256']
  self.assertTrue(verify_image(p,s)['dtb_identity_match'])
  with self.assertRaises(LoopError):verify_image(p,'0'*64)
 def test_remote_error(self):
  self.assertTrue(command_failed(0,'FAILED (remote: invalid bootimage)'))
  self.assertTrue(command_failed(124,'timeout'))
 def test_personalities(self):
  s=(R/'.work/p4-unixnu/.cache/xnu-work/iokit/KernelConfigTables.cpp').read_text()
  for name in ['ArmCpu','ArmDTPlatform']:self.assertEqual(s.count("'IOClass' = '"+name+"';"),1)
 def test_entry_is_kernel_code(self):
  s=(R/'out/p5/kernel/entry-disassembly.log').read_text()
  self.assertIn('<__start>',s);self.assertIn('stp\tw11, w11',s);self.assertIn('<_start_first_cpu>',s)
if __name__=='__main__':unittest.main()
