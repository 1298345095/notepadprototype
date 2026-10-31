#!/usr/bin/env bash
# Keys and pointer go to Sway's outer X11 seat. The editor and Fcitx5 see only Wayland.
set -euo pipefail
exe=$(realpath "$1")
out=$(realpath -m "$2")
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out"
if [[ "${3:-}" != inner ]]; then
    if [[ $(id -u) == 0 ]]; then
        echo "Run as a regular desktop user so Sway can create its seat." >&2
        exit 77
    fi
    exec xvfb-run -a -s "-screen 0 1280x900x24" dbus-run-session -- bash "$here/run.sh" "$exe" "$out" inner
fi
tmp=$(mktemp -d)
export XDG_RUNTIME_DIR="$tmp/runtime" XDG_CONFIG_HOME="$tmp/config" XDG_DATA_HOME="$tmp/data" XDG_CACHE_HOME="$tmp/cache"
export XDG_SESSION_TYPE=wayland XDG_CURRENT_DESKTOP=sway LANG=C.UTF-8
unset WAYLAND_DISPLAY SWAYSOCK GTK_IM_MODULE QT_IM_MODULE
install -d -m 700 "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME/fcitx5/conf" "$XDG_DATA_HOME" "$XDG_CACHE_HOME"
export NP4_QA_DIR
NP4_QA_DIR=$(mktemp -d "$out/commands.XXXXXX")
trap 'for pid in ${editor_pid:-} ${ime_pid:-} ${sway_pid:-}; do kill "$pid" 2>/dev/null || true; done' EXIT
cat > "$tmp/sway.conf" <<'SWAY'
xwayland disable
default_border none
default_floating_border none
focus_follows_mouse no
seat seat0 fallback true
input * xkb_layout us
output * scale 1
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
for i in {1..120}; do
    ipc=$(find "$XDG_RUNTIME_DIR" -maxdepth 1 -name 'sway-ipc*.sock' -print -quit)
    socket=$(find "$XDG_RUNTIME_DIR" -maxdepth 1 -type s -name 'wayland-*' -print -quit)
    [[ -n "$ipc" && -n "$socket" ]] && break
    sleep .1
done
if [[ -z "$ipc" || -z "$socket" ]]; then
    cat "$out/sway.log" >&2
    exit 77
fi
export SWAYSOCK="$ipc" WAYLAND_DISPLAY="${socket##*/}"
env -u DISPLAY wayland-info > "$out/globals.txt"
for interface in zwp_input_method_manager_v2 zwp_text_input_manager_v3; do
    grep -q "$interface" "$out/globals.txt" || { echo "Seat lacks $interface"; exit 77; }
done
env -u DISPLAY WAYLAND_DEBUG=client fcitx5 > "$out/fcitx.log" 2>&1 &
ime_pid=$!
env -u DISPLAY FLTK_BACKEND=wayland GDK_BACKEND=wayland WAYLAND_DEBUG=client "$exe" > "$out/editor.log" 2> "$out/editor-wire.log" &
editor_pid=$!
python3 "$here/drive.py" "$exe" "$editor_pid" "$out" | tee "$out/status.txt"
