#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Fixture-only host tests; none executes AIR/IR3 or accesses a device."""
import hashlib,json,os,struct,subprocess,tempfile,unittest
from pathlib import Path
import compile_air_fixture as helper
import export_program

ROOT=Path(__file__).resolve().parent
FIXTURE=Path(os.environ['NATIVE540_AIR_FIXTURE'])

class FixtureTests(unittest.TestCase):
    def test_actual_manifest_and_all_file_hashes(self):
        r=json.loads((FIXTURE/'fixture-manifest.json').read_text())
        self.assertEqual(r['entry'],'padding_gradient')
        self.assertFalse(r['reflectionEdited']);self.assertFalse(r['runtimeVerified']);self.assertFalse(r['deviceOperated'])
        for name,value in r['hashes'].items(): self.assertEqual(helper.native540.sha(FIXTURE/name),value)
        m=helper.check_manifest(FIXTURE/'compiled-program.json')
        self.assertEqual(len(m['variants'][0]['info']['descriptionWords']),621)
        self.assertEqual(m['variants'][0]['info']['codeWords'],416)
    def test_every_omitted_reflection_word_remains_fail_closed(self):
        m=json.loads((FIXTURE/'compiled-program.json').read_text());p=m['variants'][0]['info']
        allowed={export_program.PREFIX_FIELDS.index(k) for k in export_program.TYPED_FIELDS}|set(range(44,52))
        for i in set(range(621))-allowed:
            q=json.loads(json.dumps(p));q['descriptionWords'][i]^=1
            with self.assertRaises(ValueError):export_program.validate_reflection(q)
    def test_oracle_exact_finite_bits_and_bounds(self):
        def pattern(i):return 0x3f000000|((i*0x0019660d+0x00034567)&0x007fffff)
        result=[pattern(i+262) for i in range(256)]
        self.assertEqual(len(set(result)),256)
        for word in result:
            self.assertGreaterEqual(struct.unpack('<f',struct.pack('<I',word))[0],0.5)
            self.assertLess(struct.unpack('<f',struct.pack('<I',word))[0],1.0)
        self.assertGreaterEqual(262,0);self.assertLess(517,780)
        self.assertNotEqual(result[0],pattern(0))
    def test_no_overwrite_or_wrong_input(self):
        with tempfile.TemporaryDirectory() as name:
            private=Path(name);bad=private/'bad.metallib';bad.write_bytes(b'not AIR')
            with self.assertRaises(ValueError):helper.build(FIXTURE,private)
            with self.assertRaises(ValueError):helper.build(private/'new',private,library=bad)
            self.assertFalse((private/'new').exists())
    def test_host_typed_plan_encoder_oracle_sanitized(self):
        source=r"""
#include "NativeMetal540AIRFixture.h"
#include <cassert>
#include <cstdio>
#include <cstring>
namespace F=NativeMetal540AIRFixture;
int main(){
 NativeMetal540::Resources r;
 r.shader={0x100000,4096};r.scratch={0x200000,4096};
 r.buffers[0]={{0x300000,4096},0,F::sourceBytes,false};
 r.buffers[1]={{0x400000,F::outputAllocationBytes},0,F::outputBytes,true};
 r.buffers[2]={{0x500000,4096},0,48,false};
 NativeMetal540::Plan q;assert(NativeMetal540::plan(F::program,F::dispatch,r,q));
 assert(q.constantWords==144 && q.sharedBytes==0 && q.cached==13);
 assert(q.constants[96]==3120 && q.constants[97]==1024 && q.constants[98]==48);
 for(unsigned i=0;i<12;++i)assert(q.constants[F::program.info.driverBase+i]==F::driverWords[i]);
 for(unsigned i=0;i<F::program.info.immediateCount;++i)assert(q.constants[F::program.info.immediateBase+i]==F::immediates[i]);
 assert(q.descriptorBytes[0]==3120 && q.descriptorBytes[1]==1024 && q.descriptorBytes[2]==48);
 uint32_t ib[8192]={},count=0;assert(NativeMetal540::encode(F::program,F::dispatch,r,ib,8192,count,true));
 bool codeFound=false;for(unsigned i=0;i+F::program.info.codeWords<=count;++i)
  if(!std::memcmp(ib+i,F::code,sizeof(F::code)))codeFound=true;
 assert(codeFound);
 for(unsigned x=0;x<256;++x){assert(F::sourceIndex(x)==x+262);assert(F::expected(x)==F::inputPattern(x+262));}
 assert(F::expected(256)==F::sentinel);
 r.buffers[1].writable=false;assert(!NativeMetal540::plan(F::program,F::dispatch,r,q));
 r.buffers[1].writable=true;r.buffers[1].allocation.address=0x300000;
 assert(!NativeMetal540::plan(F::program,F::dispatch,r,q));
 r.buffers[1].allocation.address=0x400000;assert(NativeMetal540::plan(F::program,F::dispatch,r,q));
 std::printf("{\"hostOnly\":true,\"encodedWords\":%u,\"constantWords\":[",count);
 for(unsigned i=0;i<q.constantWords;++i)std::printf("%s%u",i?",":"",q.constants[i]);
 std::puts("]}");
}
"""
        with tempfile.TemporaryDirectory() as name:
            tmp=Path(name);cpp=tmp/'fixture.cpp';cpp.write_text(source)
            for sanitized in [False,True]:
                exe=tmp/('fixture-san' if sanitized else 'fixture')
                cmd=['clang++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(FIXTURE),'-I'+str(ROOT.parents[1]/'package/Userland'),str(cpp),'-o',str(exe)]
                if sanitized:cmd.extend(['-fsanitize=address,undefined','-fno-omit-frame-pointer'])
                subprocess.run(cmd,check=True,capture_output=True,text=True)
                run=subprocess.run([exe],check=True,capture_output=True,text=True)
                result=json.loads(run.stdout)
                receipt=json.loads((FIXTURE/'fixture-manifest.json').read_text())
                self.assertEqual(result['constantWords'],receipt['constantWords'])
                self.assertTrue(result['hostOnly'])
                print('host sanitizer' if sanitized else 'host normal','encodedWords',result['encodedWords'])
if __name__=='__main__':unittest.main()
