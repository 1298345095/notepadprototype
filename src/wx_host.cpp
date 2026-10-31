// wxMSW shell hosting the statically linked Notepad4 Scintilla Win32 control.
#include <wx/wx.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/nativewin.h>
#include <windows.h>
#include <commctrl.h>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include "Scintilla.h"
#include "SciLexer.h"
#include "native_workload.hpp"

static std::string ReadBytes(const wxString& path) {
    wxFile file(path);
    const auto size = file.IsOpened() ? file.Length() : wxInvalidOffset;
    if (size == wxInvalidOffset) throw std::runtime_error("Could not open input file");
    std::string bytes(static_cast<size_t>(size), '\0');
    if (!bytes.empty() && file.Read(bytes.data(), bytes.size()) != size)
        throw std::runtime_error("Could not read input file");
    return bytes;
}

class NativeFrame final : public wxFrame {
    HWND sci = nullptr;
    wxString path;

    LRESULT Sci(UINT msg, WPARAM wp = 0, LPARAM lp = 0) const {
        return ::SendMessageW(sci, msg, wp, lp);
    }
    void SetDocument(std::string_view bytes, bool json) {
        Sci(SCI_SETCODEPAGE, SC_CP_UTF8);
        Sci(SCI_SETUNDOCOLLECTION, 0);
        Sci(SCI_CLEARALL);
        Sci(SCI_SETLEXER, json ? SCLEX_JSON : SCLEX_NULL);
        Sci(SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<LPARAM>("Consolas"));
        Sci(SCI_STYLESETSIZE, STYLE_DEFAULT, 11);
        Sci(SCI_STYLECLEARALL);
        if (json) {
            Sci(SCI_SETKEYWORDS, 0, reinterpret_cast<LPARAM>("Infinity NaN false null true"));
            Sci(SCI_STYLESETFORE, SCE_JSON_PROPERTYNAME, RGB(102, 48, 147));
            Sci(SCI_STYLESETFORE, SCE_JSON_STRING_DQ, RGB(27, 109, 65));
            Sci(SCI_STYLESETFORE, SCE_JSON_NUMBER, RGB(162, 63, 27));
        }
        if (!bytes.empty())
            Sci(SCI_ADDTEXT, bytes.size(), reinterpret_cast<LPARAM>(bytes.data()));
        Sci(SCI_GOTOPOS, 0);
        Sci(SCI_SETUNDOCOLLECTION, 1);
        Sci(SCI_EMPTYUNDOBUFFER);
        Sci(SCI_SETSAVEPOINT);
    }
    std::string Bytes() const {
        const size_t size = static_cast<size_t>(Sci(SCI_GETLENGTH));
        std::string out(size + 1, '\0');
        Sci(SCI_GETTEXT, size, reinterpret_cast<LPARAM>(out.data()));
        out.resize(size);
        return out;
    }
    bool WriteDocument() {
        wxTempFile file(path);
        const auto bytes = Bytes();
        if (!file.IsOpened() || !file.Write(bytes.data(), bytes.size()) || !file.Commit())
            return false;
        Sci(SCI_SETSAVEPOINT);
        return true;
    }
    void Save() {
        if (path.empty()) {
            wxFileDialog dlg(this, "Save UTF-8 file", "", "", "*.*", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
            if (dlg.ShowModal() != wxID_OK) return;
            path = dlg.GetPath();
        }
        if (!WriteDocument())
            wxMessageBox("Could not save file", "NP4-wx", wxICON_ERROR, this);
    }
    static LRESULT CALLBACK KeyProc(HWND win, UINT msg, WPARAM wp, LPARAM lp,
                                     UINT_PTR id, DWORD_PTR context) {
        if (msg == WM_KEYDOWN && wp == 'S' && (::GetKeyState(VK_CONTROL) & 0x8000)) {
            reinterpret_cast<NativeFrame*>(context)->Save();
            return 0;
        }
        if (msg == WM_CHAR && wp == 19) return 0;
        if (msg == WM_NCDESTROY) ::RemoveWindowSubclass(win, KeyProc, id);
        return ::DefSubclassProc(win, msg, wp, lp);
    }
public:
    explicit NativeFrame(const wxString& file)
        : wxFrame(nullptr, wxID_ANY, "NP4-wx native  " + wxFileName(file).GetFullName(),
                  wxDefaultPosition, wxSize(900, 600)), path(file) {
        constexpr int editorId = wxID_HIGHEST + 101;
        sci = ::CreateWindowExW(0, L"Scintilla", nullptr,
              WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS,
              0, 0, 1, 1, static_cast<HWND>(GetHandle()),
              reinterpret_cast<HMENU>(static_cast<INT_PTR>(editorId)),
              ::GetModuleHandleW(nullptr), nullptr);
        if (!sci) throw std::runtime_error("Could not create the fork child HWND");
        auto child = std::make_unique<wxNativeWindow>();
        if (!child->Create(this, editorId, sci)) {
            ::DestroyWindow(sci);
            throw std::runtime_error("wxNativeWindow could not attach to the fork");
        }
        child->Disown(); // Transfer lifetime of the native child to wx.
        auto *layout = new wxBoxSizer(wxVERTICAL);
        layout->Add(child.release(), 1, wxEXPAND);
        SetSizer(layout);
        Layout();
        if (!::SetWindowSubclass(sci, KeyProc, 1, reinterpret_cast<DWORD_PTR>(this)))
            throw std::runtime_error("Could not install the Save shortcut");
        const std::string bytes = path.empty() ? std::string{} : ReadBytes(path);
        Sci(SCI_SETMODEVENTMASK, SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT);
        SetDocument(bytes, wxFileName(path).GetExt().Lower() == "json");
    }
    int Benchmark() { return np4_native_bench(sci); }
    void FocusEditor() { ::SetFocus(sci); }
    bool FileSelfTest() {
        const std::string original = ReadBytes(path);
        if (Bytes() != original) return false;
        Sci(SCI_SELECTALL);
        constexpr char replacement[] = "Native save 中文\n";
        Sci(SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(replacement));
        return WriteDocument() && ReadBytes(path) == replacement;
    }
    bool SelfTest() {
        static_assert(SCI_GETCHARACTERANDWIDTH == 2067);
        const std::string sample = "{\"name\":\"中文😀\"}\n";
        SetDocument(sample, true);
        Sci(SCI_COLOURISE, 0, -1);
        const auto pos = sample.find("😀");
        Sci_Position width = 0;
        const auto character = Sci(SCI_GETCHARACTERANDWIDTH, pos, reinterpret_cast<LPARAM>(&width));
        if (character != 0x1f600 || width != 4 || Sci(SCI_GETLEXER) != SCLEX_JSON ||
            Sci(SCI_GETSTYLEINDEXAT, 2) != SCE_JSON_PROPERTYNAME)
            return false;
        Sci(SCI_SETSEL, pos, pos + 4);
        Sci(SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>("x"));
        auto expected = sample;
        expected.replace(pos, 4, "x");
        if (Bytes() != expected) return false;
        Sci(SCI_UNDO);
        return Bytes() == sample;
    }
};

class NativeApp final : public wxApp {
    bool registered = false;
    NativeFrame* frame = nullptr;
    bool test = false;
    bool bench = false;
    bool fileTest = false;
public:
    ~NativeApp() override {
        if (registered) Scintilla_ReleaseResources();
    }
    bool OnInit() override {
        const wxString arg = argc > 1 ? wxString(argv[1]) : wxString();
        test = arg == "--self-test";
        bench = arg == "--bench";
        fileTest = arg == "--file-self-test";
        if (fileTest && argc != 3) {
            std::fputs("Usage: np4-wx-native --file-self-test <existing UTF-8 file>\n", stderr);
            return false;
        }
        try {
            Scintilla_LoadDpiForWindow();
            registered = Scintilla_RegisterClasses(::GetModuleHandleW(nullptr)) != 0;
            if (!registered) throw std::runtime_error("Could not register the Scintilla window class");
            const wxString file = fileTest ? wxString(argv[2]) : (test || bench ? wxString{} : arg);
            frame = new NativeFrame(file);
            frame->Show();
            frame->FocusEditor();
            return true;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "NP4-wx native: %s\n", error.what());
            return false;
        }
    }
    int OnRun() override {
        if (!test && !fileTest && !bench) return wxApp::OnRun();
        frame->Update();
        int status = 0;
        try {
            if (bench) {
                status = frame->Benchmark();
            } else if (fileTest) {
                status = frame->FileSelfTest() ? 0 : 1;
                std::puts(status == 0 ? "PASS file open, edit, save and byte comparison" : "FAIL file self-test");
            } else {
                status = frame->SelfTest() ? 0 : 1;
                std::puts(status == 0 ? "PASS fork 2067, JSON lexer, selection replace and undo" : "FAIL fork self-test");
            }
        } catch (const std::exception& error) {
            std::fprintf(stderr, "NP4-wx native: %s\n", error.what());
            status = 1;
        }
        delete frame;
        frame = nullptr;
        return status;
    }
};
wxIMPLEMENT_APP(NativeApp);
