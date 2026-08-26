#pragma once

#include <QtGlobal>

class QString;
class QObject;

namespace muffin {

class EditorController;
class EditorView;
class VirtualSourceEdit;

namespace a11y {

// Controller↔view registry backing the EditorView accessible adapter. Registered from
// EditorController::attach, cleared from detach; the adapter factory cannot reach the
// controller otherwise (EditorView deliberately holds no controller pointer).
void registerController(EditorView* view, EditorController* controller);
void unregisterController(EditorView* view);
EditorController* controllerFor(const EditorView* view);

}  // namespace a11y

// Install the QAccessible factory for the two editor canvases (rendered EditorView with its
// EditorController mapping, and the source-mode VirtualSourceEdit). Call once before any
// editor widget is created. Offscreen/no-screen-reader environments are unaffected — the
// factory only produces interfaces when something queries them.
void installEditorAccessibility();

}  // namespace muffin
