#!/bin/bash
set -euo pipefail
root="$PWD"
here="$(cd "$(dirname "$0")" && pwd)"
[ "$(uname -s)/$(uname -m)" = "Darwin/arm64" ] || { echo 'Requires Apple Silicon macOS'; exit 2; }
tmp="$(mktemp -d "${TMPDIR:-/tmp}/np4-cocoa.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT
src="$tmp/fork"
out="$root/out-cocoa"
mkdir -p "$out"
python3 "$here/prepare.py" "$root/deps/notepad4" "$src"
app="$tmp/Notepad4 Cocoa Probe.app"
mac="$app/Contents/MacOS"
mkdir -p "$mac" "$app/Contents/Resources"
cp "$root/deps/notepad4/scintilla/License.txt" "$app/Contents/Resources/Scintilla-License.txt"
cp "$root/deps/notepad4/License.txt" "$app/Contents/Resources/Notepad4-License.txt"
cat > "$app/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>org.notepadprototype.CocoaProbe</string>
<key>CFBundleExecutable</key><string>np4-cocoa-document</string>
<key>CFBundleName</key><string>Notepad4 Cocoa Probe</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleVersion</key><string>1</string>
<key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST
doc=(CellBuffer CharClassify CaseConvert CaseFolder ChangeHistory Decoration Document PerLine RESearch RunStyles UndoHistory UniConversion)
lex=(Accessor DefaultLexer LexAccessor LexerBase LexerModule PropSetSimple WordList)
sources=()
for name in "${doc[@]}"; do sources+=("$src/src/$name.cxx"); done
for name in "${lex[@]}"; do sources+=("$src/lexlib/$name.cxx"); done
sources+=("$src/lexers/LexJSON.cxx")
flags=(-arch arm64 -std=c++20 -O2 -DNDEBUG -DNO_CXX11_REGEX -ffunction-sections -fdata-sections -include cstdint "-I$src/src" "-I$src/include" "-I$src/lexlib")
exe="$mac/np4-cocoa-document"
clang++ "${flags[@]}" -fobjc-arc "${sources[@]}" "$here/probe.mm" -framework Cocoa -Wl,-dead_strip -o "$exe"
strip -x "$exe"
codesign --force --sign - "$app"
"$exe" --self-test "$out/render.png"
ditto -c -k --keepParent "$app" "$out/np4-cocoa-document-arm64.zip"
file "$exe"
otool -L "$exe"
echo "MachOBytes=$(stat -f%z "$exe") BundleZIPBytes=$(stat -f%z "$out/np4-cocoa-document-arm64.zip")"
shasum -a 256 "$exe" "$out/np4-cocoa-document-arm64.zip"
