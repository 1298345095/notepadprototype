#!/usr/bin/env bash
# Build the two static toolkit hosts from pinned source checkouts in a new output directory.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
fork=$(realpath "$1") toolkit=$(realpath "$2") donor=$(realpath "$3") wxsrc=$(realpath "$4")
out=$(realpath -m "$5")
test "$(git -C "$toolkit" rev-parse HEAD)" = a9b1113516ffd15fc7602a6d425a317df30f4720
test "$(git -C "$wxsrc" rev-parse HEAD)" = 749625e0ea4d70d1f75494aa52231d835531a618
mkdir -p "$out/fltk-src" "$out/bin" "$out/licenses"
if [[ ! -f "$out/fltk-src/.np4-patched" ]]; then
    git -C "$toolkit" archive HEAD | tar xf - -C "$out/fltk-src"
    patch -d "$out/fltk-src" -p1 < "$here/../input-host/patch/fltk-wayland-edit.patch"
    touch "$out/fltk-src/.np4-patched"
fi
opts=(-G Ninja -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG -ffunction-sections -fdata-sections' '-DCMAKE_C_FLAGS_RELEASE=-O2 -DNDEBUG -ffunction-sections -fdata-sections' -DCMAKE_EXE_LINKER_FLAGS=-Wl,--gc-sections)
jobs=${BUILD_JOBS:-4}
cmake -S "$out/fltk-src" -B "$out/fltk-sdk" "${opts[@]}" -DFLTK_BACKEND_X11=OFF -DFLTK_BACKEND_WAYLAND=ON -DFLTK_BUILD_FLUID=OFF -DFLTK_BUILD_TEST=OFF -DFLTK_BUILD_GL=OFF -DFLTK_BUILD_FLTK_OPTIONS=OFF
cmake --build "$out/fltk-sdk" --target fltk --parallel "$jobs"
cmake -S "$here" -B "$out/build" "${opts[@]}" -DNOTEPAD4_SRC="$fork" -DFLTK_DIR="$out/fltk-sdk" -DSCINTILLA_DONOR_SRC="$donor" -DWX_STATIC_SRC="$wxsrc"
cmake --build "$out/build" --target fltk-study wx-study --parallel "$jobs"
for ui in fltk wx; do
    cp "$out/build/$ui/$ui-study" "$out/bin/"
    strip --strip-unneeded "$out/bin/$ui-study"
    readelf -d "$out/bin/$ui-study" > "$out/bin/$ui-needed.txt"
    ldd "$out/bin/$ui-study" > "$out/bin/$ui-ldd.txt"
    if grep -Ei 'NEEDED.*lib(fltk|wx_)' "$out/bin/$ui-needed.txt"; then exit 2; fi
done
cp "$fork/License.txt" "$out/licenses/Notepad4.txt"
cp "$donor/License.txt" "$out/licenses/Scintilla.txt"
cp "$fork/scintilla/License.txt" "$out/licenses/Notepad4-Scintilla.txt"
cp "$toolkit/COPYING" "$out/licenses/FLTK.txt"
cp "$wxsrc/docs/licence.txt" "$out/licenses/wxWidgets.txt"
(cd "$out/bin" && sha256sum *-study > SHA256SUMS)
