#include "document/DocumentSession.h"
#include "blocks/html/HtmlSanitizer.h"
#include "blocks/literal/LiteralBlockController.h"
#include "blocks/literal/LiteralBlockUtil.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/EditorContext.h"
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

LiteralBlockSpec htmlSpec() {
  return LiteralBlockSpec{
      BlockType::HtmlBlock,
      HitTestResult::Zone::Html,
      QStringLiteral("No HTML block is active."),
      QStringLiteral("Edit HTML Block"),
      QStringLiteral("Backspace HTML Block"),
      QStringLiteral("Delete HTML Block Text"),
      QStringLiteral("Delete HTML Block Selection"),
      QStringLiteral("Set HTML Block"),
      QStringLiteral("  ")};
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
  LiteralBlockController controller(htmlSpec());
  controller.setContext({&session, &selection, &undoStack, &brushQueue});

  session.setMarkdownText(QStringLiteral("<div>alpha</div>"), false);
  MarkdownNode* html = firstHtmlBlock(session);
  require(html != nullptr, "html block missing");
  setHtmlHit(selection, html, 0);

  require(controller.enterEditMode(), "enter html edit should work");
  require(controller.isEditing(), "controller should be in html edit mode");
  require(selection.cursorPosition().text.textOffset == html->literal().size(), "enter html edit cursor mismatch");

  require(controller.insertText(QStringLiteral("\n<span>beta</span>")), "html insert should work");
  require(session.markdownText().toString().contains(QStringLiteral("<span>beta</span>")), "html insert markdown mismatch");
  require(undoStack.canUndo(), "html insert should push undo");
  EditTransaction htmlInsertUndo = undoStack.takeUndo();
  require(htmlInsertUndo.isReplaceNodeCommand(), "html insert should use ReplaceNodeCommand");
  require(htmlInsertUndo.replaceNodeCommand().nodeType == BlockType::HtmlBlock, "html insert command type mismatch");

  require(controller.deleteBackward(), "html backspace should work");
  require(session.markdownText().toString().contains(QStringLiteral("<span>beta</span")), "html backspace markdown mismatch");
}

void testSetHtmlRoundtripAndSanitizer() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  LiteralBlockController controller(htmlSpec());
  controller.setContext({&session, &selection, &undoStack, &brushQueue});

  session.setMarkdownText(QStringLiteral("<section>old</section>"), false);
  MarkdownNode* html = firstHtmlBlock(session);
  require(html != nullptr, "html block missing for set html test");
  setHtmlHit(selection, html, 0);

  require(controller.enterEditMode(), "enter html edit should work for set html test");
  const QString source = QStringLiteral("<div onclick=\"evil()\"><a href=\"javascript:alert(1)\">x</a><script>alert(2)</script></div>");
  require(controller.setContent(source), "set html should work");
  require(session.markdownText().toString().contains(QStringLiteral("onclick=\"evil()\"")), "raw html roundtrip mismatch");
  require(undoStack.canUndo(), "set html should push undo");
  EditTransaction setHtmlUndo = undoStack.takeUndo();
  require(setHtmlUndo.isReplaceNodeCommand(), "set html should use ReplaceNodeCommand");

  const QString preview = sanitizedHtmlPreview(controller);
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

void testSanitizerClosesBypassClasses() {
  // Slash-delimited event handler: the old \s+on regex missed `<img/onerror=...>`.
  require(!HtmlSanitizer().sanitizedPreview(QStringLiteral("<img/onerror=alert(1) src=x>"))
              .contains(QStringLiteral("onerror"), Qt::CaseInsensitive),
          "sanitizer must strip slash-separated event handlers");

  // <iframe> subtree dropped entirely (content included).
  const QString iframe = HtmlSanitizer().sanitizedPreview(
      QStringLiteral("<iframe src=\"javascript:alert(1)\">fallback</iframe>"));
  require(!iframe.contains(QStringLiteral("iframe"), Qt::CaseInsensitive), "iframe subtree must be removed");
  require(!iframe.contains(QStringLiteral("javascript:"), Qt::CaseInsensitive), "iframe javascript src must be removed");

  // <svg> can carry nested <script>/onload; drop the whole subtree.
  const QString svg = HtmlSanitizer().sanitizedPreview(
      QStringLiteral("<svg onload=\"alert(1)\"><script>alert(2)</script></svg>"));
  require(!svg.contains(QStringLiteral("svg"), Qt::CaseInsensitive), "svg subtree must be removed");
  require(!svg.contains(QStringLiteral("onload"), Qt::CaseInsensitive), "svg onload must be removed");
  require(!svg.contains(QStringLiteral("script"), Qt::CaseInsensitive), "svg nested script must be removed");

  // vbscript: and dangerous data: URIs are neutralized to '#'.
  require(HtmlSanitizer().sanitizedPreview(QStringLiteral("<a href=\"vbscript:alert(1)\">x</a>"))
              .contains(QStringLiteral("href=\"#\""), Qt::CaseInsensitive),
          "vbscript URL must be neutralized to '#'");
  require(!HtmlSanitizer().sanitizedPreview(QStringLiteral("<a href=\"data:text/html,<b>x</b>\">x</a>"))
              .contains(QStringLiteral("data:"), Qt::CaseInsensitive),
          "data:text/html URL must be neutralized");

  // Control-character obfuscation of the scheme must not bypass the check.
  require(!HtmlSanitizer().sanitizedPreview(QStringLiteral("<a href=\"java\tscript:alert(1)\">x</a>"))
              .contains(QStringLiteral("script:"), Qt::CaseInsensitive),
          "tab-obfuscated javascript scheme must be neutralized");

  // Benign structure and safe data: images survive the whitelist.
  const QString kept = HtmlSanitizer().sanitizedPreview(
      QStringLiteral("<p>hi <strong>there</strong></p><img src=\"data:image/png;base64,AAAA\">"));
  require(kept.contains(QStringLiteral("<p>"), Qt::CaseInsensitive), "benign <p> should be preserved");
  require(kept.contains(QStringLiteral("<strong>"), Qt::CaseInsensitive), "benign <strong> should be preserved");
  require(kept.contains(QStringLiteral("data:image/png;base64"), Qt::CaseInsensitive),
          "safe data:image URL should be preserved");
}

}  // namespace

int main() {
  testEnterEditAndTextEditing();
  testSetHtmlRoundtripAndSanitizer();
  testStandaloneSanitizer();
  testSanitizerClosesBypassClasses();
  return 0;
}
