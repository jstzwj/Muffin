#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/SelectionController.h"

#include "EditorTestUtils.h"

#include <QApplication>
#include <QClipboard>

#include <iostream>

using namespace muffin;

namespace {
// Place the caret on the virtual trailing paragraph below the last top-level block.
void placeTrailingCaret(EditorController& controller, const DocumentSession& session) {
  HitTestResult hit;
  hit.zone = HitTestResult::Zone::BlockAfter;
  hit.blockId = blockAt(session, 0)->id();
  hit.textNodeId = hit.blockId;
  controller.activateHit(hit);
}
}  // namespace

// Regression: backspace on the virtual trailing paragraph below a list did nothing (for a list
// it even hit the "unsupported edit" path, since the list node is not an editable text block).
// The caret must pull up to the end of the last block's content; the document text is unchanged
// because the trailing paragraph carries no text.
void testBackspaceFromTrailingAfterList() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);
  view.resize(720, 460);

  session.setMarkdownText(QStringLiteral("345. 123"), false);
  view.setDocument(session.document());
  placeTrailingCaret(controller, session);
  require(controller.selection().cursorPosition().afterBlock, "caret should start on the trailing paragraph");

  require(controller.inputController().deleteBackward(), "backspace from trailing paragraph should be handled");

  require(session.markdownText() == QStringLiteral("345. 123"), "trailing-paragraph backspace must not change the document text");
  const CursorPosition c = controller.selection().cursorPosition();
  require(!c.afterBlock, "caret should leave the virtual trailing paragraph");
  MarkdownNode* block = session.document().node(c.blockId);
  require(block != nullptr && block->type() == BlockType::ListItem, "caret should land on the last list item");
  require(c.text.sourceOffset == 8, "caret should sit at the end of the list item content (after '123')");
  require(c.text.textOffset == 3, "caret text offset should be 3 within the list item content");

  // A second backspace now deletes from the content end, proving the caret is real and editable.
  require(controller.inputController().deleteBackward(), "second backspace should delete a content character");
  require(session.markdownText() == QStringLiteral("345. 12"), "second backspace should delete the trailing '3'");
}

// Companion: the same behavior for a plain paragraph (guards the generic case and the existing
// "backspace on trailing paragraph is harmless" contract — markdown unchanged, caret valid).
void testBackspaceFromTrailingAfterParagraph() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);
  view.resize(720, 460);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  view.setDocument(session.document());
  placeTrailingCaret(controller, session);
  require(controller.selection().cursorPosition().afterBlock, "caret should start on the trailing paragraph");

  require(controller.inputController().deleteBackward(), "backspace from trailing paragraph should be handled");
  require(session.markdownText() == QStringLiteral("alpha"), "trailing-paragraph backspace must not change plain paragraph text");
  const CursorPosition c = controller.selection().cursorPosition();
  require(!c.afterBlock, "caret should leave the virtual trailing paragraph");
  require(c.text.sourceOffset == 5, "caret should sit at the end of 'alpha'");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
  testBackspaceFromTrailingAfterList();
  testBackspaceFromTrailingAfterParagraph();
  QApplication::clipboard()->clear();
  return 0;
}
