"""Compare the same native fork workload on a single Windows runner (Python 3)."""
import argparse
import csv
import hashlib
import os
from pathlib import Path
import re
import statistics
import subprocess
import time

TOOLKITS = {'fltk': 'FLTK', 'wx': 'wxMSW', 'tk': 'Tcl/Tk', 'fox': 'FOX'}
FIELDS = ('load_ms', 'colourise_ms', 'edit_undo_64_ms')
RECORD = re.compile(r'NP4_CORE,([1-5]),4194288,(\d+\.\d+),(\d+\.\d+),(\d+\.\d+)')


def write_csv(path, rows):
    with path.open('w', newline='', encoding='utf-8') as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)


def core_measurements(name, exe, launch):
    env = dict(os.environ, NP4_BENCH_ALL_EVENTS='0')
    proc = subprocess.run([str(exe), '--bench'], cwd=exe.parent, env=env, capture_output=True,
                          timeout=90, text=True, encoding='utf-8', errors='replace')
    if proc.returncode:
        raise RuntimeError(f'{exe.name} exited {proc.returncode}: {proc.stderr}')
    lines = proc.stdout.splitlines()
    if len(lines) != 5:
        raise ValueError(f'{exe.name}: expected 5 NP4_CORE samples, received {lines!r}')
    rows = []
    for trial, line in enumerate(lines, 1):
        match = RECORD.fullmatch(line)
        if not match or int(match[1]) != trial:
            raise ValueError(f'{exe.name}: invalid trial {trial}: {line!r}')
        rows.append(dict(toolkit=name, launch=launch, trial=trial, bytes=4194288,
                         **dict(zip(FIELDS, map(float, match.groups()[1:])))))
    return rows


def startup_probe():
    """Return a native probe: launch until a visible owned editor responds with the sample length."""
    import ctypes as C
    from ctypes import wintypes as W
    u = C.WinDLL('user32', use_last_error=True)
    callback = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    u.EnumWindows.argtypes = [callback, W.LPARAM]
    u.EnumChildWindows.argtypes = [W.HWND, callback, W.LPARAM]
    u.IsWindowVisible.argtypes = [W.HWND]
    u.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
    u.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
    u.SendMessageTimeoutW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM, W.UINT, W.UINT,
                                       C.POINTER(C.c_size_t)]
    u.SendMessageTimeoutW.restype = C.c_ssize_t
    u.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]

    class MemoryCounters(C.Structure):
        _fields_ = [('cb', W.DWORD), ('faults', W.DWORD)] + [
            (key, C.c_size_t) for key in ('peak_ws', 'working_set', 'peak_paged', 'paged',
                                          'peak_nonpaged', 'nonpaged', 'pagefile', 'peak_pagefile')]

    k = C.WinDLL('kernel32', use_last_error=True)
    k.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
    k.OpenProcess.restype = W.HANDLE
    k.CloseHandle.argtypes = [W.HANDLE]
    k.K32GetProcessMemoryInfo.argtypes = [W.HANDLE, C.POINTER(MemoryCounters), W.DWORD]

    def working_set(pid):
        handle = k.OpenProcess(0x0410, False, pid)  # query information + read memory
        if not handle:
            raise C.WinError(C.get_last_error())
        try:
            counters = MemoryCounters()
            counters.cb = C.sizeof(counters)
            if not k.K32GetProcessMemoryInfo(handle, C.byref(counters), counters.cb):
                raise C.WinError(C.get_last_error())
            return counters.working_set
        finally:
            k.CloseHandle(handle)

    def measure(exe):
        start = time.perf_counter_ns()
        proc = subprocess.Popen([str(exe)], cwd=exe.parent, stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        root = None
        try:
            @callback
            def top(hwnd, context):
                nonlocal root
                pid = W.DWORD()
                u.GetWindowThreadProcessId(hwnd, C.byref(pid))
                if pid.value != proc.pid or not u.IsWindowVisible(hwnd):
                    return True

                @callback
                def child(inner, context):
                    nonlocal root
                    cls = C.create_unicode_buffer(128)
                    u.GetClassNameW(inner, cls, len(cls))
                    if cls.value == 'Scintilla' and u.IsWindowVisible(inner):
                        length = C.c_size_t()
                        if u.SendMessageTimeoutW(inner, 2006, 0, 0, 2, 150, C.byref(length)) and length.value == 22:
                            root = hwnd
                            return False
                    return True

                u.EnumChildWindows(hwnd, child, 0)
                return root is None

            while root is None:
                if proc.poll() is not None or time.perf_counter_ns() - start > 20_000_000_000:
                    raise RuntimeError(f'{exe.name}: no visible editor with the 22-byte sample')
                u.EnumWindows(top, 0)
                if root is None:
                    time.sleep(.005)
            elapsed = (time.perf_counter_ns() - start) / 1_000_000
            time.sleep(.300)
            working_set_bytes = working_set(proc.pid)
            if not u.PostMessageW(root, 0x10, 0, 0) or proc.wait(timeout=15) != 0:
                raise RuntimeError(f'{exe.name}: did not exit normally after WM_CLOSE')
            return elapsed, working_set_bytes
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
    return measure


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in TOOLKITS:
        parser.add_argument('--'+name, type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--startup', action='store_true', help='Also measure three warm launches to an observable 22-byte native editor')
    args = parser.parse_args()
    files = {TOOLKITS[k]: getattr(args, k).resolve() for k in TOOLKITS if getattr(args, k)}
    if os.name != 'nt' or not files or not all(p.is_file() for p in files.values()):
        parser.error('Run on Windows with at least one existing native executable')
    args.out.mkdir(parents=True, exist_ok=True)
    names = list(files)
    split = len(names) // 2
    order = names + list(reversed(names)) + names[split:] + names[:split]
    rows = []
    for launch, name in enumerate(order, 1):
        print(f'{launch}/{len(order)} {name} --bench', flush=True)
        rows.extend(core_measurements(name, files[name], launch))
    write_csv(args.out / 'samples.csv', rows)

    summary = ['## Native Windows x64: embedded Notepad4 Win32 control', '',
               'Same runner, 3 fresh processes per available host, 5 timed trials after one warm-up per process. Times are p50 of 15 trials.',
               'Input: 4,194,288 UTF-8 bytes already in memory; JSON lexer and insert/delete notification mask; redraw suspended. Edit column measures 64 replace-and-undo pairs as a batch.', '',
               '| Host | EXE bytes | MiB | Load ms | JSON colourise ms | 64 edit/undo ms | SHA-256 |',
               '|---|---:|---:|---:|---:|---:|---|']
    for name, exe in files.items():
        host_rows = [sample for sample in rows if sample['toolkit'] == name]
        p50 = [statistics.median(row[field] for row in host_rows) for field in FIELDS]
        size = exe.stat().st_size
        digest = hashlib.sha256(exe.read_bytes()).hexdigest()
        summary.append(f'| {name} | {size} | {size/(1024**2):.2f} | {p50[0]:.3f} | {p50[1]:.3f} | {p50[2]:.3f} | `{digest}` |')
    summary.extend(['', 'Inputs are the supplied Release executables. Toolkit build options, features and host notification routing differ. EXE size includes only embedded components. The core measurements exclude startup, disk I/O, UI presentation and interactive input latency.'])

    publish(args.out / 'summary.md', summary, 'w')
    if args.startup:
        measure = startup_probe()
        for exe in files.values():
            measure(exe)  # unrecorded warm-up; no cache flush
        launches = []
        for launch, name in enumerate(order, 1):
            elapsed, working_set = measure(files[name])
            launches.append(dict(toolkit=name, launch=launch, visible_editor_ms=elapsed, working_set_bytes=working_set))
        write_csv(args.out / 'startup.csv', launches)
        section = ['', 'Warm launch: p50 of three fresh processes after one unrecorded start per host, with polling every 5 ms. From process creation until its visible Scintilla HWND answers SCI_GETLENGTH with the 22-byte sample. This endpoint does not await a displayed frame. The instantaneous process working set is sampled 300 ms after that observation, including resident shared and private pages. Each process must exit normally via WM_CLOSE.', '',
                   '| Host | Ready ms | Working set MiB |', '|---|---:|---:|']
        for name in files:
            host = [r for r in launches if r['toolkit'] == name]
            ready = statistics.median(r['visible_editor_ms'] for r in host)
            ws = statistics.median(r['working_set_bytes'] for r in host) / (1024**2)
            section.append(f'| {name} | {ready:.3f} | {ws:.2f} |')
        publish(args.out / 'summary.md', section, 'a')


def publish(path, lines, mode):
    text = '\n'.join(lines) + '\n'
    with path.open(mode, encoding='utf-8') as stream:
        stream.write(text)
    print(text, flush=True)
    if os.getenv('GITHUB_STEP_SUMMARY'):
        with open(os.environ['GITHUB_STEP_SUMMARY'], 'a', encoding='utf-8') as step:
            step.write(text)


if __name__ == '__main__':
    main()
