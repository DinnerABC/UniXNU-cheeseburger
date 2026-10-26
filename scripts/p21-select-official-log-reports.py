#!/usr/bin/env python3
"""Select every upstream read-only report whose real providers are compiled.
Never synthesize missing hardware exports. Save exclusions with exact names.
"""
from pathlib import Path
import json,re,subprocess,hashlib,sys
R=Path(__file__).resolve().parents[1];S=R/'package/BoardRuntime';objects=[Path(x) for x in sys.argv[1:]]
output=subprocess.check_output(['/opt/homebrew/opt/llvm/bin/llvm-nm','--defined-only',*map(str,objects)],text=True)
names={line.split()[-1][1:] for line in output.splitlines() if line.split() and line.split()[-1].startswith('_')}
text=(S/'OfficialLogReports.template.inc').read_text();prefix=(S/'BootLog.c').read_text().split('enum {LogBlock')[0]
weak=set()
for decl in re.findall(r'extern\s+([^;]+);',prefix,re.S):
 if 'weak_import' in decl:
  weak.update(w for w in re.findall(r'\b(?:native_|boot_progress_|boot_ui_trace_)\w+\b',decl) if not w.endswith('_record'))
# Mask comments and strings before balancing C braces.
pattern=r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\x27(?:\\.|[^\x27\\])*\x27'
mask=re.sub(pattern,lambda m:''.join('\n' if c=='\n' else ' ' for c in m.group()),text)
replacements=[];sections=[]
for match in re.finditer(r'^  if \(',text,re.M):
 start=match.start();opening=mask.find('{',start);level=1;end=opening+1
 while level and end<len(mask):
  if mask[end]=='{':level+=1
  elif mask[end]=='}':level-=1
  end+=1
 assert not level
 block=text[start:end];used=sorted(weak & set(re.findall(r'\b\w+\b',block)));missing=sorted(set(used)-names)
 label=block.splitlines()[0].strip();sections.append({'condition':label,'enabled':not missing,'providers':used,'not_selected':missing})
 if missing:replacements.append((start,end,'  /* Not selected: '+', '.join(missing)+' */'))
for a,b,new in reversed(replacements):text=text[:a]+new+text[b:]
(S/'OfficialLogReports.inc').write_text(text)
O=R/'out/p21/official-adaptation/runtime-build';(O/'official-log-coverage.json').write_text(json.dumps({'source':'UniXNU-sheng 0c2046b84dbee545e7e3923eb74d7bb1ce020978','template_sha256':hashlib.sha256((S/'OfficialLogReports.template.inc').read_bytes()).hexdigest(),'selected_sha256':hashlib.sha256(text.encode()).hexdigest(),'sections':sections},indent=2)+'\n')
print('Official log reports:',sum(s['enabled'] for s in sections),'selected,',sum(not s['enabled'] for s in sections),'unselected provider groups')
