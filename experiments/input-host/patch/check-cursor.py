"""Confirm the widget sees identical-string cursor changes actually emitted by Fcitx."""
import pathlib
import re
import sys

out = pathlib.Path(sys.argv[1])
wire = (out/"editor-wire.log").read_text(errors="replace").splitlines()
app = (out/"editor.log").read_text(errors="replace").splitlines()
snap = [s.split() for s in (out/"stages.txt").read_text().splitlines()]
if not any(s == "endpoint=patched-fltk-real-fork-document" for s in app):
    sys.exit("Expected the patched input-batch endpoint")
def wire_events(lines):
    rx = r'\.preedit_string\("([^"]*)",\s*(-?\d+),\s*(-?\d+)\)'
    return [(m[0].encode().hex(),int(m[1]),int(m[2])) for line in lines for m in re.findall(rx,line)]
def app_events(lines):
    rx = r'^preedit begin=(-?\d+) end=(-?\d+) hex=([0-9a-f]*)$'
    return [(m[2],int(m[0]),int(m[1])) for line in lines for m in re.findall(rx,line)]
for start,end in zip(snap,snap[1:]):
    w = wire_events(wire[int(start[2]):int(end[2])])
    a = app_events(app[int(start[1]):int(end[1])])
    print(f"{start[0]} -> {end[0]} wire={w} widget={a}")
    if not w:
        print("INCONCLUSIVE: no cursor change delivered by engine during arrow key")
        sys.exit(77)
    if w[-1] not in a:
        sys.exit("FAIL: final preedit range sent by engine never reached widget")
events = app_events(app)
if not any(a[0] and a[0] == b[0] and a[1:] != b[1:] for a,b in zip(events, events[1:])):
    print("INCONCLUSIVE: engine did not produce unchanged-text cursor movement")
    sys.exit(77)
print("PASS: same-text preedit cursor change reached patched widget")
