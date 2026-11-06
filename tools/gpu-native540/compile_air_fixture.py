#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Exact real-21H461 AIR fixture: pinned compiler, unchanged strict exporter.
Apple inputs/LLVM text remain private. This helper neither discovers devices nor
publishes Metal objects. It is not a general AIR/reflection rewriting utility.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import native540
import export_program

ENTRY = 'padding_gradient'
LIBRARY = Path('/private/tmp/p21-os-21h461/System/Library/Frameworks/MLCompute.framework/default.metallib')
LIBRARY_HASH = 'fa9101b5c07bd91720de2cbbcb170b3acd061d8eae4bf13bc6f9f7fc07794e9b'
AIR_HASH = 'a858cdfb6fef8374c586b86ce4ff6547af6acf4f9da40206c8726c8e88a1ccb9'
BODY_HASH = 'd61ae067525ba3d6e6cbc861867897e21a8634bdeb845509300f907e27ab6d1a'
LLVM = Path('/opt/homebrew/opt/llvm/bin/llvm-dis')
TOOLS = Path(__file__).resolve().parent
PARAMS = [0,260,3,1,256,1,1,2,2,1,1,0]
DRIVER = [4,1,1,1,0,0,0,64,64,1,1,6]

def digest(data):
    return hashlib.sha256(data).hexdigest()

def body(text):
    start = text.index('define void @padding_gradient(')
    return text[start:text.index('\n}\n', start)+3]

def check_manifest(path):
    m = json.loads(Path(path).read_text())
    if m.get('entry') != ENTRY or m.get('input', {}).get('sha256') != LIBRARY_HASH:
        raise ValueError('wrong actual AIR selection or library hash')
    native540.validate(Path(path).parent, m)
    v = m['variants'][0]
    export_program.validate_reflection(v['info'])
    p = v['info']
    if (len(m['variants']) != 1 or p['codeWords'] > 512 or p['bufferMask'] != 7 or
        p['writeMask'] != 2 or p['descriptionWords'][44:52] != [4,4,4,0,0,0,0,0] or
        p['sharedBytes'] or p['threadgroupMask'] or p['driverBase'] != 112 or p['driverCount'] != 12):
        raise ValueError('selected compiler contract changed; no reflection edits permitted')
    return m

def build(output, private, reuse=None, library=LIBRARY):
    output, private, library = Path(output).absolute(), Path(private).absolute(), Path(library).resolve()
    if output.exists() or output.is_symlink():
        raise ValueError('refusing to replace existing fixture')
    if TOOLS == private or TOOLS in private.resolve().parents:
        raise ValueError('Apple input directory must be outside tools')
    if native540.sha(library) != LIBRARY_HASH or library.stat().st_size != 653424:
        raise ValueError('wrong 21H461 original metallib')
    provenance = json.loads((native540.ROOT/'build.json').read_text())
    for name, binary in [('compiler_sha256', native540.ROOT/'native540-compiler'),
                         ('backend_sha256', native540.ROOT/'obj/gpu-compiler/host/src/freedreno/QcomGPUCompiler.dylib')]:
        if native540.sha(binary) != provenance[name]:
            raise ValueError('fixed compiler/backend changed')
    proc = subprocess.run([native540.ROOT/'native540-compiler','--list',library],capture_output=True,text=True,timeout=120)
    if proc.returncode:
        raise ValueError(proc.stderr)
    matches = [f for f in json.loads(proc.stdout) if f['name'] == ENTRY]
    if len(matches) != 1 or matches[0]['metalType'] != 2:
        raise ValueError('not unique real compute AIR')
    f = matches[0]
    air = library.read_bytes()[f['offset']:f['offset']+f['bytes']]
    if digest(air) != AIR_HASH:
        raise ValueError('selected original AIR changed')
    private.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='air-inspect-',dir=private) as name:
        tmp = Path(name)
        (tmp/'selected.air').write_bytes(air)
        dis = subprocess.run([LLVM,tmp/'selected.air','-o',tmp/'selected.ll'],capture_output=True,text=True,timeout=120)
        if dis.returncode:
            raise ValueError(dis.stderr)
        text = (tmp/'selected.ll').read_text()
        if digest(body(text).encode()) != BODY_HASH:
            raise ValueError('actual LLVM body differs from reviewed copy semantics')
        if 'air.function_constant' in text or 'air.threadgroup' in text:
            raise ValueError('specialization/shared feature not allowed')
    manifest = Path(reuse).resolve() if reuse else native540.compile_library(library,ENTRY,private/'compiled')
    m = check_manifest(manifest)
    if m['source'] != provenance:
        raise ValueError('compiled source provenance differs from fixed compiler')
    # Existing exporter performs its entire 621-word and ISA-hash gate unchanged.
    export_program.export_program(manifest, output)
    header = (output/'program.h').read_text().replace('namespace Native540Export {','namespace NativeMetal540AIRFixture {')
    prefix, ending = header.rsplit('}\n',1)
    extra = r"""
// Only caller-owned inputs and comparison oracle; no output BO writes/submits.
struct PaddingParams { uint32_t op, sourceDims[3], resultDims[3], padX[2], padY[2], constantValue; };
static_assert(sizeof(PaddingParams)==48,"actual AIR parameter byte size");
static_assert(__builtin_offsetof(PaddingParams,sourceDims)==4,"actual sourceDims offset");
static_assert(__builtin_offsetof(PaddingParams,resultDims)==16,"actual resultDims offset");
static_assert(__builtin_offsetof(PaddingParams,padX)==28,"actual padX offset");
static_assert(__builtin_offsetof(PaddingParams,padY)==36,"actual padY offset");
static_assert(__builtin_offsetof(PaddingParams,constantValue)==44,"actual constantValue offset");
inline constexpr PaddingParams params={0,{260,3,1},{256,1,1},{2,2},{1,1},0};
inline constexpr uint32_t sourceWords=780, sourceBytes=3120, outputWords=256, outputBytes=1024;
inline constexpr uint32_t outputAllocationBytes=4096, guardWords=768, sentinel=0x13579bdfu;
inline constexpr uint32_t driverWords[]={4,1,1,1,0,0,0,64,64,1,1,6};
inline constexpr NativeMetal540::Dispatch dispatch={{4,1,1},{64,1,1},{}};
inline constexpr uint32_t inputPattern(uint32_t i) { return 0x3f000000u|((i*0x0019660du+0x00034567u)&0x007fffffu); }
inline constexpr uint32_t sourceIndex(uint32_t x,uint32_t y=0,uint32_t z=0) { return x+params.padX[0]+params.sourceDims[0]*(y+params.padY[0]+params.sourceDims[1]*z); }
// Float load/store with no arithmetic: exact finite-normal input bits copied.
inline constexpr uint32_t expected(uint32_t x) { return x<outputWords?inputPattern(sourceIndex(x)):sentinel; }
static_assert(sizeof(code)/sizeof(code[0])==program.info.codeWords,"untouched compiler ISA size");
static_assert(program.info.codeWords<=512,"existing direct shader bound");
"""
    (output/'NativeMetal540AIRFixture.h').write_text(prefix+extra+'}\n'+ending)
    shutil.copyfile(manifest,output/'compiled-program.json')
    shutil.copyfile(manifest.parent/'program.base.ir3',output/'program.base.ir3')
    info = m['variants'][0]['info']
    geometry={67:64,71:1,75:1,72:256,73:1,74:1,76:4,77:1,78:1,96:3120,97:1024,98:48}
    constants=[0]*(info['constlen']*4)
    for i,v in geometry.items(): constants[i]=v
    constants[112:124]=DRIVER
    constants[info['immediateBase']:info['immediateBase']+info['immediateCount']]=m['variants'][0]['immediates']
    receipt=dict(schema=1,entry=ENTRY,route='AIR-NIR-A540-IR3',runtimeVerified=False,deviceOperated=False,
        metallib=m['input'],air=dict(f,sha256=AIR_HASH,llvmBodySHA256=BODY_HASH),
        specializationValues=[],dynamicSharedBytes=0,reflectionEdited=False,
        compileReuse=str(manifest) if reuse else None,strictExporterSHA256=native540.sha(TOOLS/'export_program.py'),
        helperSHA256=native540.sha(Path(__file__)),compiler=m['source'],
        llvm=dict(path=str(LLVM),sha256=native540.sha(LLVM)),
        reviewedSemantics='if x<resultDims[0] && y<resultDims[1] && z<resultDims[2]: dst[(z*resultDims[1]+y)*resultDims[0]+x]=src[((z*sourceDims[1]+y+pad_y[0])*sourceDims[0])+x+pad_x[0]]; float bits copied; no arithmetic',
        paramsWords=PARAMS,bindings=[dict(slot=0,bytes=3120,writable=False),dict(slot=1,bytes=1024,allocationBytes=4096,writable=True),dict(slot=2,bytes=48,writable=False)],
        dispatch=dict(groups=[4,1,1],local=[64,1,1],invocations=256),
        oracle=dict(input='0x3f000000 | ((i*0x0019660d+0x00034567)&0x007fffff)',sourceIndex='x+262',outputWords=256,guardWords=768,sentinel=0x13579bdf,comparisonOnly=True),
        driverConstants=dict(base=112,count=12,words=DRIVER),constantWords=constants,
        typedMetadata={k:info[k] for k in export_program.TYPED_FIELDS},strides=info['descriptionWords'][44:52],
        limitations=['host-only compiler/metadata/oracle checks; not hardware execution','no system Metal device/library/queue or IOSurface adapter','future owner must attest upload/cache/lifetime/physical disjointness and compare actual readback; never seed expected output'],
        hashes={f.name:native540.sha(f) for f in output.iterdir() if f.is_file()})
    (output/'fixture-manifest.json').write_text(json.dumps(receipt,indent=2)+'\n')
    return output/'NativeMetal540AIRFixture.h'

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('output',type=Path);p.add_argument('--private',type=Path,required=True)
    p.add_argument('--reuse-compiled',type=Path);p.add_argument('--library',type=Path,default=LIBRARY)
    a=p.parse_args()
    try: print(build(a.output,a.private,a.reuse_compiled,a.library))
    except (ValueError,OSError,KeyError,TypeError,subprocess.TimeoutExpired) as ex: p.exit(1,str(ex)+'\n')
if __name__=='__main__': main()
