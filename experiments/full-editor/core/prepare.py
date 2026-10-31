"""Prepare a UTF-8-only Linux experiment from the exact Notepad4 sources."""
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys

revision = 'bc2fb59cd927288fc07bcdae48e91780cc5139d6'
here = Path(__file__).resolve().parent
checkout, out = map(lambda arg: Path(arg).resolve(), sys.argv[1:])
actual = subprocess.check_output(['git', '-C', checkout, 'rev-parse', 'HEAD'], text=True).strip()
if actual != revision:
    raise SystemExit(f'Expected Notepad4 {revision}, got {actual}')

def edit(name, old, new):
    path = out / name
    text = path.read_text()
    if text.count(old) != 1:
        raise SystemExit(f'Expected one {old!r} in {name}')
    path.write_text(text.replace(old, new))

digest = hashlib.sha256(Path(__file__).read_bytes() + (here / 'ParallelPortable.h').read_bytes()).hexdigest()
stamp = out / '.source-id'
if stamp.exists() and stamp.read_text() == revision + digest:
    sys.exit()
for directory in ('include', 'src', 'lexlib', 'lexers'):
    shutil.copytree(checkout / 'scintilla' / directory, out / directory, dirs_exist_ok=True)
edit('include/VectorISA.h', '#include <intrin.h>', '#if defined(__i386__) || defined(__x86_64__)\n#include <immintrin.h>\n#endif')
edit('include/VectorISA.h', '#if NP2_TARGET_ARM\n#if defined(__clang__)', '#if defined(__GNUC__) || defined(__clang__)\n#if defined(__clang__)')
edit('include/VectorISA.h', '#if NP2_TARGET_ARM\ninline bool bittest', '#if !defined(_MSC_VER)\ninline bool bittest')
edit('src/ElapsedPeriod.h', '//#include <chrono>', '#include <chrono>')
edit('src/ElapsedPeriod.h', '#if 0', '#if 1')
edit('src/Document.cxx', '#include <windows.h>', '#include <cwchar>')
edit('src/Document.cxx', 'unsigned len = ::MultiByteToWideChar(dbcsCodePage, 0, charBytes, widthCharBytes, charInfo.buffer, 2);',
     'unsigned len = 0; // Legacy code page input is outside this UTF-8 experiment.')
shutil.copyfile(here / 'ParallelPortable.h', out / 'src/ParallelSupport.h')
edit('src/EditModel.h', 'void *idleTaskTimer;', 'mutable std::chrono::steady_clock::time_point idleTaskDeadline;')
path = out / 'src/EditModel.cxx'
text = path.read_text()
start = text.index('#if _WIN32_WINNT')
end = text.index('SetIdleTaskTime(IdleLineWrapTime);', start)
path.write_text(text[:start] + 'hardwareConcurrency = 1; // UI-thread glyph measurement on both hosts.\n\t' + text[end:])
edit('src/EditModel.cxx', '\tCloseHandle(idleTaskTimer);\n', '')
edit('src/EditModel.cxx', 'LARGE_INTEGER dueTime;\n\tdueTime.QuadPart = -INT64_C(10*1000)*milliseconds; // convert to 100ns\n\tSetWaitableTimer(idleTaskTimer, &dueTime, 0, nullptr, nullptr, false);',
     'idleTaskDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);')
edit('src/EditModel.cxx', 'return WaitableTimerExpired(idleTaskTimer);', 'return std::chrono::steady_clock::now() >= idleTaskDeadline;')
path = out / 'src/EditView.cxx'
text = path.read_text().replace('void * const idleTaskTimer = model.idleTaskTimer;', 'const auto idleTaskDeadline = model.idleTaskDeadline;')
path.write_text(text.replace('WaitableTimerExpired(idleTaskTimer)', 'std::chrono::steady_clock::now() >= idleTaskDeadline'))
edit('src/UniqueString.h', '#pragma once', '#pragma once\n#include <cstring>')
edit('src/Editor.cxx', '#else\n\t\tPTP_WORK work', '#elif USE_WIN32_PTP_WORK\n\t\tPTP_WORK work')
edit('src/Platform.h', '#else\n#undef PLAT_WIN\n#define PLAT_WIN 1', '#elif defined(_WIN32)\n#undef PLAT_WIN\n#define PLAT_WIN 1')
path = out / 'lexlib/LexerModule.cxx'
text = path.read_text()
start = text.index('const LexerModule * const lexerCatalogue[] = {')
end = text.index('};', start)
path.write_text(text[:start] + 'const LexerModule * const lexerCatalogue[] = {&lmJSON, &lmNull' + text[end:])
stamp.write_text(revision + digest)
