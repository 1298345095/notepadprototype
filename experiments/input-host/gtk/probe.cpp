#include <wx/wx.h>
#include <wx/nativewin.h>
#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>
#include "Scintilla.h"
#include "ScintillaWidget.h"
#include <cstdio>
#include <string>
#include <string_view>

// Isolate the host with upstream Scintilla GTK3; the fork uses its own backend and ABI.
static sptr_t send(GtkWidget *widget, unsigned message, uptr_t a = 0, sptr_t b = 0) {
    return scintilla_send_message(SCINTILLA(widget), message, a, b);
}
static std::string document(GtkWidget *widget) {
    const auto length = static_cast<size_t>(send(widget, SCI_GETLENGTH));
    std::string text(length + 1, 0);
    send(widget, SCI_GETTEXT, text.size(), reinterpret_cast<sptr_t>(text.data()));
    text.resize(length);
    return text;
}
static std::string hex(std::string_view bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (unsigned char c : bytes) {
        out += digits[c >> 4];
        out += digits[c & 15];
    }
    return out;
}
static void notify(ScintillaObject *obj, gint, SCNotification *scn, gpointer) {
    GtkWidget *widget = GTK_WIDGET(obj);
    if (scn->nmhdr.code == SCN_CHARADDED) {
        char bytes[8]{};
        int count = g_unichar_validate(scn->ch) ? g_unichar_to_utf8(scn->ch, bytes) : 0;
        bool marked = scn->characterSource == SC_CHARACTERSOURCE_TENTATIVE_INPUT;
        std::printf("notification=charAdded source=%d char=%d pos=%ld\n",
                    scn->characterSource, scn->ch, static_cast<long>(send(widget, SCI_GETCURRENTPOS)));
        // Here marked is the tentative character's byte count; actual per-keystroke
        // preedit contents/cursor are captured from the Wayland trace by the runner.
        std::printf("edit marked=%d replace=0 incoming=%s doc=%s\n", marked ? count : 0,
            hex(std::string_view(bytes, count)).c_str(), hex(document(widget)).c_str());
    } else if (scn->nmhdr.code == SCN_MODIFIED &&
               (scn->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))) {
        std::printf("notification=modified flags=%d pos=%ld len=%ld\n", scn->modificationType,
                    static_cast<long>(scn->position), static_cast<long>(scn->length));
        // The editor can cancel the last preedit with deletion and no CHARADDED.
        std::printf("edit marked=0 replace=0 incoming= doc=%s\n", hex(document(widget)).c_str());
    } else if (scn->nmhdr.code == SCN_FOCUSIN || scn->nmhdr.code == SCN_FOCUSOUT) {
        std::printf("notification=focus state=%d\n", scn->nmhdr.code == SCN_FOCUSIN);
    }
}
static void contracts(GtkWidget *w) {
    send(w, SCI_SETCODEPAGE, SC_CP_UTF8);
    send(w, SCI_SETIMEINTERACTION, SC_IME_INLINE);
    std::string fixture = "{\n A中😀B\n}\nafter\n";
    send(w, SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(fixture.c_str()));
    send(w, SCI_SETFOLDLEVEL, 0, SC_FOLDLEVELBASE | SC_FOLDLEVELHEADERFLAG);
    send(w, SCI_SETFOLDLEVEL, 1, SC_FOLDLEVELBASE + 1);
    send(w, SCI_SETFOLDLEVEL, 2, SC_FOLDLEVELBASE + 1);
    send(w, SCI_SETFOLDLEVEL, 3, SC_FOLDLEVELBASE);
    send(w, SCI_FOLDLINE, 0, SC_FOLDACTION_CONTRACT);
    bool fold = !send(w, SCI_GETLINEVISIBLE, 1) && send(w, SCI_DOCLINEFROMVISIBLE, 1) == 3;
    send(w, SCI_FOLDLINE, 0, SC_FOLDACTION_EXPAND);
    const sptr_t b = fixture.find("B");
    send(w, SCI_SETSEL, fixture.find("中"), b);
    bool restored = send(w, SCI_DOCLINEFROMVISIBLE, 1) == 1;
    sptr_t x = send(w, SCI_POINTXFROMPOSITION, 0, b);
    sptr_t y = send(w, SCI_POINTYFROMPOSITION, 0, b);
    sptr_t hit = send(w, SCI_POSITIONFROMPOINTCLOSE, x, y + 3);
    send(w, SCI_AUTOCSHOW, 0, reinterpret_cast<sptr_t>("alpha beta"));
    bool list = send(w, SCI_AUTOCACTIVE);
    send(w, SCI_AUTOCCANCEL);
    std::printf("contract fold_map=%d expanded_map=%d point_hit=%ld expected=%ld popup=%d ime_inline=%ld\n",
                fold, restored, (long)hit, (long)b, list, (long)send(w, SCI_GETIMEINTERACTION));
    send(w, SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(""));
    send(w, SCI_EMPTYUNDOBUFFER);
}
static gboolean mapped(gpointer data) {
    GtkWidget *editor = GTK_WIDGET(data);
    if (!gtk_widget_get_mapped(editor) || gtk_widget_get_allocated_width(editor) < 60)
        return G_SOURCE_CONTINUE;
    auto *top = gtk_widget_get_toplevel(editor);
    auto *display = gtk_widget_get_display(editor);
    bool native = GDK_IS_WAYLAND_DISPLAY(display) &&
                  gdk_wayland_window_get_wl_surface(gtk_widget_get_window(top));
    contracts(editor);
    g_signal_connect(editor, SCINTILLA_NOTIFY, G_CALLBACK(notify), nullptr);
    gtk_widget_grab_focus(editor);
    std::printf("control=wxGTK+upstream-Scintilla-5.6.8 backend=%s focus_widget=%d\n",
                G_OBJECT_TYPE_NAME(display), gtk_window_get_focus(GTK_WINDOW(top)) == editor);
    std::puts(native ? "ready=real_wayland_surface" : "ready=non_wayland");
    return G_SOURCE_REMOVE;
}
class ControlApp : public wxApp {
public:
    bool OnInit() override {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        auto *frame = new wxFrame(nullptr, wxID_ANY, "NP4 Real Pinyin Probe",
                                  wxDefaultPosition, wxSize(640,300));
        GtkWidget *editor = scintilla_new();
        auto *wrapped = new wxNativeWindow(frame, wxID_ANY, editor);
        auto *sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(wrapped, 1, wxEXPAND);
        frame->SetSizer(sizer);
        frame->Show(); frame->Layout();
        g_timeout_add(50, mapped, editor);
        return true;
    }
};
wxIMPLEMENT_APP(ControlApp);
