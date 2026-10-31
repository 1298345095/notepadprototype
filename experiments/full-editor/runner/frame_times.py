"""Time from a real IME batch to the next damaged main-surface buffer commit."""
import argparse
import json
import math
from pathlib import Path
import re
import statistics


def samples(path):
    # Decode only protocol metadata, not keyboard dumps into the report.
    trace = path.read_text(errors='replace').splitlines()
    surface_by_role = {}
    for line in trace:
        match = re.search(r'get_xdg_surface\(new id xdg_surface[@#](\d+), wl_surface[@#](\d+)\)', line)
        if match:
            surface_by_role[match[1]] = match[2]
        role = re.search(r'xdg_surface[@#](\d+)\.get_toplevel\(', line)
        if role and role[1] in surface_by_role:
            main_id = surface_by_role[role[1]]
            break
    else:
        return []
    main = re.compile(r' -> wl_surface[@#]' + main_id + r'\.(\w+)\(')
    input_pattern = re.compile(r'(?<! -> )zwp_text_input_v3[@#]\d+\.preedit_string\("([^"]+)"')
    start = pending = None
    attached = damaged = False
    result = []
    for line in trace:
        time = re.match(r'\[\s*([0-9.]+)\]', line)
        if not time:
            continue
        event = input_pattern.search(line)
        if event:
            pending = event[1]
            start = None
        if pending and re.search(r' zwp_text_input_v3[@#]\d+\.done\(', line):
            start = float(time[1])
            pending = None
            attached = damaged = False
        event = main.search(line)
        if not event or start is None:
            continue
        if event[1] == 'attach' and 'wl_buffer' in line:
            attached = True
        elif event[1] in ('damage', 'damage_buffer'):
            damaged = True
        elif event[1] == 'commit':
            if attached and damaged:
                result.append(round(float(time[1]) - start, 3))
                start = None
            attached = damaged = False
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('traces', nargs='+', type=Path)
    args = parser.parse_args()
    by_file = {str(p):samples(p) for p in args.traces}
    values = sorted(v for values in by_file.values() for v in values)
    print(json.dumps(dict(samples=by_file, n=len(values),
                          median_ms=statistics.median(values) if values else None,
                          p95_ms=values[math.ceil(.95*len(values))-1] if values else None)))
