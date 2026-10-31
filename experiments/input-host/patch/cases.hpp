#pragma once
#include "fork.hpp"
#include <cstdio>

struct zwp_text_input_v3;
void text_input_preedit_string(void *, zwp_text_input_v3 *, const char *, int32_t, int32_t);
void text_input_commit_string(void *, zwp_text_input_v3 *, const char *);
void text_input_delete_surrounding_text(void *, zwp_text_input_v3 *, uint32_t, uint32_t);
void text_input_done(void *, zwp_text_input_v3 *, uint32_t);

namespace probe {
using Scintilla::DocumentOption;
using Scintilla::Internal::Document;
struct State {
    Document doc{DocumentOption::Default};
    wl_surface *surface;
    Sci::Position caret = 4;
    std::vector<int> cursors;
    bool valid = true;
    int deletes = 0;
    int commits = 0;
    explicit State(wl_surface *s) : surface(s) { doc.InsertString(0, "A中😀B"); }
    std::string text() const {
        std::string s(doc.Length(), '\0');
        doc.GetCharRange(s.data(), 0, doc.Length());
        return s;
    }
};

bool consume(wl_surface *s, const Fl_Wayland_Text_Edit &e, void *data) {
    auto &state = *static_cast<State *>(data);
    if (s != state.surface) return false;
    if (e.has_deletion) {
        if (e.delete_before != 3 || e.delete_after != 4 || state.deletes++) {
            state.valid = false;
            return true;
        }
        state.caret -= e.delete_before;
        state.doc.DeleteChars(state.caret, e.delete_before + e.delete_after);
    }
    if (e.has_commit && e.commit) {
        state.valid &= std::string(e.commit) == "你好";
        state.doc.InsertString(state.caret, e.commit);
        state.caret += std::strlen(e.commit);
        ++state.commits;
    }
    if (e.has_preedit) {
        state.valid &= e.cursor_begin == e.cursor_end;
        state.valid &= e.preedit ? std::string(e.preedit) == "nihao" : e.cursor_begin == -1;
        state.cursors.push_back(e.cursor_begin);
    }
    return true;
}

int run(wl_surface *s, int &legacy_keys) {
    State state(s);
    fl_wl_set_edit_handler(consume, &state);
    text_input_delete_surrounding_text(s, nullptr, 3, 4);
    if (state.text() != "A中😀B") return 11;
    text_input_done(s, nullptr, 0);
    if (state.text() != "AB") return 12;
    text_input_preedit_string(s, nullptr, "nihao", 5, 5);
    text_input_done(s, nullptr, 0);
    text_input_preedit_string(s, nullptr, "nihao", 2, 2);
    text_input_done(s, nullptr, 0);
    if (state.text() != "AB") return 13;
    text_input_preedit_string(s, nullptr, nullptr, -1, -1);
    text_input_commit_string(s, nullptr, "你好");
    text_input_done(s, nullptr, 0);
    fl_wl_set_edit_handler(nullptr, nullptr);
    if (!state.valid || state.cursors != std::vector<int>{5, 2, -1} ||
        state.commits != 1 || state.deletes != 1 || state.text() != "A你好B" || legacy_keys) return 14;
    text_input_commit_string(s, nullptr, "x");
    text_input_done(s, nullptr, 0);
    std::printf("preedit-cursor=5,2,-1 deletion(3,4)=AB final=A你好B commits=1 duplicate-legacy=0 fallback-keys=%d\n", legacy_keys);
    return legacy_keys == 1 ? 0 : 15;
}
}
