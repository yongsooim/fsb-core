#!/usr/bin/env python3
"""Compare complete recorded outputs; run descriptions/provenance may differ."""
from pathlib import Path
import hashlib,json,sys
left,right,output=map(Path,sys.argv[1:])
excluded={'runtime.json','inventory-fixture.json'}
files=sorted(p.name for p in left.iterdir() if p.is_file() and p.name not in excluded)
rows=[]
for name in files:
 a=left/name;b=right/name;digest=hashlib.sha256(a.read_bytes()).hexdigest()
 rows.append({'file':name,'bytes':a.stat().st_size,'sha256':digest,'equal':b.exists() and hashlib.sha256(b.read_bytes()).hexdigest()==digest})
a=json.loads((left/'runtime.json').read_text());b=json.loads((right/'runtime.json').read_text())
fields=['run_completed','stopped_on_fault','fault','through_event','last_ms','rendered_frames','root_pc','live_dialogues','messages','game_over','game_mode','map_id']
runtime_differences={key:[a.get(key),b.get(key)] for key in fields if a.get(key)!=b.get(key)}
report={'left':str(left.resolve()),'right':str(right.resolve()),'compared_files':len(rows),'excluded_metadata':sorted(excluded),'equal':all(r['equal'] for r in rows) and not runtime_differences,'differences':[r['file'] for r in rows if not r['equal']],'runtime_differences':runtime_differences,'files':rows}
output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(report,indent=2)+'\n');print({k:v for k,v in report.items() if k!='files'})
raise SystemExit(0 if report['equal'] else 1)
