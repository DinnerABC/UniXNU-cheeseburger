#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

REPO=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('native540',REPO/'tools/gpu-native540/native540.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
ROOT=module.ROOT

class Native540(unittest.TestCase):
    def test_real_stages(self):
        libraries=[('SpriteKit','SolidColor_VertexFunc',1),('SpriteKit','FastSingle_FragFunc',2),('MLCompute','arithmetic_reduce_gradient_all',0)]
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            for index,(lib,entry,stage) in enumerate(libraries):
                source=Path('/private/tmp/p21-os-21h461/System/Library/Frameworks')/(lib+'.framework/default.metallib')
                output=Path(tmp)/str(index)
                manifest=module.compile_library(source,entry,output)
                data=json.loads(manifest.read_text())
                self.assertEqual(data['variants'][0]['info']['stage'],stage)
                self.assertFalse(data['runtimeVerified'])
                if stage==2:self.assertIn('msaa',[v['name'] for v in data['variants']])
                with self.assertRaises(ValueError):module.compile_library(source,entry,output)
                bad=copy.deepcopy(data);bad['gpu']=740
                with self.assertRaises(ValueError):module.validate(output,bad)
                for field,value in [('codeWords',33),('fullRegs',0),('constlen',129),('localID',253),('instrlen',129)]:
                    bad=copy.deepcopy(data);bad['variants'][0]['info'][field]=value
                    with self.assertRaises(ValueError):module.validate(output,bad)
                code=output/data['variants'][0]['shader'];code.write_bytes(code.read_bytes()[:-4])
                with self.assertRaises(ValueError):module.validate(output,data)

    def test_real_failures_and_malformed_input(self):
        source=Path('/private/tmp/p21-os-21h461/System/Library/Frameworks/MLCompute.framework/default.metallib')
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            tmp=Path(tmp)
            for i,entry in enumerate(['missing_entry','concat_channels_forward','arithmetic_unary_forward_float']):
                out=tmp/str(i)
                with self.assertRaises(ValueError):module.compile_library(source,entry,out)
                self.assertFalse(out.exists())
            for i,data in enumerate([b'',b'not metal',b'MTLB'+b'\0'*100]):
                src=tmp/('bad'+str(i));src.write_bytes(data)
                proc=subprocess.run([ROOT/'native540-compiler',src,'main',tmp/'bad-output'],capture_output=True)
                self.assertNotEqual(proc.returncode,0)
                self.assertFalse((tmp/'bad-output.json').exists())
            entries=json.loads(subprocess.check_output([ROOT/'native540-compiler','--list',source]))
            entry=next(e for e in entries if e['name']=='batch_norm_compute_mean')
            raw=bytearray(source.read_bytes());raw[entry['offset']+entry['bytes']//2]^=1;src=tmp/'corrupt.metallib';src.write_bytes(raw)
            proc=subprocess.run([ROOT/'native540-compiler',src,'batch_norm_compute_mean',tmp/'corrupt-output'],capture_output=True)
            self.assertNotEqual(proc.returncode,0)

if __name__=='__main__':unittest.main(verbosity=2)
