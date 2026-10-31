#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/platform.H>
#include "cases.hpp"

class Frame : public Fl_Window {
public:
    using Fl_Window::Fl_Window;
    bool visible_frame = false;
    bool recording = false;
    int legacy_keys = 0;
    int handle(int e) override {
        if (e == FL_FOCUS) return 1;
        if (recording && e == FL_KEYDOWN) {
            ++legacy_keys;
            return 1;
        }
        return Fl_Window::handle(e);
    }
    void draw() override {
        visible_frame = true;
        Fl_Window::draw();
    }
};

int main() {
    Frame frame(320, 120, "fltk input batch study");
    frame.end();
    frame.show();
    for (int i = 0; !frame.visible_frame && i < 100; ++i) Fl::wait(.05);
    if (!fl_wl_display() || !frame.visible_frame || !fl_wl_xid(&frame)) return 2;
    auto *surface = fl_wl_surface(fl_wl_xid(&frame));
    if (!surface) return 3;
    Fl::focus(&frame);
    frame.recording = true;
    return probe::run(surface, frame.legacy_keys);
}
