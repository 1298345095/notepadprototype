"""Drive the focused editor through the compositor seat and sample its public test protocol."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

exe, pid, out = Path(sys.argv[1]), int(sys.argv[2]), Path(sys.argv[3])
qa = Path(os.environ['NP4_QA_DIR'])
reports = []
replies = {}
sequence = 0


def invoke(*args, check=True):
    return subprocess.run(args, text=True, capture_output=True, timeout=5, check=check)


def record(label, ok, detail):
    reports.append(dict(label=label, passed=bool(ok), detail=detail))
    print(f"{'PASS' if ok else 'FAIL'} {label} {detail}", flush=True)


def examine_tree():
    data = json.loads(invoke('swaymsg','-r','-t','get_tree').stdout)
    def node(current):
        if current.get('pid') == pid and current.get('shell') == 'xdg_shell':
            return current
        for child in current.get('nodes', []) + current.get('floating_nodes', []):
            found = node(child)
            if found:
                return found
        return None
    return data, node(data)


def prepare_surface():
    deadline = time.monotonic() + 14
    while time.monotonic() < deadline:
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            raise RuntimeError('editor exited before mapping')
        data, editor = examine_tree()
        if editor:
            break
        time.sleep(.10)
    else:
        raise RuntimeError('editor did not map an xdg-shell toplevel')
    criterion = f"[con_id={editor['id']}]"
    invoke('swaymsg', criterion, 'fullscreen', 'enable')
    invoke('swaymsg', criterion, 'focus')
    time.sleep(.35)
    data, editor = examine_tree()
    (out/'tree.json').write_text(json.dumps(data,indent=2))
    for what in ('inputs','outputs'):
        (out/(what+'.json')).write_text(invoke('swaymsg','-r','-t','get_'+what).stdout)
    if not editor or not editor.get('focused') or not editor.get('fullscreen_mode'):
        raise RuntimeError('editor does not have fullscreen seat focus')
    candidates = invoke('xdotool','search','--onlyvisible','--name','.*').stdout.splitlines()
    if not candidates:
        raise RuntimeError('Sway nesting window not visible on Xvfb')
    outer = candidates[-1]
    invoke('xdotool','windowfocus',outer)
    record('native_surface',True,dict(shell=editor['shell'],rect=editor['rect'],name=editor['name'],pid=pid))
    return outer


def ask(cmd):
    global sequence
    sequence += 1
    temp = qa/'request.tmp'
    temp.write_text(f'{sequence} {cmd}\n')
    os.replace(temp,qa/'request')
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        try:
            lines = (qa/'reply').read_text().splitlines()
        except FileNotFoundError:
            lines = []
        if lines and lines[0].strip() == str(sequence):
            data = dict(line.split('=',1) for line in lines[1:] if '=' in line)
            (out/f'{sequence:02}-{cmd}.txt').write_text('\n'.join(lines) + '\n')
            replies.setdefault(cmd,[]).append(data)
            return data
        time.sleep(.025)
    raise RuntimeError(f'no reply to {sequence} {cmd}')


def mouse(outer,x,y):
    invoke('xdotool','mousemove','--window',outer,str(int(x)),str(int(y)))


def key(symbol):
    invoke('xdotool','key',symbol)
    time.sleep(.09)


def enter_prefix():
    ask('prefix')
    invoke('xdotool','type','--delay','55','np4ProofCan')
    time.sleep(.16)


def set_ime(active):
    for attempt in range(35):
        invoke('fcitx5-remote', '-o' if active else '-c', check=False)
        if active:
            invoke('fcitx5-remote','-s','pinyin',check=False)
        current = invoke('fcitx5-remote','-n',check=False).stdout.strip()
        state = invoke('fcitx5-remote',check=False).stdout.strip()
        if (active and current == 'pinyin') or (not active and (state == '1' or current == 'keyboard-us')):
            return True
        time.sleep(.12)
    return False


def drive():
    outer = prepare_surface()
    set_ime(False)
    r = ask('ready')
    record('fork_2067',r.get('forkChar') == '128512' and r.get('forkWidth') == '4',r)
    mouse(outer,int(r['foldX']),int(r['foldY']))
    time.sleep(.10)
    invoke('xdotool','click','1')
    time.sleep(.16)
    r = ask('dragplan')
    record('real_margin_click_and_softwrap',r.get('folded') == '1' and int(r.get('wraps','0')) >= 2,r)
    x1,x2,y = map(int,(r['x1'],r['x2'],r['y']))
    mouse(outer,x1,y)
    invoke('xdotool','mousedown','1')
    time.sleep(.06)
    for step in range(1,9):
        mouse(outer,round(x1 + (x2-x1)*step/8),y)
        time.sleep(.018)
    invoke('xdotool','mouseup','1')
    time.sleep(.12)
    r = ask('selection')
    record('drag_exact_utf8',r.get('ok')=='1' and r.get('selectedHex','').lower()=='42e4b8adf09f9880',r)
    if shutil.which('grim'):
        image = invoke('grim','-l','2',str(out/'selection.png'),check=False)
        if image.returncode:
            print('Screenshot unavailable:',image.stderr,flush=True)
    enter_prefix()
    r = ask('completion')
    record('real_document_candidates',r.get('active')=='1' and r.get('first')=='np4ProofCandidateAlpha' and
           r.get('listedAlpha')=='1' and r.get('listedBeta')=='1',r)
    key('Down'); key('Return')
    r = ask('accepted')
    record('choose_second_candidate',r.get('ok')=='1',r)
    key('ctrl+z')
    r = ask('undo')
    record('undo_completion',r.get('ok')=='1',r)
    enter_prefix()
    r = ask('completion')
    record('list_open_before_pinyin',r.get('active')=='1',r)
    if not set_ime(True):
        record('native_pinyin',False,'Fcitx5 Pinyin engine unavailable on this seat')
        return
    wire = out/'editor-wire.log'
    offset = wire.stat().st_size
    for letter in 'nihao':
        key(letter)
    time.sleep(.20)
    before = ask('composition')
    key('Left')
    left = ask('composition')
    key('Right')
    right = ask('composition')
    snapshots = [before,left,right]
    record('preedit_cursor_on_fork', [s.get('caretOffset') for s in snapshots] == ['6','5','6'] and
        all(s.get('currentHex') == '6e692068616f' for s in snapshots), snapshots)
    key('space')
    time.sleep(.35)
    r = ask('ime')
    with wire.open('rb') as stream:
        stream.seek(offset)
        traffic = stream.read().decode(errors='replace')
    preedits = re.findall(r'zwp_text_input_v3[@#]\d+\.preedit_string\("([^"]+)",',traffic)
    commits = re.findall(r'zwp_text_input_v3[@#]\d+\.commit_string\("([^"]+)"\)',traffic)
    record('native_pinyin_wire',bool(preedits) and '你好' in commits,dict(preedits=preedits,commits=commits))
    record('pinyin_completion_interaction',r.get('ok')=='1',r)
    record('fork_ime_notifications',int(r.get('sourceTentative','0'))>0 and
           int(r.get('sourceResult','0'))>0 and r.get('wrongPopupAccepts')=='0',
           {name:r.get(name) for name in ('sourceTentative','sourceResult','wrongPopupAccepts')})


try:
    drive()
except Exception as error:
    record('driver',False,repr(error))
finally:
    if replies:
        try:
            # Switch back to ordinary typing. mutate edits the source document and
            # clears the suffix itself; prefix would reset the prepared state.
            if not set_ime(False):
                raise RuntimeError('Could not switch from Pinyin to ordinary typing')
            mutation = ask('mutate')
            invoke('xdotool','type','--delay','55','np4ProofCan')
            time.sleep(.16)
            updated = ask('completion')
            record('document_candidates_track_mutation',mutation.get('ok')=='1' and
                   updated.get('active')=='1' and updated.get('listedAlpha')=='1' and
                   updated.get('listedBeta')=='0' and updated.get('listedZeta')=='1',
                   dict(mutate=mutation, completion=updated))
            ask('prefix')
            invoke('xdotool','type','--delay','65','tr')
            time.sleep(.16)
            keyword=ask('completion')
            record('json_keyword_from_application',keyword.get('active')=='1' and
                   keyword.get('first')=='true' and keyword.get('targetHex')=='7472',keyword)
        except Exception as error:
            record('document_candidates_track_mutation',False,repr(error))
        try:
            data = ask('bench')
            record('same_workload',bool(data) and 'error' not in data and data.get('ok','1')=='1',data)
        except Exception as error:
            record('same_workload',False,repr(error))
    status = Path(f'/proc/{pid}/status')
    usage = []
    if status.exists():
        usage = [s for s in status.read_text().splitlines() if s.startswith(('VmRSS:','VmHWM:'))]
    (out/'results.json').write_text(json.dumps(dict(binaryBytes=exe.stat().st_size,
        memorySnapshot=usage, checks=reports, replies=replies),ensure_ascii=False,indent=2)+'\n')
sys.exit(0 if all(report['passed'] for report in reports) else 1)
