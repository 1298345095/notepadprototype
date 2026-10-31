#include "fork_editor.hpp"
#include "study.hpp"
#include <gtk/gtk.h>
#include "ScintillaWidget.h"
#include <wx/wx.h>
#include <wx/nativewin.h>
#include <cstdio>
#include <string>

namespace {
bool ready = false;
std::string filePath;

void notify(ScintillaObject *, gint, Scintilla::NotificationData *event, void *) {
    if (ready)
        np4qa::notify(static_cast<unsigned>(event->nmhdr.code), event->ch,
                      static_cast<int>(event->characterSource), event->listType);
}

gboolean manualComplete(GtkWidget *, GdkEventKey *event, gpointer) {
    const auto modifiers = gtk_accelerator_get_default_mod_mask();
    if (event->keyval == GDK_KEY_space && (event->state & modifiers) == GDK_CONTROL_MASK) {
        np4qa::complete();
        return TRUE;
    }
    return FALSE;
}

gboolean tick(gpointer data) {
    auto *editor = GTK_WIDGET(data);
    if (!gtk_widget_get_mapped(editor) || gtk_widget_get_allocated_width(editor) < 80)
        return G_SOURCE_CONTINUE;
    if (!ready) {
        gtk_widget_grab_focus(editor);
        ready = true;
        np4qa::attach([editor](unsigned message, uintptr_t wp, intptr_t lp) {
            return scintilla_send_message(SCINTILLA(editor), message, wp, lp);
        }, filePath.empty() ? nullptr : filePath.c_str());
        std::printf("native_display=%s host=wxGTK gtk=%d.%d.%d\n",
                    G_OBJECT_TYPE_NAME(gtk_widget_get_display(editor)),
                    gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version());
        std::fflush(stdout);
    }
    int x = 0, y = 0;
    gtk_widget_translate_coordinates(editor, gtk_widget_get_toplevel(editor), 0, 0, &x, &y);
    np4qa::tick(x, y);
    return G_SOURCE_CONTINUE;
}

class App : public wxApp {
public:
    bool OnInit() override {
        Scintilla::Internal::CharClassify::InitUnicodeData();
        if (argc > 1) filePath = wxString(argv[1]).ToStdString(wxConvUTF8);
        auto *frame = new wxFrame(nullptr, wxID_ANY, "NP4 wx Study", wxDefaultPosition, wxSize(720, 440));
        GtkWidget *editor = scintilla_new();
        // Keep keyboard and IM events on Scintilla. wx wraps only the container.
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_box_pack_start(GTK_BOX(box), editor, true, true, 0);
        gtk_widget_show(editor);
        auto *host = new wxNativeWindow(frame, wxID_ANY, box);
        auto *sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(host, 1, wxEXPAND);
        frame->SetSizer(sizer);
        g_signal_connect(editor, SCINTILLA_NOTIFY, G_CALLBACK(notify), nullptr);
        g_signal_connect(editor, "key-press-event", G_CALLBACK(manualComplete), nullptr);
        const guint timer = g_timeout_add(30, tick, editor);
        g_signal_connect(editor, "destroy", G_CALLBACK(+[](GtkWidget *, gpointer value) {
            g_source_remove(GPOINTER_TO_UINT(value));
        }), GUINT_TO_POINTER(timer));
        frame->Show();
        return true;
    }
};
}
wxIMPLEMENT_APP(App);
