// Windows route: the entire editing control belongs to the pinned Notepad4 fork.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/platform.H>
#include "Scintilla.h"
#include "SciLexer.h"
#include "native_workload.hpp"
#include <filesystem>
#include <climits>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

class ForkWindowClass final {
public:
    explicit ForkWindowClass(HINSTANCE instance) {
        Scintilla_LoadDpiForWindow();
        if (!Scintilla_RegisterClasses(instance))
            throw std::runtime_error("Cannot register the fork's Scintilla window class");
    }
    ~ForkWindowClass() { Scintilla_ReleaseResources(); }
    ForkWindowClass(const ForkWindowClass &) = delete;
    ForkWindowClass &operator=(const ForkWindowClass &) = delete;
};

std::string readUtf8(const fs::path &path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open input");
    LARGE_INTEGER size {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > INT_MAX - 1) {
        CloseHandle(file);
        throw std::runtime_error("Input exceeds prototype size limit");
    }
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const bool ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok || read != bytes.size()) throw std::runtime_error("Cannot read input");
    if (!bytes.empty() && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), int(bytes.size()), nullptr, 0))
        throw std::runtime_error("Input must be UTF-8");
    return bytes;
}

class Host final : public Fl_Window {
    HWND child = nullptr;
    fs::path path;

    LRESULT sci(UINT id, WPARAM w = 0, LPARAM l = 0) const {
        return SendMessageW(child, id, w, l);
    }
    static LPARAM pointer(const void *p) { return reinterpret_cast<LPARAM>(p); }

    void save() {
        if (path.empty()) throw std::runtime_error("Open an existing file on the command line to use Save");
        size_t n = size_t(sci(SCI_GETLENGTH));
        std::string text(n+1, '\0');
        sci(SCI_GETTEXT, n+1, pointer(text.data()));
        wchar_t tmp[MAX_PATH] {};
        if (!GetTempFileNameW(path.parent_path().c_str(), L"np4", 0, tmp))
            throw std::runtime_error("Cannot create temporary file beside original");
        HANDLE file = CreateFileW(tmp, GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD written = 0;
        const bool ok = file != INVALID_HANDLE_VALUE && WriteFile(file, text.data(), DWORD(n), &written, nullptr);
        const bool closed = file != INVALID_HANDLE_VALUE && CloseHandle(file);
        if (!ok || !closed || written != n || !ReplaceFileW(path.c_str(), tmp, nullptr, 0, nullptr, nullptr)) {
            DeleteFileW(tmp);
            throw std::runtime_error("Cannot replace original file");
        }
        sci(SCI_SETSAVEPOINT);
    }

    static LRESULT CALLBACK editProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data) {
        auto host = reinterpret_cast<Host *>(data);
        if (msg == WM_KEYDOWN && w == 'S' && GetKeyState(VK_CONTROL) < 0) {
            try { host->save(); }
            catch (const std::exception &e) { MessageBoxA(hwnd, e.what(), "NP4-FLTK", MB_ICONERROR); }
            return 0;
        }
        if (msg == WM_CHAR && w == 19) return 0;
        if (msg == WM_NCDESTROY) {
            RemoveWindowSubclass(hwnd, editProc, id);
            host->child = nullptr;
        }
        return DefSubclassProc(hwnd, msg, w, l);
    }

    static LRESULT CALLBACK parentProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data) {
        auto editor = reinterpret_cast<Host *>(data)->child;
        if (editor && msg == WM_SIZE) {
            RECT rect {};
            GetClientRect(hwnd, &rect);
            MoveWindow(editor, 0, 0, rect.right, rect.bottom, TRUE);
        }
        if (editor && msg == WM_SETFOCUS) {
            SetFocus(editor);
            return 0;
        }
        if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, parentProc, id);
        return DefSubclassProc(hwnd, msg, w, l);
    }

public:
    Host() : Fl_Window(900, 600, "NP4-FLTK native") { end(); resizable(this); }
    ~Host() override {
        if (child) DestroyWindow(child);
        if (shown()) RemoveWindowSubclass(fltkHwnd(), parentProc, 1);
    }
    HWND fltkHwnd() const { return fl_xid(this); }
    void open(const fs::path &input, bool test) {
        if (!input.empty()) path = fs::absolute(input);
        const std::string bytes = path.empty() ? "{key:'中', yes:true}\n" : readUtf8(path);
        show();
        auto parent = fltkHwnd();
        SetWindowLongPtrW(parent, GWL_STYLE, GetWindowLongPtrW(parent, GWL_STYLE) | WS_CLIPCHILDREN);
        RECT rect {};
        GetClientRect(parent, &rect);
        child = CreateWindowExW(0, L"Scintilla", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                WS_VSCROLL | WS_HSCROLL | WS_CLIPSIBLINGS, 0, 0, rect.right, rect.bottom,
                                parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!child) throw std::runtime_error("CreateWindowExW(Scintilla) failed");
        if (!SetWindowSubclass(child, editProc, 1, reinterpret_cast<DWORD_PTR>(this)) ||
                !SetWindowSubclass(parent, parentProc, 1, reinterpret_cast<DWORD_PTR>(this)))
            throw std::runtime_error("Cannot install native event handlers");
        sci(SCI_SETMODEVENTMASK, SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT);
        sci(SCI_SETCODEPAGE, SC_CP_UTF8);
        sci(SCI_STYLESETFONT, STYLE_DEFAULT, pointer("Consolas"));
        sci(SCI_STYLESETSIZE, STYLE_DEFAULT, 11);
        sci(SCI_STYLECLEARALL);
        auto ext = path.extension().wstring();
        const bool json = test || path.empty() || ext == L".json" || ext == L".json5";
        sci(SCI_SETLEXER, json ? SCLEX_JSON : SCLEX_NULL);
        if (json) {
            sci(SCI_SETKEYWORDS, 0, pointer("Infinity NaN false null true"));
            sci(SCI_STYLESETFORE, SCE_JSON_PROPERTYNAME, RGB(32,62,141));
            sci(SCI_STYLESETFORE, SCE_JSON_STRING_DQ, RGB(13,113,87));
            sci(SCI_STYLESETFORE, SCE_JSON_STRING_SQ, RGB(13,113,87));
            sci(SCI_STYLESETFORE, SCE_JSON_KEYWORD, RGB(125,47,130));
        }
        const auto eol = bytes.find_first_of("\r\n");
        int mode = SC_EOL_LF;
        if (eol != bytes.npos && bytes[eol] == '\r')
            mode = (eol+1 < bytes.size() && bytes[eol+1] == '\n') ? SC_EOL_CRLF : SC_EOL_CR;
        sci(SCI_SETEOLMODE, mode);
        sci(SCI_ADDTEXT, bytes.size(), pointer(bytes.data()));
        sci(SCI_EMPTYUNDOBUFFER);
        sci(SCI_SETSAVEPOINT);
        sci(SCI_GOTOPOS, 0);
        if (json) sci(SCI_COLOURISE, 0, -1);
        SetFocus(child);
    }
    int bench() const { return np4_native_bench(child); }
    void verify() const {
        static_assert(SCI_GETCHARACTERANDWIDTH == 2067);
        Sci_Position width = 0;
        const auto cp = sci(SCI_GETCHARACTERANDWIDTH, 6, pointer(&width));
        if (cp != 0x4e2d || width != 3 || sci(SCI_GETLEXER) != SCLEX_JSON ||
            sci(SCI_GETSTYLEINDEXAT, 2) != SCE_JSON_PROPERTYNAME || sci(SCI_GETSTYLEINDEXAT, 6) != SCE_JSON_STRING_SQ ||
            sci(SCI_GETSTYLEINDEXAT, 16) != SCE_JSON_KEYWORD)
            throw std::runtime_error("Fork message 2067 / JSON5 lexer styles mismatch");
        sci(SCI_SETSEL, 6, 9);
        sci(SCI_REPLACESEL, 0, pointer("x"));
        if (sci(SCI_GETCHARAT, 6) != 'x') throw std::runtime_error("Selection replacement failed");
        sci(SCI_UNDO);
        width = 0;
        if (sci(SCI_GETCHARACTERANDWIDTH, 6, pointer(&width)) != 0x4e2d || width != 3)
            throw std::runtime_error("Native undo failed");
        std::puts("native window, fork 2067, JSON5 styles, replace, undo: PASS");
    }
    void verifyFileSave() {
        const std::string input = readUtf8(path);
        std::string loaded(input.size() + 1, '\0');
        sci(SCI_GETTEXT, loaded.size(), pointer(loaded.data()));
        loaded.resize(input.size());
        if (sci(SCI_GETLENGTH) != static_cast<LRESULT>(input.size()) || loaded != input)
            throw std::runtime_error("File did not load into the native editor");
        constexpr char replacement[] = "Native save 中文\n";
        sci(SCI_SELECTALL);
        sci(SCI_REPLACESEL, 0, pointer(replacement));
        save();
        if (readUtf8(path) != replacement)
            throw std::runtime_error("Saved bytes did not match the edited document");
        std::puts("native UTF-8 file load, edit and save: PASS");
    }
};

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    try {
        int argc = 0;
        LPWSTR *args = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!args) throw std::runtime_error("Cannot decode command line");
        const std::wstring arg = argc > 1 ? args[1] : L"";
        const fs::path fileArgument = argc > 2 ? args[2] : L"";
        LocalFree(args);
        const bool test = arg == L"--self-test";
        const bool bench = arg == L"--bench";
        const bool saveTest = arg == L"--file-self-test";
        if (saveTest && fileArgument.empty()) throw std::runtime_error("Provide an existing UTF-8 fixture");
        ForkWindowClass forkClass(instance);
        Host window;
        window.open(saveTest ? fileArgument : (test || bench ? fs::path{} : fs::path(arg)), test || bench);
        if (bench) return window.bench();
        if (test) { window.verify(); return 0; }
        if (saveTest) { window.verifyFileSave(); return 0; }
        return Fl::run();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
