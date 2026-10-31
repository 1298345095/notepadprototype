#include "fork_editor.hpp"

unsigned int dwUrlThreshold = 256;

namespace Scintilla::Internal {
// The fork keeps these Editor methods in ScintillaWin.cxx alongside HWND redraw
// suppression. Coalesce the final invalidation through the active platform view.
void Editor::BeginBatchUpdate() noexcept {
    if (++batchUpdateDepth == 1) {
        batchUpdateState.modEventMask = modEventMask;
        modEventMask = ModificationFlags::None;
        batchUpdateState.actions = pdoc->UndoActions();
        batchUpdateState.lines = pdoc->LinesTotal();
    }
}

void Editor::EndBatchUpdate() noexcept {
    if (batchUpdateDepth > 0 && --batchUpdateDepth == 0) {
        modEventMask = batchUpdateState.modEventMask;
        wMain.InvalidateAll();
        if (batchUpdateState.actions != pdoc->UndoActions()) {
            NotificationData n{};
            n.nmhdr.code = Notification::Modified;
            n.linesAdded = pdoc->LinesTotal() - batchUpdateState.lines;
            NotifyParent(n);
        }
    }
}
}
