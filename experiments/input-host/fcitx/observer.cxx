#include "fork.hpp"
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Window.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>
#include <cstdio>

using namespace Scintilla;
using namespace Scintilla::Internal;

// An intentionally bounded text endpoint. The fork Document stores every edit,
// while public FLTK compose events supply append/replace-at-end operations.
class Endpoint : public Fl_Box {
public:
    Endpoint() : Fl_Box(10, 10, 460, 100), document(DocumentOption::Default) {
        align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        labelsize(18);
        document.SetUndoCollection(false);
    }
    int handle(int event) override {
        if (event == FL_FOCUS || event == FL_UNFOCUS) {
            std::printf("focus=%d\n", event == FL_FOCUS);
            return 1;
        }
        if (event == FL_PUSH) return take_focus();
        if (event == FL_KEYDOWN) {
            int replace = 0;
            if (!Fl::compose(replace)) return Fl_Box::handle(event);
            const auto end = document.Length();
            if (replace < 0 || replace > end) {
                std::printf("invalid_replace=%d\n", replace);
                return 1;
            }
            if (replace) document.DeleteChars(end - replace, replace);
            auto bytes = Fl::event_length();
            if (bytes) document.InsertString(document.Length(), Fl::event_text(), bytes);
            std::string full(static_cast<size_t>(document.Length()), '\0');
            if (!full.empty()) document.GetCharRange(full.data(), 0, document.Length());
            copy_label(full.c_str());
            std::printf("edit marked=%d replace=%d incoming=", Fl::compose_state, replace);
            for (int i=0; i<bytes; ++i) std::printf("%02x", static_cast<unsigned char>(Fl::event_text()[i]));
            std::printf(" doc=");
            for (unsigned char byte : full) std::printf("%02x", byte);
            std::printf("\n");
            redraw();
            return 1;
        }
        return Fl_Box::handle(event);
    }
private:
    Document document;
    void draw() override {
        Fl_Box::draw();
        fl_font(FL_HELVETICA, 18);
        const int lineEnd = label() ? static_cast<int>(fl_width(label())) : 0;
        fl_set_spot(FL_HELVETICA, 18, x()+4+lineEnd, y()+h()/2+8, 0, 22, window());
    }
};
int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    CharClassify::InitUnicodeData();
    Fl_Window window(480, 120, "NP4 Real Pinyin Probe");
    Endpoint endpoint;
    window.end();
    window.show();
    endpoint.take_focus();
    if (!fl_wl_display() || !fl_wl_xid(&window) || !fl_wl_surface(fl_wl_xid(&window))) return 3;
    std::puts("ready=real_wayland_surface");
    return Fl::run();
}
