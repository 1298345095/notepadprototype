"""Prepare the pinned Notepad4 source in a private build tree. Do not edit checkout."""
import pathlib
import shutil
import subprocess
import sys

revision = "bc2fb59cd927288fc07bcdae48e91780cc5139d6"
source = pathlib.Path(sys.argv[1]).resolve()
dest = pathlib.Path(sys.argv[2]).resolve()
if (source / ".git").exists():
    actual = subprocess.check_output(["git", "-C", source, "rev-parse", "HEAD"], text=True).strip()
    if actual != revision:
        sys.exit(f"Expected Notepad4 {revision}, got {actual}")
source = source / "scintilla"
for directory in ["src", "include", "lexlib", "lexers"]:
    shutil.copytree(source / directory, dest / directory, dirs_exist_ok=True)

def edit(name, replacements):
    path = dest / name
    contents = path.read_text()
    for before, after in replacements:
        if before not in contents:
            sys.exit(f"Unsupported source revision in {name}")
        contents = contents.replace(before, after, 1)
    path.write_text(contents)

edit("include/VectorISA.h", [
    ("#include <intrin.h>", "#if defined(__i386__) || defined(__x86_64__)\n#include <immintrin.h>\n#endif"),
    ("#if NP2_TARGET_ARM\n#if defined(__clang__)", "#if defined(__GNUC__) || defined(__clang__)\n#if defined(__clang__)"),
    ("#if NP2_TARGET_ARM\ninline bool bittest", "#if !defined(_MSC_VER)\ninline bool bittest"),
])
edit("src/ElapsedPeriod.h", [("//#include <chrono>", "#include <chrono>"), ("#if 0", "#if 1")])
# The host accepts only UTF-8. Exclude the dormant Windows DBCS and UTF-16 regex paths.
edit("src/Document.cxx", [
    ("if (enableUrlHighlight) {", "if constexpr (false) { // URL decoration requires ScintillaBase."),
    ("#include <windows.h>", "#include <cwchar>"),
    ("unsigned len = ::MultiByteToWideChar(dbcsCodePage, 0, charBytes, widthCharBytes, charInfo.buffer, 2);",
     "unsigned len = 0; // Non-UTF-8 conversion is outside this host's input contract."),
])
