#!/usr/bin/env bash
set -euo pipefail
# Usage: bash run.sh <scintilla-5.6.8 source directory> <fltk|wx> <fltk-config|wx-config>
src="$(cd "$1" && pwd)"
host="$2"
cfg="$3"
here="$(cd "$(dirname "$0")" && pwd)"
build="${COCOA_PROBE_BUILD:-$PWD/build-mac-probe}"
mkdir -p "$build"
build="$(cd "$build" && pwd)"
if [[ ! -e "$build/Scintilla.framework/Scintilla" ]]; then
    xcodebuild -quiet -project "$src/cocoa/Scintilla/Scintilla.xcodeproj" -target Scintilla \
        -configuration Release -sdk macosx ARCHS="$(uname -m)" \
        CONFIGURATION_BUILD_DIR="$build" OBJROOT="$build/obj" SYMROOT="$build/sym" \
        DYLIB_INSTALL_NAME_BASE=@rpath CODE_SIGNING_ALLOWED=NO
fi
case "$host" in
    fltk) define=-DPROBE_FLTK; link="$("$cfg" --ldflags)" ;;
    wx) define=-DPROBE_WX; link="$("$cfg" --libs core,base)" ;;
    *) echo "expected fltk or wx" >&2; exit 3 ;;
esac
"$cfg" --version
# config scripts intentionally produce whitespace-separated compiler/linker flags.
clang++ -std=c++17 -fobjc-arc $("$cfg" --cxxflags) -std=c++17 "$define" \
    -I"$src/cocoa" -I"$src/include" -F"$build" \
    "$here/probe.mm" -framework Scintilla -framework Cocoa -framework Carbon $link \
    -Wl,-rpath,"$build" -o "$build/probe-$host"
DYLD_FRAMEWORK_PATH="$build" "$build/probe-$host"
