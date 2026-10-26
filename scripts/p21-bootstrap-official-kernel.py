#!/usr/bin/env python3
"""Official bootstrap compile-record pass only; no prelink, package or boot."""
from pathlib import Path
import os,json,subprocess,sys,time
R=Path(__file__).resolve().parents[1];U=R/'.work/p21-official-framework';O=R/'out/p21/official-adaptation'
sys.path.insert(0,str(U/'scripts'))
from unixnu.cli import parser
from unixnu.config import Context
from unixnu import feeds,kernel,patches
args=parser().parse_args(['tree','-D',str(R),'--out',str(O/'resolved')]);ctx=Context(args);modules=feeds.resolve(ctx)
assert ctx.example,'Keep full build/image guarded during bootstrap'
env=dict(os.environ,DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer')
env.update(kernel.xnu_env(ctx));env.update(kernel.version_env(modules,ctx))
env.update({'XNU_BOOTSTRAP':'1','XNU_BOOT_PROGRESS':'0','UNIXNU_PATCH_LIST':str(patches.prepare(ctx,modules)),'JOBS':'6','PATH':'/opt/homebrew/opt/python@3.13/libexec/bin:/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/bin:/opt/homebrew/opt/llvm/bin:/opt/homebrew/opt/lld/bin:/opt/homebrew/opt/unifdef/bin:'+os.environ['PATH']})
for key in ['HTTP_PROXY','HTTPS_PROXY','ALL_PROXY','http_proxy','https_proxy','all_proxy']:env.pop(key,None)
label=sys.argv[1] if len(sys.argv)>1 else '01';assert label.isdigit();log=O/f'kernel-bootstrap-{label}.log';assert not log.exists(),'Choose a new attempt label; retain old logs'
started=time.time()
with log.open('w') as f:result=subprocess.run(['bash',str(ctx.tree/'scripts/build-xnu-4k-baseline.sh')],cwd=ctx.tree,env=env,stdout=f,stderr=subprocess.STDOUT)
record=ctx.tree/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/osfmk/DEVELOPMENT/startup.o.json'
report={'exit_code':result.returncode,'bootstrap_only':True,'compile_record_exists':record.exists(),'duration_seconds':time.time()-started,'board_guard_retained':ctx.example,'no_prelink_or_image':True,'phone_operations':[]}
(O/f'kernel-bootstrap-{label}.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));sys.exit(0 if record.exists() else result.returncode)
