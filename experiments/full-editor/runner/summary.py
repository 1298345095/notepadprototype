"""Print comparable run medians and all failed behavioural assertions."""
from pathlib import Path
from statistics import median
import json
import sys

root, binaries=map(Path,sys.argv[1:])
for ui in ('fltk','wx'):
    print(ui, 'ELF_bytes', (binaries/(ui+'-study')).stat().st_size)
    rows=[json.loads(p.read_text()) for p in sorted(root.glob(ui+'-*/results.json'))]
    for i,r in enumerate(rows,1):
        print('trial',i, 'passed',sum(c['passed'] for c in r['checks']),'of',len(r['checks']), 'failed',[c['label'] for c in r['checks'] if not c['passed']], r['memorySnapshot'])
    for key in ('hit256MedianUs','hit256P95Us','completionMedianUs','completionP95Us'):
        samples=[float(b[key]) for r in rows for b in r['replies'].get('bench',[]) if key in b]
        if samples: print(key,samples,'median',median(samples))
    if rows:
        for key in ('ready','dragplan','selection','ime','completion'):
            print(key, rows[0]['replies'].get(key,{}))
