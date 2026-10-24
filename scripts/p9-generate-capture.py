#!/usr/bin/env python3
from pathlib import Path
import hashlib,json
r=Path(__file__).resolve().parents[1];o=r/'drivers/p9';src=(o/'upstream-panic-screen.c').read_text()
# Reuse upstream's actual formatter, readable-pointer checks and glyph painter.
# Source hooks avoid its fixed entry/code-cave assumptions and extra entry bars.
body=src[:src.index('/* x0 = fmt as for panic();')]
body=body.replace('#define SCALE   3','#define SCALE   2').replace('#define PANIC_TEXT_BYTES 256','#define PANIC_TEXT_BYTES 4096')
body=body.replace('for (i = 0; i < 2000 && fmt[i]; i++) {','for (i = 0; i < 2000; i++) {\n    if (!readable(fmt+i,args)) { text_invalid_format: break; }\n    if (!fmt[i]) break;')
body=body.replace('text_invalid_format: break;', 'break;')
# Darwin's assembler treats ';' as a comment, not an instruction separator.
# Keep the hardware translation query AND the PAR read in the actual object.
body=body.replace('at s1e1r, %1; isb; mrs %0, par_el1',r'at s1e1r, %1\n\tisb\n\tmrs %0, par_el1')
# Invalid/missing argument storage must not recursively fault the first panic.
body=body.replace('u64 v = args[used++];','if (!readable((const char*)(args+used),args) || !readable((const char*)(args+used)+7,args)) break;\n    u64 v = args[used++];')
body=body.replace('precision = (u32)args[used++];','if (!readable((const char*)(args+used),args) || !readable((const char*)(args+used)+7,args)) break;\n        precision = (u32)args[used++];')
(o/'Renderer.inc').write_text(body)
(o/'provenance.json').write_text(json.dumps(dict(upstream='UniXNU/UniXNU-sheng',commit='0c2046b84dbee545e7e3923eb74d7bb1ce020978',source='scripts/xnu-panic-screen.c',sha256=hashlib.sha256(src.encode()).hexdigest(),adaptations=['Use source panic funnel hook, no fixed offsets/code caves/reset probes','2x glyph scale on 1080p display','Guard format and argument reads, including precision slots','Replace semicolon instruction separators with explicit newlines for Darwin assembler; verify actual AT/ISB/MRS machine code','Permanent first-fatal record and caller/state/stack capture']),indent=2)+'\n')
