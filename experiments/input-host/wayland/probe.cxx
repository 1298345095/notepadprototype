#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/platform.H>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#ifndef FLTK_USE_WAYLAND
#error Build FLTK with the native Wayland backend for this probe.
#endif

// These are internal callbacks from the pinned FLTK static library.
// We call them to replay protocol input; they are not application APIs.
struct zwp_text_input_v3;
void text_input_preedit_string(void *, zwp_text_input_v3 *, const char *, int32_t, int32_t);
void text_input_commit_string(void *, zwp_text_input_v3 *, const char *);
void text_input_delete_surrounding_text(void *, zwp_text_input_v3 *, uint32_t, uint32_t);
void text_input_done(void *, zwp_text_input_v3 *, uint32_t);

class Observer : public Fl_Window {
public:
    Observer() : Fl_Window(320, 120, "FLTK Wayland callback probe") {}
    int draws = 0;
    std::vector<std::string> edits;
    bool recording = false;
    int handle(int event) override {
        if (recording && event == FL_KEYDOWN) {
            int replace = 0;
            Fl::compose(replace);
            edits.emplace_back(Fl::event_text(), Fl::event_length());
            return 1;
        }
        if (event == FL_FOCUS) return 1;
        return Fl_Window::handle(event);
    }
private:
    void draw() override {
        ++draws;
        Fl_Window::draw();
    }
};

int main() {
    Observer win;
    win.end();
    win.show();
    for (int i = 0; i < 150 && !win.draws; ++i) Fl::wait(0.04);
    if (!fl_wl_display() || !fl_wl_xid(&win) || !win.draws) return 2;
    auto *surface = fl_wl_surface(fl_wl_xid(&win));
    if (!surface) return 3;
    Fl::focus(&win);
    win.recording = true;
    text_input_delete_surrounding_text(surface, nullptr, 3, 4);
    text_input_done(surface, nullptr, 0);
    const auto deletion = win.edits.size();
    text_input_preedit_string(surface, nullptr, "nihao", 5, 5);
    text_input_done(surface, nullptr, 0);
    const bool initial = win.edits.size() > deletion && win.edits.back() == "nihao";
    const auto before = win.edits.size();
    text_input_preedit_string(surface, nullptr, "nihao", 2, 2);
    text_input_done(surface, nullptr, 0);
    const auto cursor_move = win.edits.size() - before;
    const auto before_commit = win.edits.size();
    text_input_commit_string(surface, nullptr, "你好");
    text_input_done(surface, nullptr, 0);
    const bool commit = win.edits.size() > before_commit && win.edits.back() == "你好";
    std::printf("native_wayland_surface=1 draws=%d initial_text_seen=%d cursor_only_widget_events=%zu delete_surrounding_widget_events=%zu commit_text_seen=%d\n",
        win.draws, initial, cursor_move, deletion, commit);
    return (initial && commit && cursor_move == 0 && deletion == 0) ? 0 : 1;
}
