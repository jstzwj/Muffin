#pragma once

#include <Qt>

// Which Ctrl-combos the editor canvas owns instead of letting them fall through to QAction
// shortcuts / the default widget handling. Shared by InputController (key dispatch + the
// ShortcutOverride event filter) and EditorView::event so the two can never disagree.
//
// Known QAction collision: Ctrl+Shift+Backspace is table.delete_row, so Backspace/Delete are
// only owned without Shift.
namespace muffin::editor_keys {

inline bool isEditorOwnedCtrlKey(int key, bool shift) {
  switch (key) {
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
      return true;
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
      return !shift;
    default:
      return false;
  }
}

}  // namespace muffin::editor_keys
