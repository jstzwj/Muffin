#pragma once

namespace muffin {

class DocumentSession;
class SelectionController;
class UndoStack;
class BrushQueue;

struct EditorContext {
  DocumentSession* session = nullptr;
  SelectionController* selection = nullptr;
  UndoStack* undoStack = nullptr;
  BrushQueue* brushQueue = nullptr;
};

}  // namespace muffin
