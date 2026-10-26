#!/usr/bin/env python3
"""Offline summary only; retains raw logs and does not infer a wait root cause."""
from pathlib import Path
from collections import Counter
import json,re,sys
root=Path(sys.argv[1]).resolve();decoded=next((root/'decoded').glob('cheeseburger-P21-*'));text=(decoded/'slots.txt').read_text();latest=(decoded/'latest.txt').read_text();msg=(decoded/'msgbuf.txt').read_text(errors='replace')
swaps=re.findall(r'^SWAP check=(\d+) reject=(\d+) why=(\d+) transfer=(\d+) rc=(\S+)',text,re.M)
matching={}
for line in text.splitlines():
 m=re.match(r'M(\d+) ',line)
 if m:matching[int(m[1])]=line
threads={};observed=Counter()
for line in text.splitlines():
 m=re.match(r'STK ([SBC]) p(\d+) tid=(\d+) PC=([0-9a-f]+) LR=([0-9a-f]+) slide=([0-9a-f]+) e=(\d+)',line)
 if m:
  role,pid,tid,pc,lr,slide,error=m.groups();key=(role,pid,tid);threads[key]={'role':role,'pid':int(pid),'tid':int(tid),'pc':pc,'lr':lr,'cache_slide':slide,'read_error':int(error),'frames':[]};observed[(role,pc)]+=1
 m=re.match(r'FP(\d+) ([SBC]) (\d+) ((?:[0-9a-f]+ ?){4})$',line)
 if m:
  offset,role,tid,values=m.groups()
  for key in reversed(threads):
   if key[0]==role and key[2]==tid:
    entry=threads[key];pos=int(offset);frames=entry['frames'];frames.extend(['0']*max(0,pos+4-len(frames)));frames[pos:pos+4]=values.split();break
warnings=list(dict.fromkeys(line for line in msg.splitlines() if re.search(r'FIRST UI|panic\(|Library not loaded|Missing executable|rejecting class|No SEP',line)))
report={'source':str(root),'retrieval':json.loads((root/'retrieval.json').read_text()) if (root/'retrieval.json').exists() else {},'snapshots_with_swap':len(swaps),'max_swap_checks':max((int(x[0]) for x in swaps),default=0),'max_swap_rejects':max((int(x[1]) for x in swaps),default=0),'max_transfers':max((int(x[3]) for x in swaps),default=0),'latest_watchdog':re.findall(r'^WATCHDOG .*',latest,re.M),'latest_vsync':re.findall(r'^VS .*',latest,re.M),'latest_iokit_summary':re.findall(r'^IOKIT .*',latest,re.M),'retained_match_events':len(matching),'accelerator_match_events':[line for line in matching.values() if re.search(r'IOAccelerator|AGX|Metal',line)],'threads':list(threads.values()),'sampled_pc_counts':[{'role':role,'pc':pc,'samples':n} for (role,pc),n in observed.most_common(32)],'kernel_warnings':warnings[:80],'limits':['Live bounded samples, not stopped/atomic stackshots','Per-role thread and frame limits apply','Matching ring and persistent window overwrite old records; inspect counters','No root cause inferred without matching-version symbols']}
(root/'diagnostic-analysis.json').write_text(json.dumps(report,indent=2)+'\n')
(root/'iokit-matching.txt').write_text('\n'.join(matching[k] for k in sorted(matching))+'\n')
(root/'ui-thread-stacks.json').write_text(json.dumps(report['threads'],indent=2)+'\n')
lines=['# Diagnostic overview','',f"SWAP max: check={report['max_swap_checks']} reject={report['max_swap_rejects']} transfer={report['max_transfers']}",f"Retained IOKit events: {len(matching)}; sampled thread identities: {len(threads)}",'','## Latest status','',*report['latest_watchdog'],*report['latest_vsync'],*report['latest_iokit_summary'],'','## Accelerator lookup evidence','',*(report['accelerator_match_events'] or ['No accelerator request captured; absence is not proof that none occurred.']),'','See diagnostic-analysis.json, ui-thread-stacks.json and iokit-matching.txt. Raw logs are preserved.']
(root/'DIAGNOSTIC_OVERVIEW.md').write_text('\n'.join(lines)+'\n');print('Analysis saved:',root/'DIAGNOSTIC_OVERVIEW.md')
