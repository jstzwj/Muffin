#pragma once

#include <QHash>

namespace muffin {

class DocumentSession;
class SelectionController;
class UndoStack;
class BrushQueue;
class EditorView;
class LiteralBlockController;

struct EditorContext {
  DocumentSession* session = nullptr;
  SelectionController* selection = nullptr;
  UndoStack* undoStack = nullptr;
  BrushQueue* brushQueue = nullptr;
  EditorView* view = nullptr;
  QHash<int, LiteralBlockController*> literalEditors;
};

}  // namespace muffin
