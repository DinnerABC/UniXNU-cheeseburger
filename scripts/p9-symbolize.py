#!/usr/bin/env python3
from pathlib import Path
import sys,re,json,bisect
r=Path(__file__).resolve().parents[1];table=(r/'out/p9/kernel/tool-validation/symbols.log').read_text();rows=[]
for line in table.splitlines():
 if re.match(r'^[0-9a-fA-F]+ ',line) and '(__TEXT_EXEC,__text)' in line:rows.append((int(line.split()[0],16),line.rsplit(' ',1)[-1]))
rows.sort();keys=[x[0] for x in rows];result=[]
for item in sys.argv[1:]:
 if not re.fullmatch(r'(0x)?[0-9a-fA-F]{8,16}',item):raise SystemExit('Exact hexadecimal PC/LR required; no OCR correction is guessed')
 address=int(item,16);i=bisect.bisect_right(keys,address)-1
 if i<0 or i+1==len(rows):result.append(dict(address=hex(address),result='Outside bounded code symbol range'));continue
 start,name=rows[i];end=rows[i+1][0];result.append(dict(address=hex(address),symbol=name,offset=hex(address-start),next_symbol=hex(end)))
print(json.dumps(result,indent=2))
