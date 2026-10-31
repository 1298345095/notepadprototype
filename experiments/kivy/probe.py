"""Windows Kivy/SDL2 shell that embeds the actual Notepad4 Win32 control."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import sys
import time

# Keep Kivy's argument parser out of the application's arguments.
os.environ["KIVY_NO_ARGS"] = "1"
os.environ.setdefault("KIVY_WINDOW", "sdl2")
os.environ.setdefault("KIVY_GL_BACKEND", "angle_sdl2")

# The windowed bootloader may have no console streams. Preserve logs for CI.
if "--self-test" in sys.argv and sys.stdout is None:
    report_arg = sys.argv.index("--self-test") + 1
    if report_arg < len(sys.argv):
        sys.stdout = sys.stderr = open(sys.argv[report_arg] + ".log", "w", encoding="utf-8", buffering=1)

from kivy.app import App
from kivy.clock import Clock
from kivy.core.window import Window
from kivy.uix.widget import Widget


def bind(dll, name, result, *args):
    function = getattr(dll, name)
    function.restype = result
    function.argtypes = args
    return function


user = C.WinDLL("user32", use_last_error=True)
kernel = C.WinDLL("kernel32", use_last_error=True)
instance = bind(kernel, "GetModuleHandleW", W.HMODULE, W.LPCWSTR)(None)
create = bind(user, "CreateWindowExW", W.HWND, W.DWORD, W.LPCWSTR, W.LPCWSTR,
              W.DWORD, C.c_int, C.c_int, C.c_int, C.c_int, W.HWND, W.HMENU,
              W.HINSTANCE, W.LPVOID)
send = bind(user, "SendMessageW", C.c_ssize_t, W.HWND, W.UINT, C.c_size_t, C.c_ssize_t)
get_parent = bind(user, "GetParent", W.HWND, W.HWND)
get_rect = bind(user, "GetClientRect", W.BOOL, W.HWND, C.POINTER(W.RECT))
get_window_rect = bind(user, "GetWindowRect", W.BOOL, W.HWND, C.POINTER(W.RECT))
get_style = bind(user, "GetWindowLongPtrW", C.c_ssize_t, W.HWND, C.c_int)
set_style = bind(user, "SetWindowLongPtrW", C.c_ssize_t, W.HWND, C.c_int, C.c_ssize_t)
move_window = bind(user, "MoveWindow", W.BOOL, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.BOOL)
set_pos = bind(user, "SetWindowPos", W.BOOL, W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.UINT)
set_focus = bind(user, "SetFocus", W.HWND, W.HWND)
destroy = bind(user, "DestroyWindow", W.BOOL, W.HWND)
is_window = bind(user, "IsWindow", W.BOOL, W.HWND)


def size_of(hwnd, rect_function):
    rect = W.RECT()
    if not rect_function(hwnd, C.byref(rect)):
        raise C.WinError(C.get_last_error())
    return rect.right - rect.left, rect.bottom - rect.top


class ForkEditor(App):
    title = "Notepad4 native editor / Kivy SDL2"
    sample = '{"name":"中文😀","ready":true}\n'.encode()

    def __init__(self, test_path):
        super().__init__()
        self.test_path = test_path
        self.child = None
        self.dll = None
        self.registered = False
        self.report = None

    def build(self):
        return Widget()

    def on_start(self):
        self.parent = Window.get_window_info().window
        base = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent))
        self.dll = C.WinDLL(str(base / "NP4Probe.dll"))
        if not bind(self.dll, "NP4_Register", C.c_int)():
            raise RuntimeError("Could not register the Notepad4 Win32 class")
        self.registered = True
        style = get_style(self.parent, -16)
        set_style(self.parent, -16, style | 0x02000000)  # WS_CLIPCHILDREN
        self.child = create(0, "Scintilla", "", 0x50010000, 0, 0, 1, 1,
                            self.parent, None, instance, None)
        if not self.child:
            raise C.WinError(C.get_last_error())
        if get_parent(self.child) != self.parent:
            raise RuntimeError("Editor does not belong to the SDL2 HWND")
        self.fit()
        Window.bind(on_resize=self.fit)
        self.sci(2037, 65001)  # SCI_SETCODEPAGE UTF-8
        self.sci(4001, 120)    # SCI_SETLEXER SCLEX_JSON
        self.sci(2051, 9, 0x8d3e20)  # SCI_STYLESETFORE JSON property
        self.sci(2051, 6, 0x57710d)  # JSON double-quoted string
        self.set_bytes(2181, self.sample)
        self.sci(2175)         # SCI_EMPTYUNDOBUFFER
        set_focus(self.child)
        if self.test_path:
            Clock.schedule_once(self.run_checks, .15)

    def fit(self, *unused):
        width, height = size_of(self.parent, get_rect)
        if not move_window(self.child, 0, 0, width, height, True):
            raise C.WinError(C.get_last_error())

    def sci(self, message, wparam=0, lparam=0):
        return send(self.child, message, wparam, lparam)

    def set_bytes(self, message, data):
        buf = C.create_string_buffer(data)
        return self.sci(message, 0, C.addressof(buf))

    def read_bytes(self):
        length = self.sci(2006)
        buf = C.create_string_buffer(length + 1)
        self.sci(2182, len(buf), C.addressof(buf))
        return buf.raw[:length]

    def run_checks(self, dt):
        offset = self.sample.index("😀".encode())
        width = C.c_ssize_t()
        codepoint = self.sci(2067, offset, C.addressof(width))
        self.sci(4003, 0, -1)
        style = self.sci(2038, 2)
        if (codepoint, width.value, self.sci(4002), style) != (0x1f600, 4, 120, 9):
            raise RuntimeError("Fork-specific codepoint, byte width or JSON lexer failed")
        self.sci(2160, offset, offset + 4)
        self.set_bytes(2170, b"x")
        if self.read_bytes() != self.sample.replace("😀".encode(), b"x"):
            raise RuntimeError("Native selection replacement failed")
        self.sci(2176)
        if self.read_bytes() != self.sample:
            raise RuntimeError("Native undo failed")
        self.initial_size = size_of(self.parent, get_rect)
        # Drive a real top-level resize from outside the Kivy size property.
        # The child may catch up only through the normal SDL2 -> Kivy callback.
        w, h = size_of(self.parent, get_window_rect)
        if not set_pos(self.parent, None, 0, 0, max(300, w - 104), max(200, h - 72), 0x16):
            raise C.WinError(C.get_last_error())
        self.deadline = time.monotonic() + 8
        Clock.schedule_interval(self.check_resize, .1)

    def check_resize(self, dt):
        parent_size = size_of(self.parent, get_rect)
        child_size = size_of(self.child, get_window_rect)
        if parent_size != self.initial_size and parent_size == child_size:
            self.report = {"fork_message": 2067, "unicode": "U+1F600",
                           "utf8_bytes": 4, "json_property_style": 9, "undo": True,
                           "parent_verified": True, "before": self.initial_size,
                           "resized_parent": parent_size, "resized_child": child_size,
                           "onefile_extraction": hasattr(sys, "_MEIPASS")}
            self.stop()
            return False
        if time.monotonic() > self.deadline:
            raise RuntimeError(f"Resize failed: initial={self.initial_size}, parent={parent_size}, child={child_size}")
        return True

    def on_stop(self):
        self.cleanup()

    def cleanup(self):
        if self.child:
            Window.unbind(on_resize=self.fit)
            if is_window(self.child):
                destroy(self.child)
            self.child = None
        if self.registered:
            bind(self.dll, "NP4_Release", None)()
            self.registered = False


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", type=Path, metavar="OUTPUT_JSON")
    args = parser.parse_args()
    app = ForkEditor(args.self_test)
    try:
        app.run()
    finally:
        app.cleanup()
    if args.self_test:
        if app.report is None:
            raise RuntimeError("No verified Kivy GUI result")
        if hasattr(sys, "_MEIPASS"):
            extracted = [p for p in Path(sys._MEIPASS).rglob("*") if p.is_file()]
            app.report["expanded_file_count"] = len(extracted)
            app.report["expanded_file_bytes"] = sum(p.stat().st_size for p in extracted)
        args.self_test.write_text(json.dumps(app.report) + "\n", encoding="utf-8")
