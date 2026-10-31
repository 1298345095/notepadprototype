#!/usr/bin/env bash
set -euo pipefail
# bash build.sh /path/to/extracted/scintilla /path/to/gtk-control-executable
here="$(cd "$(dirname "$0")" && pwd)"
src="$(cd "$1" && pwd)"
out="$2"
make -C "$src/gtk" -j"${BUILD_JOBS:-4}" GTK3=1 static
c++ -std=c++17 -O2 -DGTK $(wx-config --cxxflags) $(pkg-config --cflags gtk+-3.0) \
    -I"$src/include" "$here/probe.cpp" "$src/bin/scintilla.a" \
    $(wx-config --libs core,base) $(pkg-config --libs gtk+-3.0 gmodule-no-export-2.0) -o "$out"
