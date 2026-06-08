#include "editor/EditorContext.h"

#include "editor/BlockEditContext.h"
#include "editor/SelectionController.h"

namespace muffin {

bool EditorContext::hasSession() const {
  return session != nullptr;
}

bool EditorContext::hasSelection() const {
  return selection != nullptr;
}

bool EditorContext::hasCursor() const {
  return selection && selection->hasCursor();
}

bool EditorContext::hasEditingServices() const {
  return session && selection && undoStack && brushQueue;
}

BlockEditContextResolver EditorContext::contextResolver() const {
  return BlockEditContextResolver(session, selection);
}

}  // namespace muffin
