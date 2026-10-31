#pragma once
#include <windows.h>
#include "Scintilla.h"
#include "SciLexer.h"
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <string>

// The hosts pass their actual fork HWND. No operating-system file I/O, paint or
// toolkit event loop is timed. Each trial creates fresh text and styling state.
inline int np4_native_bench(HWND editor) {
    const std::string row = "{\"key\":\"中文 café 🙂\",\"number\":42,\"active\":true}\n";
    std::string payload;
    payload.reserve(4 * 1024 * 1024);
    while (payload.size() + row.size() <= 4 * 1024 * 1024) payload += row;
    const auto msg = [&](UINT id, WPARAM w = 0, LPARAM l = 0) {
        return ::SendMessageW(editor, id, w, l);
    };
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    const auto tick = [] { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; };
    const auto ms = [&](LONGLONG a, LONGLONG b) { return 1000.0 * (b-a) / freq.QuadPart; };
    // Match the actual Notepad4 host unless replaying the all-events control.
    const char *all = std::getenv("NP4_BENCH_ALL_EVENTS");
    const auto mask = all && std::string_view(all) == "1" ? SC_MODEVENTMASKALL : (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT);
    msg(SCI_SETMODEVENTMASK, mask);
    if (msg(SCI_GETMODEVENTMASK) != mask) return 4;
    msg(SCI_SETCODEPAGE, SC_CP_UTF8);
    msg(SCI_SETLEXER, SCLEX_JSON);
    msg(SCI_SETKEYWORDS, 0, reinterpret_cast<LPARAM>("Infinity NaN false null true"));
    msg(WM_SETREDRAW, FALSE);
    for (int trial = 0; trial < 6; ++trial) {
        msg(SCI_SETUNDOCOLLECTION, 0);
        msg(SCI_CLEARALL);
        auto t0 = tick();
        msg(SCI_ADDTEXT, payload.size(), reinterpret_cast<LPARAM>(payload.data()));
        auto t1 = tick();
        msg(SCI_COLOURISE, 0, -1);
        auto t2 = tick();
        if (msg(SCI_GETLENGTH) != static_cast<LRESULT>(payload.size()) ||
            msg(SCI_GETSTYLEINDEXAT, payload.size() - row.size() + 2) != SCE_JSON_PROPERTYNAME) return 2;
        msg(SCI_SETUNDOCOLLECTION, 1);
        msg(SCI_EMPTYUNDOBUFFER);
        auto t3 = tick();
        for (int i = 0; i < 64; ++i) {
            msg(SCI_SETSEL, 2, 5);
            msg(SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>("same"));
            msg(SCI_UNDO);
        }
        auto t4 = tick();
        if (msg(SCI_GETLENGTH) != static_cast<LRESULT>(payload.size()) || msg(SCI_GETCHARAT, 2) != 'k') return 3;
        if (trial) std::printf("NP4_CORE,%d,%llu,%.4f,%.4f,%.4f\n", trial,
            static_cast<unsigned long long>(payload.size()), ms(t0,t1), ms(t1,t2), ms(t3,t4));
    }
    msg(WM_SETREDRAW, TRUE);
    return 0;
}
