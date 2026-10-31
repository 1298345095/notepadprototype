import pathlib, re, sys
p = pathlib.Path(sys.argv[1])
wire = (p/"editor-wire.log").read_text(errors="replace")
edits = (p/"editor.log").read_text()
recv = re.findall(r'zwp_text_input_v3[@#]\d+\.preedit_string\("([^"]*)",\s*(-?\d+),\s*(-?\d+)\)', wire)
commits = re.findall(r'zwp_text_input_v3[@#]\d+\.commit_string\("([^"]*)"\)', wire)
print("input_method_engine=pinyin")
print("preedit_and_cursor_from_wire=", recv)
print("commits_from_wire=", commits)
stages = [(name, int(a), int(b)) for name, a, b in
          (line.split() for line in (p/"stages.txt").read_text().splitlines())]
wlines, elines = wire.splitlines(), edits.splitlines()
for (old, ea, wa), (new, eb, wb) in zip(stages, stages[1:]):
    calls = [l for l in wlines[wa:wb] if ".preedit_string(" in l]
    editcount = sum(l.startswith("edit ") for l in elines[ea:eb])
    print(f"{old} -> {new}: widget_edits={editcount} raw_preedit={calls}")
cursor_moves = [(a,b) for a,b in zip(recv,recv[1:]) if a[0] and a[0] == b[0] and a[1:] != b[1:]]
print("unchanged_text_with_cursor_update=", cursor_moves)
records = re.findall(r'^edit marked=(\d+) replace=\d+ incoming=([0-9a-f]*) doc=([0-9a-f]*)$', edits, re.M)
if not recv or not any(c for c in commits):
    print("INCONCLUSIVE: real text-input protocol produced no preedit or no commit")
    sys.exit(77)
if not records:
    print("FAIL: active protocol produced no fork document edits")
    sys.exit(1)
final = bytes.fromhex(records[-1][2]).decode()
print("final_fork_document=", repr(final), "marked_updates=", sum(int(r[0])>0 for r in records))
if "你好" not in commits:
    print("INCONCLUSIVE: engine's space-selected candidate differed from the fixture")
    sys.exit(77)
if final != "你好" or not any(int(r[0]) for r in records) or "invalid_replace=" in edits:
    print("FAIL: ordinary commit/cancel or FLTK's preedit mapping differs")
    sys.exit(1)
print("PASS: real Fcitx Pinyin commit and subsequent cancel reached the fork document")
