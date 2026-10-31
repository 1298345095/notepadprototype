#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <tcl.h>
#include <tk.h>
#include <tkPlatDecls.h>
#include "Scintilla.h"
#include "SciLexer.h"
#include "native_workload.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
static constexpr char demo[] = "{key:'中', yes:true}\n";

static std::string readFile(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open input");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

class Editor {
    Tk_Window frame;
    HWND child{};
    fs::path file;
    LRESULT msg(UINT id, WPARAM w=0, LPARAM l=0) const { return SendMessageW(child,id,w,l); }
    static LPARAM ptr(const void *p) { return reinterpret_cast<LPARAM>(p); }
    std::string bytes() const {
        auto n=static_cast<size_t>(msg(SCI_GETLENGTH)); std::string s(n+1,'\0');
        msg(SCI_GETTEXT,s.size(),ptr(s.data())); s.resize(n); return s;
    }
    void save() {
        if (file.empty()) throw std::runtime_error("Open an existing file to save with Ctrl+S");
        auto data=bytes(); wchar_t tmp[MAX_PATH]{};
        if(!GetTempFileNameW(file.parent_path().c_str(),L"np4",0,tmp)) throw std::runtime_error("Cannot create sibling temporary file");
        std::ofstream out(fs::path(tmp),std::ios::binary|std::ios::trunc);
        out.write(data.data(),static_cast<std::streamsize>(data.size())); out.close();
        if(!out || !ReplaceFileW(file.c_str(),tmp,nullptr,0,nullptr,nullptr)) {
            DeleteFileW(tmp); throw std::runtime_error("Cannot replace original file");
        }
        msg(SCI_SETSAVEPOINT);
    }
    void resize() const {
        if(!child)return; RECT rc{}; GetClientRect(GetParent(child),&rc);
        MoveWindow(child,0,0,rc.right,rc.bottom,TRUE);
    }
    static void structure(void *context,XEvent *event) {
        auto *editor=static_cast<Editor*>(context);
        if(event->type==ConfigureNotify) editor->resize();
        if(event->type==DestroyNotify) editor->frame=nullptr;
    }
    static LRESULT CALLBACK keyboard(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
        auto *editor=reinterpret_cast<Editor*>(data);
        if(m==WM_KEYDOWN && w=='S' && GetKeyState(VK_CONTROL)<0) {
            try { editor->save(); } catch(const std::exception &err) { MessageBoxA(h,err.what(),"NP4-Tk",MB_ICONERROR); }
            return 0;
        }
        if(m==WM_CHAR && w==19) return 0;
        if(m==WM_NCDESTROY) {editor->child=nullptr;RemoveWindowSubclass(h,keyboard,id);}
        return DefSubclassProc(h,m,w,l);
    }
public:
    Editor(Tk_Window f,HINSTANCE app,const fs::path &path) : frame(f), file(path.empty()?path:fs::absolute(path)) {
        const std::string text=file.empty()?demo:readFile(file);
        Tk_MakeWindowExist(f);
        HWND parent=Tk_GetHWND(Tk_WindowId(f));
        child=CreateWindowExW(0,L"Scintilla",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|WS_HSCROLL,
                                0,0,1,1,parent,nullptr,app,nullptr);
        if(!child) throw std::runtime_error("Cannot create fork HWND");
        if(!SetWindowSubclass(child,keyboard,1,reinterpret_cast<DWORD_PTR>(this))) {
            DestroyWindow(child); throw std::runtime_error("Cannot attach shortcut");
        }
        Tk_CreateEventHandler(f,StructureNotifyMask,structure,this);resize();
        msg(SCI_SETMODEVENTMASK,SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT);
        msg(SCI_SETCODEPAGE,SC_CP_UTF8); msg(SCI_STYLESETFONT,STYLE_DEFAULT,ptr("Consolas"));
        msg(SCI_STYLESETSIZE,STYLE_DEFAULT,11); msg(SCI_STYLECLEARALL);
        auto ext=file.extension().wstring();bool json=file.empty()||!_wcsicmp(ext.c_str(),L".json")||!_wcsicmp(ext.c_str(),L".json5");
        msg(SCI_SETLEXER,json?SCLEX_JSON:SCLEX_NULL);
        if(json) {
            msg(SCI_SETKEYWORDS,0,ptr("Infinity NaN false null true"));
            msg(SCI_STYLESETFORE,SCE_JSON_PROPERTYNAME,RGB(32,62,141));
            msg(SCI_STYLESETFORE,SCE_JSON_STRING_DQ,RGB(13,113,87));
            msg(SCI_STYLESETFORE,SCE_JSON_STRING_SQ,RGB(13,113,87));
            msg(SCI_STYLESETFORE,SCE_JSON_KEYWORD,RGB(125,47,130));
        }
        msg(SCI_ADDTEXT,text.size(),ptr(text.data())); msg(SCI_EMPTYUNDOBUFFER);msg(SCI_SETSAVEPOINT);msg(SCI_GOTOPOS,0);
        if(json) msg(SCI_COLOURISE,0,-1);
        SetFocus(child);
    }
    ~Editor() { if(frame) Tk_DeleteEventHandler(frame,StructureNotifyMask,structure,this); if(child) DestroyWindow(child); }
    int bench() const {return np4_native_bench(child);}
    void selfTest() {
        static_assert(SCI_GETCHARACTERANDWIDTH==2067); Sci_Position width{};
        if(bytes()!=demo || msg(SCI_GETCHARACTERANDWIDTH,6,ptr(&width))!=0x4e2d || width!=3 ||
           msg(SCI_GETLEXER)!=SCLEX_JSON || msg(SCI_GETSTYLEINDEXAT,2)!=SCE_JSON_PROPERTYNAME) throw std::runtime_error("Fork/JSON self-test failed");
        msg(SCI_SETSEL,6,9);msg(SCI_REPLACESEL,0,ptr("x"));if(msg(SCI_GETCHARAT,6)!='x') throw std::runtime_error("Replace failed");
        msg(SCI_UNDO);if(bytes()!=demo) throw std::runtime_error("Undo failed");
        std::puts("PASS real Tk-hosted fork HWND: 2067, JSON5, replace, undo");
    }
    void fileTest() {
        if(bytes()!=readFile(file)) throw std::runtime_error("File load failed");
        msg(SCI_SELECTALL);msg(SCI_REPLACESEL,0,ptr("Native save 中文\n"));save();
        if(readFile(file)!="Native save 中文\n") throw std::runtime_error("File save failed");
        std::puts("PASS native Tk UTF-8 file read/edit/save");
    }
};

int WINAPI wWinMain(HINSTANCE app,HINSTANCE,PWSTR,int) {
    int argc=0; auto raw=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(!raw)return 1;
    const std::wstring arg=argc>1?raw[1]:L"";
    const fs::path testPath=argc>2?raw[2]:L"";
    LocalFree(raw);
    // The Windows hook discovers the containing EXE itself and mounts its appended wish.zip.
    (TclZipfs_AppHook)(&argc,nullptr);
    Tcl_Interp *interp=Tcl_CreateInterp();bool registered=false;int result=0;
    try {
        if(Tcl_Init(interp)!=TCL_OK || Tk_Init(interp)!=TCL_OK ||
           Tcl_Eval(interp,"if {![zipfs exists $tcl_library/init.tcl] || ![zipfs exists $tk_library/tk.tcl]} {error {Tcl/Tk script libraries were not loaded from embedded ZipFS}}; wm title . {NP4-Tk native}; wm geometry . 900x600; ttk::frame .editor; pack .editor -fill both -expand 1; update")!=TCL_OK)
            throw std::runtime_error(Tcl_GetString(Tcl_GetObjResult(interp)));
        Tk_Window frame=Tk_NameToWindow(interp,".editor",Tk_MainWindow(interp));
        if(!frame)throw std::runtime_error("Missing Tk editor frame");
        Scintilla_LoadDpiForWindow();registered=Scintilla_RegisterClasses(app)!=0;
        if(!registered)throw std::runtime_error("Cannot register fork");
        bool test=arg==L"--self-test", bench=arg==L"--bench", fileTest=arg==L"--file-self-test";
        if(fileTest && testPath.empty())throw std::runtime_error("Missing fixture");
        Editor editor(frame,app,fileTest?testPath:(test||bench?fs::path{}:fs::path(arg)));
        if(test)editor.selfTest();else if(fileTest)editor.fileTest();else if(bench)result=editor.bench();else Tk_MainLoop();
    } catch (const std::exception &err) {std::fprintf(stderr,"NP4-Tk: %s\n",err.what());result=1;}
    if(registered) Scintilla_ReleaseResources();Tcl_DeleteInterp(interp);Tcl_Finalize();return result;
}
