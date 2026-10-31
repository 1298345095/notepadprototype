#!/usr/bin/env bash
# Arguments: clean FLTK git checkout, fork checkout, directory with fork.hpp+prepare.py, study build directory.
set -euo pipefail
source_checkout=$(realpath "$1")
notepad=$(realpath "$2")
helper=$(realpath "$3")
out=$(realpath -m "$4")
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out/fltk"
if [[ ! -f "$out/fltk/.applied-probe" ]]; then
  git -C "$source_checkout" archive a9b1113516ffd15fc7602a6d425a317df30f4720 | tar -x -C "$out/fltk"
  (cd "$out/fltk" && patch --batch --forward -p1 < "$here/fltk-wayland-edit.patch")
  touch "$out/fltk/.applied-probe"
fi
cmake -S "$here" -B "$out/build" -DCMAKE_BUILD_TYPE=Release \
  "-DFLTK_SRC=$out/fltk" "-DNOTEPAD4_SRC=$notepad" "-DCOCOA_PROBE=$helper"
cmake --build "$out/build" --target fltk-edit-probe fltk-patched-pinyin --parallel 3
if [[ -z "${WAYLAND_DISPLAY:-}" ]]; then
  export XDG_RUNTIME_DIR="$out/runtime"
  install -d -m700 "$XDG_RUNTIME_DIR"
  export WAYLAND_DISPLAY="fltk-replay-$$"
  weston --backend=headless-backend.so --socket="$WAYLAND_DISPLAY" --idle-time=0 > "$out/weston.log" 2>&1 &
  pid=$!
  trap 'kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true' EXIT
  for unused in {1..60}; do [[ -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]] && break; sleep .1; done
  [[ -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]]
fi
env -u DISPLAY FLTK_BACKEND=wayland "$out/build/fltk-edit-probe"
