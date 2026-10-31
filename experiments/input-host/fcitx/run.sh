#!/usr/bin/env bash
set -euo pipefail
exe=$(realpath "$1")
out=$(realpath -m "$2")
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out"
unavailable() { echo "INCONCLUSIVE: $*" | tee "$out/status.txt"; exit 77; }
if [[ "${3:-}" != inner ]]; then
  for prg in dbus-run-session xvfb-run Xvfb sway swaymsg fcitx5 fcitx5-remote xdotool wayland-info; do
    command -v "$prg" >/dev/null || unavailable "Missing $prg"
  done
  exec xvfb-run -a dbus-run-session -- bash "$here/run.sh" "$exe" "$out" inner
fi
tmp=$(mktemp -d)
export XDG_RUNTIME_DIR="$tmp/run" XDG_CONFIG_HOME="$tmp/config" XDG_DATA_HOME="$tmp/data" XDG_CACHE_HOME="$tmp/cache"
mkdir -m 700 -p "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME/fcitx5/conf" "$XDG_DATA_HOME" "$XDG_CACHE_HOME"
trap 'for pid in ${observer_pid:-} ${ime_pid:-} ${sway_pid:-}; do kill "$pid" 2>/dev/null || true; done' EXIT
export XDG_SESSION_TYPE=wayland XDG_CURRENT_DESKTOP=sway LANG=C.UTF-8
unset GTK_IM_MODULE QT_IM_MODULE WAYLAND_DISPLAY SWAYSOCK
cat > "$tmp/sway.conf" <<'SWAY'
xwayland disable
default_border none
focus_follows_mouse no
seat seat0 fallback true
input * xkb_layout us
SWAY
cat > "$XDG_CONFIG_HOME/fcitx5/profile" <<'PROFILE'
[Groups/0]
Name=Default
Default Layout=us
DefaultIM=pinyin
[Groups/0/Items/0]
Name=keyboard-us
[Groups/0/Items/1]
Name=pinyin
[GroupOrder]
0=Default
PROFILE
cat > "$XDG_CONFIG_HOME/fcitx5/conf/pinyin.conf" <<'PINYIN'
PreeditMode=ComposingPinyin
PreeditCursorPositionAtBeginning=False
PINYIN
WLR_BACKENDS=x11 WLR_RENDERER=pixman sway --unsupported-gpu -c "$tmp/sway.conf" > "$out/sway.log" 2>&1 &
sway_pid=$!
for i in {1..100}; do
  ipc=$(find "$XDG_RUNTIME_DIR" -maxdepth 1 -name 'sway-ipc*.sock' -print -quit)
  sock=$(find "$XDG_RUNTIME_DIR" -maxdepth 1 -type s -name 'wayland-*' -print -quit)
  [[ -n "$ipc" && -n "$sock" ]] && break
  sleep 0.1
done
[[ -n "$ipc" && -n "$sock" ]] || unavailable "Sway for nested X11 seat did not start"
export SWAYSOCK="$ipc" WAYLAND_DISPLAY="$(basename "$sock")"
env -u DISPLAY wayland-info > "$out/globals.txt"
for interface in zwp_input_method_manager_v2 zwp_text_input_manager_v3; do
  grep -q "$interface" "$out/globals.txt" || unavailable "Sway lacks $interface"
done
env -u DISPLAY WAYLAND_DEBUG=client fcitx5 > "$out/fcitx.log" 2>&1 &
ime_pid=$!
env -u DISPLAY FLTK_BACKEND=wayland WAYLAND_DEBUG=client "$exe" > "$out/editor.log" 2> "$out/editor-wire.log" &
observer_pid=$!
for i in {1..100}; do
  if grep -q 'ready=real_wayland_surface' "$out/editor.log" && swaymsg -t get_tree -r | grep -q 'NP4 Real Pinyin Probe'; then break; fi
  sleep 0.1
done
grep -q 'ready=real_wayland_surface' "$out/editor.log" || unavailable "Editor did not map a native wl_surface"
swaymsg '[title="^NP4 Real Pinyin Probe$"] focus' > "$out/focus-command.txt"
swaymsg -t get_tree -r > "$out/tree.json"
swaymsg -t get_inputs -r > "$out/inputs.json"
outer=$(timeout 4 xdotool search --onlyvisible --name '.*' | tail -1)
[[ -n "$outer" ]] || unavailable "Failed to identify Sway's virtual X11 output"
timeout 4 xdotool windowfocus "$outer" || unavailable "Could not focus compositor's X11 output"
for i in {1..80}; do
  if fcitx5-remote -o >/dev/null 2>&1 && fcitx5-remote -s pinyin >/dev/null 2>&1 && [[ "$(fcitx5-remote -n 2>/dev/null)" == pinyin ]]; then break; fi
  sleep 0.12
done
fcitx5-remote -n > "$out/engine.txt" || true
grep -qx pinyin "$out/engine.txt" || unavailable "Pinyin was not selected on the current input context"
# Send physical-style XTest key presses to the compositor's outer keyboard seat.
# The app and Fcitx5 have DISPLAY unset and cannot receive X11 events.
for key in n i h a o; do xdotool key "$key"; sleep .12; done
sleep .25
snapshot() {
  printf '%s %s %s\n' "$1" "$(wc -l <"$out/editor.log")" "$(wc -l <"$out/editor-wire.log")" >> "$out/stages.txt"
}
: > "$out/stages.txt"
snapshot nihao
xdotool key Left; sleep .25; snapshot left
xdotool key Right; sleep .25; snapshot right
xdotool key space; sleep .45
# A second composition is canceled to check that no provisional bytes survive.
for key in n i; do xdotool key "$key"; sleep .12; done
xdotool key Escape; sleep .45
python3 "$here/verify.py" "$out" | tee "$out/status.txt"
