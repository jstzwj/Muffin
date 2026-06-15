#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/SelectionController.h"

#include "EditorTestUtils.h"

#include <QApplication>

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

MarkdownNode* findFirstParagraph(MarkdownNode* node) {
  if (node && node->type() == BlockType::Paragraph) return node;
  for (const auto& c : node->children()) {
    if (MarkdownNode* f = findFirstParagraph(c.get())) return f;
  }
  return nullptr;
}

MarkdownNode* findLastParagraph(MarkdownNode* node) {
  MarkdownNode* found = nullptr;
  if (node && node->type() == BlockType::Paragraph) found = node;
  for (const auto& c : node->children()) {
    if (MarkdownNode* f = findLastParagraph(c.get())) found = f;
  }
  return found;
}

MarkdownNode* findThematicBreak(MarkdownNode* node) {
  if (node && node->type() == BlockType::ThematicBreak) return node;
  for (const auto& c : node->children()) {
    if (MarkdownNode* f = findThematicBreak(c.get())) return f;
  }
  return nullptr;
}

// The markdown source is the source of truth. A mid-document thematic-break removal is a
// structure edit; the live (incremental) tree is only re-synced when the brush queue refreshes
// the top-level range (which the real view drives, but a headless test harness does not). So we
// reparse the resulting source to assert on the tree — this also proves the edit produced valid,
// well-structured markdown rather than a corrupted fragment.
bool reparsedRootHasBlockType(DocumentSession& session, BlockType type) {
  session.setMarkdownText(session.markdownText(), true);
  for (const auto& c : session.document().root().children()) {
    if (c->type() == type) return true;
  }
  return false;
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
};

// A thematic break carries no editable text, so the generic paragraph-merge path (which only
// looks at editable-text siblings) silently no-ops next to one. Backspace from the empty
// paragraph after a rule must instead EAT the divider: the rule is removed and the caret
// retreats to the end of the preceding editable block. (Previously a stuck no-op.)
void testBackspaceEmptyParagraphAfterRuleEatsIt() {
  Harness h;
  h.load(QStringLiteral("alpha\n\n---\n\n"));
  h.placeInEmptyParagraph();

  require(h.controller.inputController().deleteBackward(), "backspace should be handled");
  require(h.session.markdownText() == QStringLiteral("alpha\n\n"), "divider + empty paragraph collapse to 'alpha\\n\\n'");

  const CursorPosition c = h.controller.selection().cursorPosition();
  require(h.controller.selection().hasCursor(), "caret should remain valid");
  MarkdownNode* block = h.session.document().node(c.blockId);
  require(block != nullptr && block->type() == BlockType::Paragraph, "caret should retreat to the preceding paragraph");
  require(block != nullptr && c.text.textOffset == 5, "caret should sit at the end of 'alpha'");

  require(!reparsedRootHasBlockType(h.session, BlockType::ThematicBreak), "thematic break should be removed");
}

// Backspace at the start of a NON-empty paragraph that follows a rule eats the divider; the
// paragraph survives and moves up next to the preceding block, caret at its start.
void testBackspaceStartOfParagraphAfterRuleEatsIt() {
  Harness h;
  h.load(QStringLiteral("alpha\n\n---\n\nbeta"));
  MarkdownNode* beta = findLastParagraph(&h.session.document().root());
  setCursor(h.controller.selection(), beta, 0);

  require(h.controller.inputController().deleteBackward(), "backspace should be handled");
  require(h.session.markdownText() == QStringLiteral("alpha\n\nbeta"), "rule removed, two paragraphs remain");

  const CursorPosition c = h.controller.selection().cursorPosition();
  require(h.controller.selection().hasCursor(), "caret should remain valid");
  MarkdownNode* caretBlock = h.session.document().node(c.blockId);
  require(caretBlock != nullptr && caretBlock->type() == BlockType::Paragraph, "caret should sit on a paragraph");
  require(c.text.textOffset == 0, "caret should be at the start of the moved paragraph");

  require(!reparsedRootHasBlockType(h.session, BlockType::ThematicBreak), "thematic break should be removed");
}

// Delete at the END of a paragraph that precedes a rule eats the divider — the symmetric inverse
// of the backspace case. The two paragraphs stay distinct (the separator after the rule is kept).
void testDeleteEndOfParagraphBeforeRuleEatsIt() {
  Harness h;
  h.load(QStringLiteral("alpha\n\n---\n\nbeta"));
  MarkdownNode* alpha = findFirstParagraph(&h.session.document().root());
  setCursor(h.controller.selection(), alpha, 5);

  require(h.controller.inputController().deleteForward(), "delete should be handled");
  require(h.session.markdownText() == QStringLiteral("alpha\n\nbeta"), "rule removed, paragraphs must NOT merge");

  const CursorPosition c = h.controller.selection().cursorPosition();
  require(h.controller.selection().hasCursor(), "caret should remain valid");
  MarkdownNode* caretBlock = h.session.document().node(c.blockId);
  require(caretBlock != nullptr && caretBlock->type() == BlockType::Paragraph, "caret should sit on a paragraph");
  require(c.text.textOffset == 5, "caret should remain at the end of 'alpha'");

  require(!reparsedRootHasBlockType(h.session, BlockType::ThematicBreak), "thematic break should be removed");
}

// A rule as the leading block (nothing editable before it) followed by an empty paragraph:
// backspace cannot eat the divider (there is nowhere to retreat the caret). It must remain a
// safe no-op — document intact, caret valid, never corrupted or dropped.
void testLeadingRuleBackspaceIsSafe() {
  Harness h;
  h.load(QStringLiteral("---\n\n"));
  h.placeInEmptyParagraph();
  h.controller.inputController().deleteBackward();
  require(h.session.markdownText().contains(QStringLiteral("---")), "leading rule must survive backspace");
  require(h.controller.selection().hasCursor(), "caret should remain valid");
}

// A rule sitting between a list and a paragraph: backspace from the paragraph eats the divider
// and the list survives intact (its marker/content untouched).
void testBackspaceAfterRulePreservesPrecedingList() {
  Harness h;
  h.load(QStringLiteral("- item\n\n---\n\nbeta"));
  MarkdownNode* beta = findLastParagraph(&h.session.document().root());
  setCursor(h.controller.selection(), beta, 0);

  require(h.controller.inputController().deleteBackward(), "backspace should be handled");
  require(h.session.markdownText() == QStringLiteral("- item\n\nbeta"), "rule removed, list preserved");
  require(reparsedRootHasBlockType(h.session, BlockType::List), "list must survive");
  require(!reparsedRootHasBlockType(h.session, BlockType::ThematicBreak), "thematic break should be removed");
}

// Undo must restore the divider after either gesture.
void testUndoRestoresRule() {
  Harness h;
  h.load(QStringLiteral("alpha\n\n---\n\nbeta"));
  MarkdownNode* beta = findLastParagraph(&h.session.document().root());
  setCursor(h.controller.selection(), beta, 0);
  require(h.controller.inputController().deleteBackward(), "backspace should be handled");
  h.controller.undo();
  require(h.session.markdownText() == QStringLiteral("alpha\n\n---\n\nbeta"), "undo should restore the source");
  require(reparsedRootHasBlockType(h.session, BlockType::ThematicBreak), "undo should restore the rule");

  // Forward delete + undo.
  h.load(QStringLiteral("alpha\n\n---\n\nbeta"));
  MarkdownNode* alpha = findFirstParagraph(&h.session.document().root());
  setCursor(h.controller.selection(), alpha, 5);
  require(h.controller.inputController().deleteForward(), "delete should be handled");
  h.controller.undo();
  require(h.session.markdownText() == QStringLiteral("alpha\n\n---\n\nbeta"), "undo should restore the source after delete");
  require(reparsedRootHasBlockType(h.session, BlockType::ThematicBreak), "undo should restore the rule after delete");
}

// Enter before a thematic break must keep working (regression guard): it inserts a new empty
// paragraph between the paragraph and the rule, with the caret inside it.
void testEnterBeforeRuleInsertsParagraph() {
  Harness h;
  h.load(QStringLiteral("alpha\n\n---\n\n"));
  MarkdownNode* alpha = findFirstParagraph(&h.session.document().root());
  setCursor(h.controller.selection(), alpha, 5);

  require(h.controller.inputController().insertParagraphBreak(), "enter should be handled");
  require(reparsedRootHasBlockType(h.session, BlockType::ThematicBreak), "rule must survive enter");
  const auto& kids = h.session.document().root().children();
  require(kids.size() >= 3, "a new empty paragraph should appear before the rule");
  require(kids.at(0)->type() == BlockType::Paragraph && kids.at(1)->type() == BlockType::Paragraph,
          "alpha should be followed by the new empty paragraph");
  require(h.controller.selection().hasCursor(), "caret should land in the new empty paragraph");
}

// The caret can rest ON the thematic break itself — arrow-key navigation lands there because
// selectableBlockByDirection does not skip non-editable blocks. From there BOTH Delete and
// Backspace must remove the divider (previously a silent no-op: editParagraph rejects a
// non-editable block). Delete advances the caret to the start of the following paragraph.
void testDeleteOnRuleRemovesIt() {
  Harness h;
  h.load(QStringLiteral("alpha\n\n---\n\nbeta"));
  MarkdownNode* hr = findThematicBreak(&h.session.document().root());
  require(hr != nullptr, "document should contain a thematic break");
  setCursor(h.controller.selection(), hr, 0);

  require(h.controller.inputController().deleteForward(), "delete on rule should be handled");
  require(h.session.markdownText() == QStringLiteral("alpha\n\nbeta"), "rule should be removed");
  require(!reparsedRootHasBlockType(h.session, BlockType::ThematicBreak), "thematic break should be gone");
  const CursorPosition c = h.controller.selection().cursorPosition();
  require(h.controller.selection().hasCursor(), "caret should remain valid");
  require(c.text.textOffset == 0, "delete should advance the caret to the next paragraph's start");
}

// Backspace from a caret resting on the rule retreats to the preceding paragraph's end.
void testBackspaceOnRuleRemovesIt() {
  Harness h;
  h.load(QStringLiteral("alpha\n\n---\n\nbeta"));
  MarkdownNode* hr = findThematicBreak(&h.session.document().root());
  setCursor(h.controller.selection(), hr, 0);

  require(h.controller.inputController().deleteBackward(), "backspace on rule should be handled");
  require(h.session.markdownText() == QStringLiteral("alpha\n\nbeta"), "rule should be removed");
  require(!reparsedRootHasBlockType(h.session, BlockType::ThematicBreak), "thematic break should be gone");
  const CursorPosition c = h.controller.selection().cursorPosition();
  require(h.controller.selection().hasCursor(), "caret should remain valid");
  require(c.text.textOffset == 5, "backspace should retreat the caret to the previous paragraph's end");
}

// A rule that is the LAST block: the virtual trailing caret below it (afterBlock) has the rule as
// its block. Backspace there must eat the divider (previously it only collapsed the caret, leaving
// the rule — and a document whose last block is a non-editable break has nowhere else to go).
void testTrailingCaretBelowRuleEatsIt() {
  Harness h;
  h.load(QStringLiteral("alpha\n\n---"));
  MarkdownNode* hr = findThematicBreak(&h.session.document().root());
  require(hr != nullptr, "document should contain a thematic break");
  HitTestResult hit;
  hit.zone = HitTestResult::Zone::BlockAfter;
  hit.blockId = hr->id();
  hit.textNodeId = hr->id();
  h.controller.activateHit(hit);
  require(h.controller.selection().cursorPosition().afterBlock, "caret should start in the trailing area");

  require(h.controller.inputController().deleteBackward(), "backspace should be handled");
  require(h.session.markdownText() == QStringLiteral("alpha"), "rule should be removed, leaving 'alpha'");
  require(!reparsedRootHasBlockType(h.session, BlockType::ThematicBreak), "thematic break should be gone");
  require(h.controller.selection().hasCursor(), "caret should remain valid");
}

// Undo must restore a rule removed while the caret rested on it.
void testUndoRestoresRuleRemovedFromOnIt() {
  Harness h;
  h.load(QStringLiteral("alpha\n\n---\n\nbeta"));
  MarkdownNode* hr = findThematicBreak(&h.session.document().root());
  setCursor(h.controller.selection(), hr, 0);
  require(h.controller.inputController().deleteForward(), "delete on rule should be handled");
  h.controller.undo();
  require(h.session.markdownText() == QStringLiteral("alpha\n\n---\n\nbeta"), "undo should restore the source");
  require(reparsedRootHasBlockType(h.session, BlockType::ThematicBreak), "undo should restore the rule");
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
  testBackspaceEmptyParagraphAfterRuleEatsIt();
  testBackspaceStartOfParagraphAfterRuleEatsIt();
  testDeleteEndOfParagraphBeforeRuleEatsIt();
  testLeadingRuleBackspaceIsSafe();
  testBackspaceAfterRulePreservesPrecedingList();
  testUndoRestoresRule();
  testEnterBeforeRuleInsertsParagraph();
  testDeleteOnRuleRemovesIt();
  testBackspaceOnRuleRemovesIt();
  testTrailingCaretBelowRuleEatsIt();
  testUndoRestoresRuleRemovedFromOnIt();
  return 0;
}
