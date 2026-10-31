#include "fork.hpp"
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Window.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>
#include <cstdio>

using namespace Scintilla;
using namespace Scintilla::Internal;

static void print_hex(std::string_view text) {
    for (unsigned char byte : text) std::printf("%02x", byte);
}

class Endpoint : public Fl_Box {
    Document document{DocumentOption::Default};
    Sci::Position caret = 0;
    Sci::Position screen_caret = 0;
    std::string full;
public:
    Endpoint() : Fl_Box(10,10,460,100) {
        align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        labelsize(18);
    }
    int handle(int event) override {
        if (event == FL_FOCUS || event == FL_UNFOCUS) return 1;
        if (event == FL_PUSH) return take_focus();
        return Fl_Box::handle(event);
    }
    static bool receive(wl_surface *s, const Fl_Wayland_Text_Edit &e, void *user) {
        auto &editor = *static_cast<Endpoint *>(user);
        if (Fl::focus() != &editor || s != fl_wl_surface(fl_wl_xid(editor.window()))) return false;
        editor.apply(e);
        return true;
    }
    void apply(const Fl_Wayland_Text_Edit &edit) {
        document.TentativeUndo();
        if (edit.has_deletion) {
            if (edit.delete_before > static_cast<size_t>(caret) ||
                edit.delete_after > static_cast<size_t>(document.Length()-caret)) {
                std::puts("invalid_replace=outside_document");
                return;
            }
            caret -= edit.delete_before;
            document.DeleteChars(caret, edit.delete_before + edit.delete_after);
        }
        const std::string_view commit = edit.commit ? edit.commit : "";
        if (!commit.empty()) {
            document.InsertString(caret, commit);
            caret += commit.size();
        }
        const std::string_view preedit = edit.preedit ? edit.preedit : "";
        screen_caret = caret;
        if (!preedit.empty()) {
            document.TentativeStart();
            document.InsertString(caret, preedit);
            if (edit.cursor_begin >= 0 && static_cast<size_t>(edit.cursor_begin) <= preedit.size())
                screen_caret += edit.cursor_begin;
        }
        full.resize(document.Length());
        if (!full.empty()) document.GetCharRange(full.data(), 0, full.size());
        copy_label(full.c_str());
        if (edit.has_preedit) {
            std::printf("preedit begin=%d end=%d hex=",edit.cursor_begin,edit.cursor_end);
            print_hex(preedit);
            std::puts("");
        }
        std::printf("edit marked=%zu replace=0 incoming=", preedit.size());
        print_hex(preedit.empty() ? commit : preedit);
        std::printf(" doc=");
        print_hex(full);
        std::puts("");
        redraw();
    }
    void draw() override {
        Fl_Box::draw();
        fl_font(FL_HELVETICA,18);
        int advance = static_cast<int>(fl_width(full.data(),static_cast<int>(screen_caret)));
        fl_set_spot(FL_HELVETICA,18,x()+4+advance,y()+h()/2+8,0,22,window());
    }
};

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    CharClassify::InitUnicodeData();
    Fl_Window frame(480,120,"NP4 Real Pinyin Probe");
    Endpoint endpoint;
    frame.end();
    frame.show();
    endpoint.take_focus();
    if (!fl_wl_display() || !fl_wl_xid(&frame) || !fl_wl_surface(fl_wl_xid(&frame))) return 3;
    fl_wl_set_edit_handler(Endpoint::receive, &endpoint);
    std::puts("endpoint=patched-fltk-real-fork-document");
    std::puts("ready=real_wayland_surface");
    int result = Fl::run();
    fl_wl_set_edit_handler(nullptr, nullptr);
    return result;
}
