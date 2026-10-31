#include <windows.h>
#include <string_view>
#include "Scintilla.h"
#include "SciLexer.h"

extern "C" __declspec(dllexport) int NP4_Register() {
    Scintilla_LoadDpiForWindow();
    return Scintilla_RegisterClasses(GetModuleHandleW(nullptr));
}
extern "C" __declspec(dllexport) void NP4_Release() { Scintilla_ReleaseResources(); }

// Exercise an actual Win32 child of the foreign toolkit's native window.
extern "C" __declspec(dllexport) int NP4_AttachAndVerify(HWND parent) {
    RECT area{};
    if (!IsWindowVisible(parent) || !GetClientRect(parent, &area) || area.right < 80 || area.bottom < 40) return 1;
    HWND child = CreateWindowExW(0, L"Scintilla", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, area.right, area.bottom, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!child) return 2;
    const auto msg = [child](UINT id, WPARAM w=0, LPARAM l=0) { return SendMessageW(child,id,w,l); };
    constexpr std::string_view json = "{\"title\":\"中\",\"ok\":true}\n";
    msg(SCI_SETCODEPAGE, SC_CP_UTF8);
    msg(SCI_SETLEXER, SCLEX_JSON);
    msg(SCI_SETKEYWORDS, 0, reinterpret_cast<LPARAM>("false null true"));
    msg(SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(json.data()));
    msg(SCI_COLOURISE, 0, -1);
    const auto pos = json.find("中");
    Sci_Position width = 0;
    const bool sourced = GetParent(child) == parent && msg(SCI_GETCHARACTERANDWIDTH, pos, reinterpret_cast<LPARAM>(&width)) == 0x4e2d && width == 3 && msg(SCI_GETSTYLEINDEXAT, 2) == SCE_JSON_PROPERTYNAME;
    msg(SCI_SETSEL,pos,pos+3);
    msg(SCI_REPLACESEL,0,reinterpret_cast<LPARAM>("X"));
    const bool edited = msg(SCI_GETCHARAT,pos) == 'X';
    msg(SCI_UNDO);
    const bool restored = msg(SCI_GETCHARACTERANDWIDTH,pos,reinterpret_cast<LPARAM>(&width)) == 0x4e2d && width == 3 && msg(SCI_GETLENGTH) == static_cast<LRESULT>(json.size());
    UpdateWindow(child);
    DestroyWindow(child);
    return sourced && edited && restored ? 0 : 3;
}
