#!/usr/bin/env python3
"""Launch a pinned project-local test profile, exactly one temporary boot."""
import json,subprocess,sys
from pathlib import Path
r=Path(__file__).resolve().parents[2];profile=Path(sys.argv[1]).resolve()
if r not in profile.parents:raise SystemExit('Profile must be in project')
d=json.loads(profile.read_text());assert d['max_attempts']==1
cmd=[sys.executable,str(r/'tools/p5/loop.py'),'boot',str(r/d['image']),'--sha256',d['sha256'],'--observe-seconds',str(d['observe_seconds'])]
raise SystemExit(subprocess.run(cmd,cwd=r).returncode)
