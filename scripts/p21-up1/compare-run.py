"""Compare an already recovered UP1 log with frozen R17 D2; no device access."""
from pathlib import Path
import json,re,sys
R=Path(__file__).resolve().parents[2];O=R/'out/p21/upstream-a';A=R/'out/p21/persistent-logs/20261026T074907Z';B=Path(sys.argv[1]).resolve()
anchors={'S':0x19e541f18-0x9da8000,'C':0x21ca26a1c}
def summarize(root):
 d=next((root/'decoded').glob('cheeseburger-P21-*'));text=(d/'slots.txt').read_text();chunks=[]
 for ch in re.split(r'(?=^#slot )',text,flags=re.M):
  h=re.search(r'^#slot (\d+) t=(\d+)s',ch,re.M)
  if h:chunks.append((int(h[2]),ch))
 window=[x for x in chunks if x[0]<=485];latest=window[-1] if window else (0,'');wait={r:[] for r in anchors}
 for t,ch in window:
  for m in re.finditer(r'^STK ([SC]) p\d+ tid=(\d+) .*slide=([0-9a-f]+).*\n((?:FP[^\n]*\n){0,4})',ch,re.M):
   role,tid,slide,frames=m.groups();values=[]
   for line in frames.splitlines():values.extend(line.split()[3:])
   if any(int(x,16)-int(slide,16)==anchors[role] for x in values if int(x,16)):wait[role].append(t)
 swaps=[tuple(map(int,m)) for _,ch in window for m in re.findall(r'^SWAP check=(\d+) reject=(\d+) why=\d+ transfer=(\d+)',ch,re.M)]
 return {'source':str(root),'retrieval':json.loads((root/'retrieval.json').read_text()),'observed_through_s':chunks[-1][0] if chunks else 0,'comparison_last_s':latest[0],'display':re.findall(r'^DISPLAY .*',latest[1],re.M),'swap_max':{'check':max((x[0] for x in swaps),default=0),'reject':max((x[1] for x in swaps),default=0),'transfer':max((x[2] for x in swaps),default=0)},'watchdog':re.findall(r'^WATCHDOG .*',latest[1],re.M),'up1_publication':re.findall(r'^UP1 .*',latest[1],re.M),'processes':re.findall(r'^(?:SpringBoard|chronod|liveactivitiesd|identityservicesd|identityservice|locationd)[^\n]*',latest[1],re.M),'wait_anchor_snapshots':{r:{'count':len(v),'first_s':min(v) if v else None,'last_s':max(v) if v else None} for r,v in wait.items()},'ui_fault_summary':re.findall(r'^(?:PreBoard|backboardd|SpringBoard|watchdogd) fault exits=.*',latest[1],re.M)}
result={'A':summarize(A),'B':summarize(B),'limits':['Process absence and policy-file publication alone do not prove launchd acceptance','Same unslid stack anchors; absence is not proof of call completion','Physical non-black frame and UI still need user/device evidence','No automatic policy promotion or repeated boot']};(O/'runtime-comparison.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
