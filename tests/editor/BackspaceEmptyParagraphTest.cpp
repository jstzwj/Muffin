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
MarkdownNode* findEmptyParagraph(MarkdownNode* node) {
  if (!node) return nullptr;
  if (node->type() == BlockType::Paragraph) {
    const SourceRange r = node->sourceRange();
    if (r.byteStart == r.byteEnd) return node;
  }
  MarkdownNode* found = nullptr;
  for (const auto& c : node->children()) {
    if (MarkdownNode* f = findEmptyParagraph(c.get())) found = f;
  }
  return found;
}

bool rootHasOnlyBlockType(const DocumentSession& session, BlockType type) {
  const auto& kids = session.document().root().children();
  return kids.size() == 1 && kids.at(0)->type() == type;
}

struct Harness {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  Harness() {
    controller.attach(&session, &view);
    view.resize(720, 460);
  }
  void load(const QString& md) {
    session.setMarkdownText(md, false);
    view.setDocument(session.document());
  }
  void placeInEmptyParagraph() {
    MarkdownNode* empty = findEmptyParagraph(&session.document().root());
    require(empty != nullptr, "document should contain an empty paragraph");
    setCursor(controller.selection(), empty, 0);
  }
  void placeInTrailing() {
    HitTestResult hit;
    hit.zone = HitTestResult::Zone::BlockAfter;
    hit.blockId = session.document().root().children().back()->id();
    hit.textNodeId = hit.blockId;
    controller.activateHit(hit);
  }
};
}  // namespace

// Table corruption guard: backspace on an empty paragraph after a table must NOT eat the
// table's closing pipes. Previously the empty paragraph merged into a table cell, truncating
// the source to "| 1 | 2" and breaking the table. The empty paragraph is removed and the caret
// lands in the last cell.
void testEmptyParagraphAfterTable() {
  Harness h;
  const QString table = QStringLiteral("| a | b |\n|---|---|\n| 1 | 2 |");
  h.load(table + QStringLiteral("\n\n"));
  h.placeInEmptyParagraph();

  require(h.controller.inputController().deleteBackward(), "backspace should be handled");
  require(h.session.markdownText() == table, "table source must be intact (no corruption)");
  require(rootHasOnlyBlockType(h.session, BlockType::Table), "empty paragraph after table should be removed");

  const CursorPosition c = h.controller.selection().cursorPosition();
  require(!c.afterBlock, "caret should leave the empty paragraph");
  MarkdownNode* block = h.session.document().node(c.blockId);
  require(block != nullptr && block->type() == BlockType::TableCell, "caret should land in the last table cell");
}

// Virtual trailing caret below a table retreats into the last cell (was a stuck no-op).
void testTrailingCaretAfterTable() {
  Harness h;
  const QString table = QStringLiteral("| a | b |\n|---|---|\n| 1 | 2 |");
  h.load(table);
  h.placeInTrailing();
  require(h.controller.selection().cursorPosition().afterBlock, "caret should start on the trailing paragraph");

  require(h.controller.inputController().deleteBackward(), "backspace should be handled");
  require(h.session.markdownText() == table, "trailing backspace must not change the table source");
  const CursorPosition c = h.controller.selection().cursorPosition();
  require(!c.afterBlock, "caret should leave the trailing paragraph");
  MarkdownNode* block = h.session.document().node(c.blockId);
  require(block != nullptr && block->type() == BlockType::TableCell, "caret should retreat into the last table cell");
}

// Empty paragraph after an HTML block: deleted, caret at the block's content end (was a no-op).
void testEmptyParagraphAfterHtmlBlock() {
  Harness h;
  const QString html = QStringLiteral("<div>\nhtml\n</div>");
  h.load(html + QStringLiteral("\n\n"));
  h.placeInEmptyParagraph();

  require(h.controller.inputController().deleteBackward(), "backspace should be handled");
  require(rootHasOnlyBlockType(h.session, BlockType::HtmlBlock), "empty paragraph after HTML block should be removed");
  require(h.session.markdownText().contains(html), "html source must be intact");
  const CursorPosition c = h.controller.selection().cursorPosition();
  require(!c.afterBlock, "caret should leave the empty paragraph");
  MarkdownNode* block = h.session.document().node(c.blockId);
  require(block != nullptr && block->type() == BlockType::HtmlBlock, "caret should retreat into the HTML block");
}

// Front matter's trailing-newline-sensitive parsing makes deleting a trailing empty paragraph
// unsafe (the closing fence needs its newline). Backspace there must be a safe no-op: document
// intact, caret valid — never corrupted or dropped.
void testEmptyParagraphAfterFrontMatterIsSafe() {
  Harness h;
  const QString fm = QStringLiteral("---\nkey: value\n---");
  h.load(fm + QStringLiteral("\n\n"));
  h.placeInEmptyParagraph();
  h.controller.inputController().deleteBackward();
  require(h.session.markdownText().contains(fm), "front matter must survive backspace");
  require(h.controller.selection().hasCursor(), "caret should remain valid after backspace near front matter");
}

// The previously-working cases must keep working (regression guard for the common blocks).
void testEmptyParagraphAfterCommonBlocks() {
  struct Case {
    const char* name;
    QString source;
  };
  const Case cases[] = {
    {"paragraph", QStringLiteral("alpha")},
    {"heading", QStringLiteral("## Title")},
    {"ordered-list", QStringLiteral("1. first\n2. second")},
    {"unordered-list", QStringLiteral("- first\n- second")},
    {"code-fence", QStringLiteral("```\ncode line\n```")},
    {"math-block", QStringLiteral("$$\nx^2\n$$")},
    {"block-quote", QStringLiteral("> quote line")},
  };
  for (const auto& k : cases) {
    Harness h;
    h.load(k.source + QStringLiteral("\n\n"));
    h.placeInEmptyParagraph();
    require(h.controller.inputController().deleteBackward(), "backspace should be handled");
    require(h.session.markdownText() == k.source, "empty paragraph after common block should be removed");
    require(h.controller.selection().cursorPosition().isValid(), "caret should remain valid");
  }
}

// Thematic break has no editable content, so backspace on a trailing empty paragraph is a
// safe no-op (document unchanged, caret valid) — it must never corrupt or drop the caret.
void testEmptyParagraphAfterThematicBreakIsSafe() {
  Harness h;
  const QString md = QStringLiteral("---\n\n");
  h.load(md);
  h.placeInEmptyParagraph();
  h.controller.inputController().deleteBackward();
  require(h.controller.selection().hasCursor(), "caret should remain valid after backspace near a thematic break");
  // Document must not be corrupted (the table bug class): still contains the thematic break.
  require(h.session.markdownText().contains(QStringLiteral("---")), "thematic break must survive backspace");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
  testEmptyParagraphAfterTable();
  testTrailingCaretAfterTable();
  testEmptyParagraphAfterHtmlBlock();
  testEmptyParagraphAfterFrontMatterIsSafe();
  testEmptyParagraphAfterCommonBlocks();
  testEmptyParagraphAfterThematicBreakIsSafe();
  QApplication::clipboard()->clear();
  return 0;
}
