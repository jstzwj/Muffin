#include "app/DocumentSession.h"
#include "blocks/html/HtmlBlockController.h"
#include "blocks/html/HtmlSanitizer.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/SelectionController.h"

#include <cstdlib>
#include <iostream>

using namespace muffin;

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << "\n";
    std::exit(1);
  }
}

MarkdownNode* firstHtmlBlock(DocumentSession& session) {
  for (const auto& child : session.document().root().children()) {
    if (child->type() == BlockType::HtmlBlock) {
      return child.get();
    }
  }
  return nullptr;
}

void setHtmlHit(SelectionController& selection, MarkdownNode* html, qsizetype offset = 0) {
  HitTestResult hit;
  hit.zone = HitTestResult::Zone::Html;
  hit.blockId = html->id();
  hit.textNodeId = html->id();
  hit.textOffset = offset;
  selection.setHitResult(hit);
}

void testEnterEditAndTextEditing() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  HtmlBlockController controller;
  controller.setDocumentSession(&session);
  controller.setSelectionController(&selection);
  controller.setUndoStack(&undoStack);
  controller.setBrushQueue(&brushQueue);

  session.setMarkdownText(QStringLiteral("<div>alpha</div>"), false);
  MarkdownNode* html = firstHtmlBlock(session);
  require(html != nullptr, "html block missing");
  setHtmlHit(selection, html, 0);

  require(controller.enterEditMode(), "enter html edit should work");
  require(controller.isEditing(), "controller should be in html edit mode");
  require(selection.cursorPosition().text.textOffset == html->literal().size(), "enter html edit cursor mismatch");

  require(controller.insertText(QStringLiteral("\n<span>beta</span>")), "html insert should work");
  require(session.markdownText().contains(QStringLiteral("<span>beta</span>")), "html insert markdown mismatch");
  require(undoStack.canUndo(), "html insert should push undo");
  EditTransaction htmlInsertUndo = undoStack.takeUndo();
  require(htmlInsertUndo.isReplaceNodeCommand(), "html insert should use ReplaceNodeCommand");
  require(htmlInsertUndo.replaceNodeCommand().nodeType == BlockType::HtmlBlock, "html insert command type mismatch");

  require(controller.deleteBackward(), "html backspace should work");
  require(session.markdownText().contains(QStringLiteral("<span>beta</span")), "html backspace markdown mismatch");
}

void testSetHtmlRoundtripAndSanitizer() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  HtmlBlockController controller;
  controller.setDocumentSession(&session);
  controller.setSelectionController(&selection);
  controller.setUndoStack(&undoStack);
  controller.setBrushQueue(&brushQueue);

  session.setMarkdownText(QStringLiteral("<section>old</section>"), false);
  MarkdownNode* html = firstHtmlBlock(session);
  require(html != nullptr, "html block missing for set html test");
  setHtmlHit(selection, html, 0);

  require(controller.enterEditMode(), "enter html edit should work for set html test");
  const QString source = QStringLiteral("<div onclick=\"evil()\"><a href=\"javascript:alert(1)\">x</a><script>alert(2)</script></div>");
  require(controller.setHtml(source), "set html should work");
  require(session.markdownText().contains(QStringLiteral("onclick=\"evil()\"")), "raw html roundtrip mismatch");
  require(undoStack.canUndo(), "set html should push undo");
  EditTransaction setHtmlUndo = undoStack.takeUndo();
  require(setHtmlUndo.isReplaceNodeCommand(), "set html should use ReplaceNodeCommand");

  const QString preview = controller.sanitizedPreview();
  require(!preview.contains(QStringLiteral("<script"), Qt::CaseInsensitive), "script tag should be removed");
  require(!preview.contains(QStringLiteral("onclick"), Qt::CaseInsensitive), "event attribute should be removed");
  require(!preview.contains(QStringLiteral("javascript:"), Qt::CaseInsensitive), "javascript URL should be removed");
  require(preview.contains(QStringLiteral("href=\"#\"")), "javascript href should be neutralized");
}

void testStandaloneSanitizer() {
  const QString html = QStringLiteral("<img src=javascript:evil onerror='x'><SCRIPT>bad()</SCRIPT><p onClick=test>ok</p>");
  const QString preview = HtmlSanitizer().sanitizedPreview(html);
  require(!preview.contains(QStringLiteral("script"), Qt::CaseInsensitive), "standalone sanitizer should remove script");
  require(!preview.contains(QStringLiteral("onerror"), Qt::CaseInsensitive), "standalone sanitizer should remove onerror");
  require(!preview.contains(QStringLiteral("onclick"), Qt::CaseInsensitive), "standalone sanitizer should remove onclick");
  require(!preview.contains(QStringLiteral("javascript:"), Qt::CaseInsensitive), "standalone sanitizer should remove javascript URL");
}

}  // namespace

int main() {
  testEnterEditAndTextEditing();
  testSetHtmlRoundtripAndSanitizer();
  testStandaloneSanitizer();
  return 0;
}
