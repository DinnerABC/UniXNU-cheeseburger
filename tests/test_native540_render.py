#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Real shader compile + full metadata + original sagit draw differential.
Host/software-model evidence only; never reports GPU completion.
No dependency installs, device access or changes to existing build scripts.
"""
import hashlib,json,os,struct,subprocess,unittest
from pathlib import Path
REPO=Path(__file__).resolve().parents[1]
SAGIT=REPO.parent/'upstream/ios_sagit'
LOCAL=Path('/Users/dinnerabc/unixnu-p21/native540')
OUT=Path(os.environ.get('NATIVE540_RENDER_OUT',str(LOCAL/'render-20261106T061514Z')))
LIB=LOCAL/'obj/gpu-compiler/host/src/freedreno/QcomGPUCompiler.dylib'
USER=REPO/'package/Userland'
TOOLS=REPO/'tools/gpu-native540'
SDK=Path('/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run(args,name):
    p=subprocess.run(list(map(str,args)),capture_output=True,text=True)
    (OUT/(name+'.log')).write_text(p.stdout+p.stderr)
    if p.returncode:raise RuntimeError(name+': '+p.stdout+p.stderr)
    return p.stdout

def parse(path):
    raw=path.read_bytes();w=struct.unpack('<'+'I'*(len(raw)//4),raw);packets=[];i=0
    parity=lambda v:1^(bin(v).count('1')&1)
    while i<len(w):
        h=w[i];i+=1
        if h>>28==4:
            n=h&127;reg=(h>>8)&0x3ffff
            assert n and (h>>7)&1==parity(n) and (h>>27)&1==parity(reg)
            kind=('reg',reg)
        else:
            assert h>>28==7;n=h&0x3fff;op=(h>>16)&127
            assert (h>>15)&1==parity(n) and (h>>23)&1==parity(op)
            kind=('op',op)
        assert i+n<=len(w);packets.append((kind,list(w[i:i+n])));i+=n
    return packets

def draw_state(packets):
    state={};loads=[];ops=[]
    for (kind,key),v in packets:
        if kind=='reg':
            for j,value in enumerate(v):state[key+j]=value
        else:
            if key==0x30:loads.append(v)
            if key==0x38:return state,loads,ops,v
            ops.append((key,v))
    raise AssertionError('no actual draw')

class Render(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        OUT.mkdir(parents=True,exist_ok=True)
        inc=['-I'+str(USER),'-I'+str(TOOLS)]
        run(['clang++','-std=c++17','-O2','-I'+str(SAGIT/'tools/gpu-compiler'),'-I'+str(SAGIT/'drivers/include'),TOOLS/'compile_render.cpp',LIB,'-Wl,-rpath,'+str(LIB.parent),'-o',OUT/'compile-render'],'compiler-build')
        cls.compiled=OUT/'test-compiled'
        if not cls.compiled.exists():run([OUT/'compile-render',cls.compiled],'actual-compiler')
        run(['clang++','-std=c++17','-DNATIVE540_ENCODER_ONLY','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',*inc,REPO/'tests/native540_render_model.cpp',USER/'NativeMetal540Render.cpp',USER/'NativeMetal540Client.cpp','-o',OUT/'render-model'],'model-build')
        run([OUT/'render-model',OUT/'native.pm4'],'model')
        run(['clang++','-std=c++17','-I'+str(REPO/'package/BoardRuntime'),'-I'+str(SAGIT/'drivers/include'),'-I'+str(SAGIT/'drivers/gpu/QcomGPU'),'-include','GPUJITProtocol.h',REPO/'tests/native540_render_reference.cpp','-o',OUT/'render-reference'],'reference-build')
        run([OUT/'render-reference',cls.compiled/'vs.jit',cls.compiled/'fs.jit',OUT/'reference.pm4'],'reference')
        cls.manifest=json.loads((cls.compiled/'manifest.json').read_text())
    def test_actual_metadata(self):
        m=self.manifest
        self.assertEqual((m['gpu'],m['jitVersion'],m['producer']),(540,41,'qgpu_compile_glsl'))
        self.assertEqual(m['sagit'],'673f85070fe9f7872793015eb230a5c3987d4a8c')
        self.assertEqual(m['mesa'],'769e51468b49b2a42f0a0eaf71cf9eed5ff4e5de')
        self.assertFalse(m['runtimeVerified']);self.assertFalse(m['metalCompiler'])
        diffs={}
        for v in m['variants']:
            name=v['name'];raw=struct.unpack('<621I',(self.compiled/(name+'-backend-reflection.bin')).read_bytes());final=struct.unpack('<621I',(self.compiled/(name+'-final-reflection.bin')).read_bytes())
            self.assertEqual(list(raw),v['backendWords']);self.assertEqual(list(final),v['finalWords'])
            diff=[(i,a,b) for i,(a,b) in enumerate(zip(raw,final)) if a!=b]
            self.assertEqual(diff,[(58+10*i,252,0) for i in range(32)]);diffs[name]=diff
            self.assertEqual(sha(self.compiled/(name+'.ir3')),v['codeSHA256']);self.assertEqual(sha(self.compiled/(name+'.jit')),v['jitSHA256'])
            self.assertEqual(sha(self.compiled/(name+'-backend-reflection.bin')),v['backendSHA256']);self.assertEqual(sha(self.compiled/(name+'-final-reflection.bin')),v['finalSHA256'])
            self.assertEqual(final[2]*4,(self.compiled/(name+'.ir3')).stat().st_size)
        self.assertEqual((self.compiled/'render_fixture.h').read_bytes(),(TOOLS/'render_fixture.h').read_bytes())
        (OUT/'reflection-diff.json').write_text(json.dumps(diffs,indent=2)+'\n')
    def test_pm4_original_draw_diff(self):
        native=parse(OUT/'native.pm4');reference=parse(OUT/'reference.pm4')
        ns,nl,no,nd=draw_state(native);rs,rl,ro,rd=draw_state(reference)
        # Upstream embeds compiler ISA in IB data; port binds separate NEW shader
        # BOs. Normalize ONLY the two object addresses to their same stage role.
        normalized=[]
        for reg in (0xe5ac,0xe5c3):
            normalized.append({'register':reg,'native':[ns[reg],ns[reg+1]],'reference':[rs[reg],rs[reg+1]]})
            ns[reg]=rs[reg]=0;ns[reg+1]=rs[reg+1]=0
        clearOnly={hex(k):[ns.get(k),None] for k in ns if k not in rs}
        self.assertEqual(set(clearOnly),{hex(k) for k in [0x2100,0x2101,0x2102,0x2103,0x2104,0x2107,0x2110,0x2111,0x2112,0x2113,0x2114,0x2115,0x2116,0x2117,0x2118,0x2180,0x2181,0x2182,0x2184]})
        differences={hex(k):[ns.get(k),rs.get(k)] for k in ns.keys()|rs.keys() if k in rs and ns.get(k)!=rs.get(k)}
        report={'nativeWords':(OUT/'native.pm4').stat().st_size//4,'referenceWords':(OUT/'reference.pm4').stat().st_size//4,'objectAddressNormalization':normalized,'clearPrerequisiteOnlyRegisters':clearOnly,'registerDifferences':differences,'nativeDraw':nd,'referenceDraw':rd,'runtimeVerified':False}
        (OUT/'pm4-diff.json').write_text(json.dumps(report,indent=2)+'\n')
        self.assertEqual(differences,{})
        self.assertEqual(nd,rd);self.assertEqual(nd,[4|(2<<10)|(2<<6),1,3])
        # All load packets identical except code IOVA words; driver/immediates
        # are checked byte-for-byte against original constants() composition.
        self.assertEqual(len(nl),3);self.assertEqual(len(rl),3)
        for a,b in zip(nl,rl):
            self.assertEqual(a[0],b[0])
            if ((a[0]>>16)&3)==2:self.assertEqual(a[3:],b[3:])
            else:self.assertEqual(a,b)
        self.assertEqual(sum(k==('op',0x38) for k,_ in native),1)
        self.assertFalse(any(k in [('op',0x33),('op',0x3d),('op',0x73)] for k,_ in native))
        self.assertEqual(sum(k==('op',0x2c) for k,_ in native),1)
        self.assertEqual([v[0] for k,v in native if k==('op',0x46) and len(v)==4],[29,29,4])
    def test_failure_lifetime_and_oracle_model(self):
        self.assertIn('19 failures PASS',(OUT/'model.log').read_text())
        self.assertIn('NOT GPU execution',(OUT/'model.log').read_text())
    def test_arm64_compile(self):
        self.assertTrue(SDK.exists(),'use installed SDK only')
        for name in ('NativeMetal540Render','NativeMetal540Client'):
            run(['clang++','-std=c++17','-target','arm64-apple-ios17.7','-isysroot',SDK,'-DNATIVE540_ENCODER_ONLY','-Wall','-Wextra','-Werror','-I'+str(USER),'-c',USER/(name+'.cpp'),'-o',OUT/(name+'-ios.o')],name+'-arm64')
    @classmethod
    def tearDownClass(cls):
        pins={str(p):sha(p) for p in [LIB,TOOLS/'compile_render.cpp',TOOLS/'render_fixture_source.h',TOOLS/'render_fixture.h',USER/'NativeMetal540Render.h',USER/'NativeMetal540Render.cpp',SAGIT/'drivers/gpu/QcomGPU/AdrenoPipeline.h',SAGIT/'tools/gpu-compiler/Compiler.cpp',SAGIT/'tools/gpu-compiler/Backend.c',LOCAL/'obj/gpu-compiler/mesa/src/freedreno/registers/adreno/a5xx.xml']}
        (OUT/'provenance.json').write_text(json.dumps({'runtimeVerified':False,'deviceAccess':False,'pins':pins},indent=2)+'\n')
if __name__=='__main__':unittest.main()
