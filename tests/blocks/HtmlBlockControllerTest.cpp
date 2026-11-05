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
  return LiteralBlockSpec{BlockType::HtmlBlock,
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
  require(session.markdownText().toString().contains(QStringLiteral("<span>beta</span>")),
          "html insert markdown mismatch");
  require(undoStack.canUndo(), "html insert should push undo");
  EditTransaction htmlInsertUndo = undoStack.takeUndo();
  require(htmlInsertUndo.isReplaceNodeCommand(), "html insert should use ReplaceNodeCommand");
  require(htmlInsertUndo.replaceNodeCommand().nodeType == BlockType::HtmlBlock, "html insert command type mismatch");

  require(controller.deleteBackward(), "html backspace should work");
  require(session.markdownText().toString().contains(QStringLiteral("<span>beta</span")),
          "html backspace markdown mismatch");
}

void testExplicitEntryPublishesTheRequestedLiteralCaret() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  LiteralBlockController controller(htmlSpec());
  controller.setContext({&session, &selection, &undoStack, &brushQueue});
  const QString markdown = QStringLiteral("<div>alpha</div>\n\nparagraph\n\n<section>beta</section>\n");
  session.setMarkdownText(markdown, false);
  const auto first = firstHtmlBlock(session)->id();
  const auto& children = session.document().root().children();
  const auto second = children.back()->id();
  const auto paragraph = children.at(1)->id();
  setHtmlHit(selection, firstHtmlBlock(session));
  require(controller.enterEditMode(), "initial HTML source must activate");
  auto expected = controller.cursorFor(second, 4);
  int published = 0;
  QObject::connect(
      &selection, &SelectionController::selectionChanged, &selection, [&](SelectionRange range, HitTestResult) {
        ++published;
        require(controller.currentBlockId() == expected.blockId && SelectionRange{range.focus, expected}.isCollapsed(),
                "entry must publish only the normalized requested caret on the active block");
      });
  require(controller.enterEditMode(expected), "explicit entry must switch away from an already active block");
  require(published == 1, "explicit entry must publish exactly once");
  auto requested = expected;
  requested.text.textOffset = -50;
  requested.text.nodeId = first;
  requested.text.sourceOffset = 9999;
  expected = controller.cursorFor(second, 0);
  require(controller.enterEditMode(requested), "negative entry offsets must be clamped");
  requested.text.textOffset = 9999;
  expected = controller.cursorFor(second, 9999);
  require(controller.enterEditMode(requested), "oversized entry offsets must be clamped");
  requested.afterBlock = true;
  requested.text.textOffset = 0;
  require(controller.enterEditMode(requested), "a block-after entry must resolve to the end of the literal");
  require(published == 4, "each successful entry must publish one final caret");
  requested.blockId = paragraph;
  require(!controller.enterEditMode(requested), "explicit entry must reject another block type");
  require(published == 4 && controller.currentBlockId() == second &&
              SelectionRange{selection.cursorPosition(), expected}.isCollapsed(),
          "a rejected target must preserve the active source and caret without notification");
  require(session.markdownText().toString() == markdown && !undoStack.canUndo(),
          "literal activation must preserve document content and history");
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
  const QString source =
      QStringLiteral("<div onclick=\"evil()\"><a href=\"javascript:alert(1)\">x</a><script>alert(2)</script></div>");
  require(controller.setContent(source), "set html should work");
  require(session.markdownText().toString().contains(QStringLiteral("onclick=\"evil()\"")),
          "raw html roundtrip mismatch");
  require(undoStack.canUndo(), "set html should push undo");
  EditTransaction setHtmlUndo = undoStack.takeUndo();
  require(setHtmlUndo.isReplaceNodeCommand(), "set html should use ReplaceNodeCommand");

  const QString preview = sanitizedHtmlPreview(controller);
  require(!preview.contains(QStringLiteral("<script"), Qt::CaseInsensitive), "script tag should be removed");
  require(!preview.contains(QStringLiteral("onclick"), Qt::CaseInsensitive), "event attribute should be removed");
  require(!preview.contains(QStringLiteral("javascript:"), Qt::CaseInsensitive), "javascript URL should be removed");
  require(preview.contains(QStringLiteral("href=\"#\"")), "javascript href should be neutralized");
}

void testEditingIndentedHtmlPreservesSeparators() {
  const QString before = QStringLiteral("# Before\n\n");
  const QString html = QStringLiteral("  <div>\n  alpha\n  </div>\n");
  for (const QString& after : {QString(), QStringLiteral("\nplain"), QStringLiteral("\n\n\nplain")}) {
    DocumentSession session;
    SelectionController selection;
    UndoStack undoStack;
    BrushQueue brushQueue;
    LiteralBlockController controller(htmlSpec());
    controller.setContext({&session, &selection, &undoStack, &brushQueue});
    const QString original = before + html + after;
    session.setMarkdownText(original, false);
    MarkdownNode* block = firstHtmlBlock(session);
    require(block != nullptr, "indented HTML fixture should parse");
    setHtmlHit(selection, block);
    require(controller.enterEditMode(), "indented HTML edit should activate");
    CursorPosition cursor = selection.cursorPosition();
    cursor.text.textOffset = html.indexOf(QStringLiteral("alpha")) + 2;
    selection.setCursorPosition(cursor);
    require(controller.insertText(QStringLiteral("Z")), "indented HTML should accept typing");
    QString expected = original;
    expected.insert(before.size() + cursor.text.textOffset, QLatin1Char('Z'));
    require(session.markdownText().toString() == expected,
            "HTML typing must preserve leading indentation and following separators exactly");
    require(session.lastParseWasLocalEdit(), "HTML typing should remain a local edit");
    require(controller.deleteBackward(), "indented HTML should accept Backspace");
    require(session.markdownText().toString() == original,
            "HTML typing and Backspace must round-trip the complete Markdown source");
  }
}

void testStandaloneSanitizer() {
  const QString html =
      QStringLiteral("<img src=javascript:evil onerror='x'><SCRIPT>bad()</SCRIPT><p onClick=test>ok</p>");
  const QString preview = HtmlSanitizer().sanitizedPreview(html);
  require(!preview.contains(QStringLiteral("script"), Qt::CaseInsensitive),
          "standalone sanitizer should remove script");
  require(!preview.contains(QStringLiteral("onerror"), Qt::CaseInsensitive),
          "standalone sanitizer should remove onerror");
  require(!preview.contains(QStringLiteral("onclick"), Qt::CaseInsensitive),
          "standalone sanitizer should remove onclick");
  require(!preview.contains(QStringLiteral("javascript:"), Qt::CaseInsensitive),
          "standalone sanitizer should remove javascript URL");
}

void testSanitizerClosesBypassClasses() {
  // Slash-delimited event handler: the old \s+on regex missed `<img/onerror=...>`.
  require(!HtmlSanitizer()
               .sanitizedPreview(QStringLiteral("<img/onerror=alert(1) src=x>"))
               .contains(QStringLiteral("onerror"), Qt::CaseInsensitive),
          "sanitizer must strip slash-separated event handlers");

  // <iframe> subtree dropped entirely (content included).
  const QString iframe =
      HtmlSanitizer().sanitizedPreview(QStringLiteral("<iframe src=\"javascript:alert(1)\">fallback</iframe>"));
  require(!iframe.contains(QStringLiteral("iframe"), Qt::CaseInsensitive), "iframe subtree must be removed");
  require(!iframe.contains(QStringLiteral("javascript:"), Qt::CaseInsensitive),
          "iframe javascript src must be removed");

  // <svg> can carry nested <script>/onload; drop the whole subtree.
  const QString svg =
      HtmlSanitizer().sanitizedPreview(QStringLiteral("<svg onload=\"alert(1)\"><script>alert(2)</script></svg>"));
  require(!svg.contains(QStringLiteral("svg"), Qt::CaseInsensitive), "svg subtree must be removed");
  require(!svg.contains(QStringLiteral("onload"), Qt::CaseInsensitive), "svg onload must be removed");
  require(!svg.contains(QStringLiteral("script"), Qt::CaseInsensitive), "svg nested script must be removed");

  // vbscript: and dangerous data: URIs are neutralized to '#'.
  require(HtmlSanitizer()
              .sanitizedPreview(QStringLiteral("<a href=\"vbscript:alert(1)\">x</a>"))
              .contains(QStringLiteral("href=\"#\""), Qt::CaseInsensitive),
          "vbscript URL must be neutralized to '#'");
  require(!HtmlSanitizer()
               .sanitizedPreview(QStringLiteral("<a href=\"data:text/html,<b>x</b>\">x</a>"))
               .contains(QStringLiteral("data:"), Qt::CaseInsensitive),
          "data:text/html URL must be neutralized");

  // Control-character obfuscation of the scheme must not bypass the check.
  require(!HtmlSanitizer()
               .sanitizedPreview(QStringLiteral("<a href=\"java\tscript:alert(1)\">x</a>"))
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

} // namespace

int main() {
  testEnterEditAndTextEditing();
  testExplicitEntryPublishesTheRequestedLiteralCaret();
  testEditingIndentedHtmlPreservesSeparators();
  testSetHtmlRoundtripAndSanitizer();
  testStandaloneSanitizer();
  testSanitizerClosesBypassClasses();
  return 0;
}
