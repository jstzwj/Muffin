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
  session.setMarkdownText(session.markdownText().toString(), true);
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
  // Rest the caret in a block's afterBlock virtual area (Zone::BlockAfter) — the caret target for
  // the space after a non-editable leaf like a thematic break, now that no VEP node is synthesized
  // there.
  void placeAfter(MarkdownNode* block) {
    HitTestResult hit;
    hit.zone = HitTestResult::Zone::BlockAfter;
    hit.blockId = block->id();
    hit.textNodeId = block->id();
    controller.activateHit(hit);
  }
  // Simulate clicking the rule's box — the hit the view produces for a click on a thematic break
  // (Zone::SelectBlock). activateHit turns it into a whole-block selection.
  void selectBlock(MarkdownNode* block) {
    HitTestResult hit;
    hit.zone = HitTestResult::Zone::SelectBlock;
    hit.blockId = block->id();
    hit.textNodeId = block->id();
    controller.activateHit(hit);
  }
};

// A thematic break carries no editable text. The caret rests in its afterBlock virtual area (no
// VEP node is synthesized after a rule). Backspace there must EAT the divider: the rule is removed
// and the caret retreats to the end of the preceding editable block. (Previously, with a VEP, the
// caret sat on that empty paragraph; now it sits afterBlock on the rule — same outcome via
// tryRemoveThematicBreak.)
void testBackspaceAfterRuleEatsIt() {
  Harness h;
  h.load(QStringLiteral("alpha\n\n---\n\n"));
  MarkdownNode* hr = findThematicBreak(&h.session.document().root());
  require(hr != nullptr, "document should contain a thematic break");
  h.placeAfter(hr);
  require(h.controller.selection().cursorPosition().afterBlock, "caret should start in the rule's afterBlock area");

  require(h.controller.inputController().deleteBackward(), "backspace should be handled");
  require(h.session.markdownText().toString() == QStringLiteral("alpha\n\n"), "divider should collapse to 'alpha\\n\\n'");

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
  require(h.session.markdownText().toString() == QStringLiteral("alpha\n\nbeta"), "rule removed, two paragraphs remain");

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
  require(h.session.markdownText().toString() == QStringLiteral("alpha\n\nbeta"), "rule removed, paragraphs must NOT merge");

  const CursorPosition c = h.controller.selection().cursorPosition();
  require(h.controller.selection().hasCursor(), "caret should remain valid");
  MarkdownNode* caretBlock = h.session.document().node(c.blockId);
  require(caretBlock != nullptr && caretBlock->type() == BlockType::Paragraph, "caret should sit on a paragraph");
  require(c.text.textOffset == 5, "caret should remain at the end of 'alpha'");

  require(!reparsedRootHasBlockType(h.session, BlockType::ThematicBreak), "thematic break should be removed");
}

// A rule as the leading (and only) block: the caret rests afterBlock on it. Backspace eats the
// divider (consistent with the mid-document afterBlock case) and leaves a clean empty document —
// the caret lands on the leading virtual paragraph, never corrupted or dropped. (Previously, with
// a VEP after the rule, the caret sat on that empty paragraph and backspace was a stuck no-op; the
// afterBlock caret routes the same keystroke through tryRemoveThematicBreak instead.)
void testBackspaceOnLeadingRuleRemovesIt() {
  Harness h;
  h.load(QStringLiteral("---\n\n"));
  MarkdownNode* hr = findThematicBreak(&h.session.document().root());
  require(hr != nullptr, "document should contain a thematic break");
  h.placeAfter(hr);
  require(h.controller.selection().cursorPosition().afterBlock, "caret should start in the rule's afterBlock area");

  require(h.controller.inputController().deleteBackward(), "backspace should be handled");
  require(h.session.markdownText().toString().isEmpty(), "leading rule should be removed, leaving an empty document");
  require(h.controller.selection().hasCursor(), "caret should remain valid on the leading virtual paragraph");
  require(!reparsedRootHasBlockType(h.session, BlockType::ThematicBreak), "thematic break should be gone");
}

// A rule sitting between a list and a paragraph: backspace from the paragraph eats the divider
// and the list survives intact (its marker/content untouched).
void testBackspaceAfterRulePreservesPrecedingList() {
  Harness h;
  h.load(QStringLiteral("- item\n\n---\n\nbeta"));
  MarkdownNode* beta = findLastParagraph(&h.session.document().root());
  setCursor(h.controller.selection(), beta, 0);

  require(h.controller.inputController().deleteBackward(), "backspace should be handled");
  require(h.session.markdownText().toString() == QStringLiteral("- item\n\nbeta"), "rule removed, list preserved");
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
  require(h.session.markdownText().toString() == QStringLiteral("alpha\n\n---\n\nbeta"), "undo should restore the source");
  require(reparsedRootHasBlockType(h.session, BlockType::ThematicBreak), "undo should restore the rule");

  // Forward delete + undo.
  h.load(QStringLiteral("alpha\n\n---\n\nbeta"));
  MarkdownNode* alpha = findFirstParagraph(&h.session.document().root());
  setCursor(h.controller.selection(), alpha, 5);
  require(h.controller.inputController().deleteForward(), "delete should be handled");
  h.controller.undo();
  require(h.session.markdownText().toString() == QStringLiteral("alpha\n\n---\n\nbeta"), "undo should restore the source after delete");
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

// Typing from the afterBlock caret on a MID-DOCUMENT rule (content follows it) must insert the text
// as a SEPARATE paragraph between the rule and the following block — never merged into it. The
// afterBlock path (insertBlockAfterCurrentBlock) was built for the document-trailing case; without
// a trailing separator the typed text used to fuse with the next block ("hibeta").
void testTypeAfterMidDocRuleStaysSeparate() {
  Harness h;
  h.load(QStringLiteral("alpha\n\n---\n\nbeta"));
  MarkdownNode* hr = findThematicBreak(&h.session.document().root());
  require(hr != nullptr, "document should contain a thematic break");
  h.placeAfter(hr);
  require(h.controller.selection().cursorPosition().afterBlock, "caret should start afterBlock on the rule");

  require(h.controller.inputController().insertText(QStringLiteral("hi")), "typing should be handled");
  const QString src = h.session.markdownText().toString();
  require(!src.contains(QStringLiteral("hibeta")), "typed text must NOT merge into the following block; got: " + src);
  require(src.contains(QStringLiteral("hi")), "typed text should be present; got: " + src);
  require(reparsedRootHasBlockType(h.session, BlockType::ThematicBreak), "rule must survive typing after it");
  require(h.controller.selection().hasCursor(), "caret should remain valid");
}

// Enter from the afterBlock caret on a rule widens the trailing gap and keeps the caret afterBlock
// on the rule (mirroring the document-trailing caret: blank lines alone create no paragraph in
// Markdown — only typed text does). The caret must stay valid across repeated Enter, and a
// subsequent typed character must form its OWN paragraph after the rule, never merging into the
// following block.
void testEnterAfterRuleThenRepeat() {
  Harness h;
  h.load(QStringLiteral("alpha\n\n---\n\nbeta"));
  MarkdownNode* hr = findThematicBreak(&h.session.document().root());
  require(hr != nullptr, "document should contain a thematic break");
  h.placeAfter(hr);

  for (int i = 0; i < 3; ++i) {
    require(h.controller.inputController().insertParagraphBreak(), "enter should be handled");
    require(h.controller.selection().hasCursor(), "caret should remain valid after each enter");
  }
  // Typing after the Enters must land in a real, separate paragraph after the rule. Do this BEFORE
  // any full-reparse assertion: reparsedRootHasBlockType rebuilds the tree with fresh node ids,
  // which would stale the caret's blockId and falsely make the typed keystroke unhandled.
  require(h.controller.inputController().insertText(QStringLiteral("z")), "typing should be handled");
  const QString src = h.session.markdownText().toString();
  require(!src.contains(QStringLiteral("zbeta")) && src.contains(QStringLiteral("z")),
          "typed text should form its own paragraph after the rule; got: " + src);
  require(reparsedRootHasBlockType(h.session, BlockType::ThematicBreak), "rule must survive repeated enter");
}

// Typing from the afterBlock caret on a TRAILING rule (the document's last block) keeps working:
// the text becomes a new paragraph after the rule (the regression guard that already existed for
// the trailing case, now reached via the afterBlock caret instead of a VEP).
void testTypeAfterTrailingRule() {
  Harness h;
  h.load(QStringLiteral("---"));
  MarkdownNode* hr = findThematicBreak(&h.session.document().root());
  require(hr != nullptr, "document should contain a thematic break");
  h.placeAfter(hr);
  require(h.controller.inputController().insertText(QStringLiteral("123")), "typing should be handled");
  require(h.session.markdownText().toString() == QStringLiteral("---\n\n123"), "trailing rule + '123' -> '---\\n\\n123'");
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
  require(h.session.markdownText().toString() == QStringLiteral("alpha\n\nbeta"), "rule should be removed");
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
  require(h.session.markdownText().toString() == QStringLiteral("alpha\n\nbeta"), "rule should be removed");
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
  require(h.session.markdownText().toString() == QStringLiteral("alpha"), "rule should be removed, leaving 'alpha'");
  require(!reparsedRootHasBlockType(h.session, BlockType::ThematicBreak), "thematic break should be gone");
  require(h.controller.selection().hasCursor(), "caret should remain valid");
}

// Delete at the END of editable content nested in a CONTAINER that immediately precedes a rule
// eats the divider. The caret's block is the block quote's inner paragraph; its tree siblings live
// INSIDE the quote, so the old nextSibling()-based lookup never reached the top-level rule and the
// gesture was a silent no-op. This is the user's real document: "> note\n\n---\n\n## Heading".
void testDeleteFromBlockBeforeRuleAcrossContainer() {
  Harness h;
  h.load(QStringLiteral("> quote\n\n---\n\n## H"));
  MarkdownNode* quote = findFirstParagraph(&h.session.document().root());
  setCursor(h.controller.selection(), quote, 5);

  require(h.controller.inputController().deleteForward(), "delete should be handled");
  require(h.session.markdownText().toString() == QStringLiteral("> quote\n\n## H"),
          "rule removed, block quote + heading survive; got: " + h.session.markdownText().toString());
  require(!reparsedRootHasBlockType(h.session, BlockType::ThematicBreak), "thematic break should be gone");
  require(reparsedRootHasBlockType(h.session, BlockType::BlockQuote), "block quote must survive");
  require(reparsedRootHasBlockType(h.session, BlockType::Heading), "heading must survive");
  require(h.controller.selection().hasCursor(), "caret should remain valid");
}

// Symmetric: Delete at the end of the LAST list item immediately preceding a rule eats the divider.
// The old code only climbed one ListItem level, so a rule that is a sibling of the whole List (the
// last item has no next sibling) was unreachable — another silent no-op.
void testDeleteFromLastListItemBeforeRule() {
  Harness h;
  h.load(QStringLiteral("- item\n\n---"));
  MarkdownNode* item = findFirstParagraph(&h.session.document().root());
  setCursor(h.controller.selection(), item, 4);

  require(h.controller.inputController().deleteForward(), "delete should be handled");
  require(h.session.markdownText().toString() == QStringLiteral("- item"),
          "rule removed, list item survives; got: " + h.session.markdownText().toString());
  require(!reparsedRootHasBlockType(h.session, BlockType::ThematicBreak), "thematic break should be gone");
  require(reparsedRootHasBlockType(h.session, BlockType::List), "list must survive");
}

// Delete at the end of a paragraph nested in a CONTAINER merges with the following editable block —
// the same thing a top-level caret already does (e.g. "alpha\n\n> quote" + Del → "alphaquote"). The
// container-blind nextEditableTextBlock used to make this a silent no-op; now it climbs out and the
// nested caret behaves like a top-level one. "> quote\n\nbeta" + Del (end of quote) → "> quotebeta".
void testDeleteMergesParagraphOutOfBlockquote() {
  Harness h;
  h.load(QStringLiteral("> quote\n\nbeta"));
  MarkdownNode* quote = findFirstParagraph(&h.session.document().root());
  setCursor(h.controller.selection(), quote, 5);

  require(h.controller.inputController().deleteForward(), "delete should be handled");
  require(h.session.markdownText().toString() == QStringLiteral("> quotebeta"),
          "quote should merge into the following paragraph; got: " + h.session.markdownText().toString());
  require(h.controller.selection().hasCursor(), "caret should remain valid");
}

// Same climb, but the following block is a LIST — consistent with a top-level caret merging into a
// list ("alpha\n\n- item" + Del → "alphaitem"). "> quote\n\n- item" + Del → "> quoteitem".
void testDeleteMergesParagraphOutOfBlockquoteIntoList() {
  Harness h;
  h.load(QStringLiteral("> quote\n\n- item"));
  MarkdownNode* quote = findFirstParagraph(&h.session.document().root());
  setCursor(h.controller.selection(), quote, 5);

  require(h.controller.inputController().deleteForward(), "delete should be handled");
  require(h.session.markdownText().toString() == QStringLiteral("> quoteitem"),
          "quote should merge into the list's first item; got: " + h.session.markdownText().toString());
}

// Deleting a top-level `---` from a caret nested DEEP inside an adjacent list must leave the LIVE
// tree correct — not just the source text. The edit window spans the list (editable) and the
// divider (non-editable); the slice picker used to slice only the list and silently drop the
// divider, leaving a stale ThematicBreak node plus duplicated Heading/List nodes (stale ranges → the
// rendered heading read as garbled "1 | 例1…" and list items lost their text). Now the slice picker
// refuses (full re-parse) whenever an edit strictly overlaps a non-editable top-level block.
void testDeleteRuleFromNestedListItemKeepsLiveTreeConsistent() {
  Harness h;
  h.load(QStringLiteral(
      "text1\n\n---\n\n## Heading One\n\n- outer\n  - inner content here\n\n---\n\n## Heading Two\n\n- o2\n  - i2"));
  MarkdownNode* list = blockAt(h.session, 3);
  MarkdownNode* outerItem = childAt(list, 0);
  MarkdownNode* nestedList = childAt(outerItem, 1);
  MarkdownNode* innerItem = childAt(nestedList, 0);
  require(innerItem != nullptr && innerItem->type() == BlockType::ListItem, "found nested list item");
  setCursor(h.controller.selection(), innerItem, 18);  // end of "inner content here"

  require(h.controller.inputController().deleteForward(), "delete should be handled");
  require(!h.session.markdownText().toString().contains(QStringLiteral("---\n\n## Heading Two")),
          "the second divider should be gone from the source; got: " + h.session.markdownText().toString());

  // Capture the LIVE tree's top-level signature, then compare it against a fresh reparse of the
  // (correct) source. A corrupt incremental update diverges here even though the source is right.
  auto signature = [](DocumentSession& s) {
    QString out;
    for (const auto& c : s.document().root().children()) {
      const SourceRange r = c->sourceRange();
      out += QString::number(static_cast<int>(c->type())) + QStringLiteral(":") +
             QString::number(r.byteStart) + QStringLiteral("-") + QString::number(r.byteEnd) +
             QStringLiteral(" ");
    }
    return out.trimmed();
  };
  const QString live = signature(h.session);
  h.session.setMarkdownText(h.session.markdownText().toString(), true);  // full re-parse = ground truth
  const QString truth = signature(h.session);
  require(live == truth,
          "live tree must match a fresh re-parse (no stale/duplicate nodes); live={" + live +
              "} truth={" + truth + "}");
}

// Undo must restore a rule removed while the caret rested on it.
void testUndoRestoresRuleRemovedFromOnIt() {
  Harness h;
  h.load(QStringLiteral("alpha\n\n---\n\nbeta"));
  MarkdownNode* hr = findThematicBreak(&h.session.document().root());
  setCursor(h.controller.selection(), hr, 0);
  require(h.controller.inputController().deleteForward(), "delete on rule should be handled");
  h.controller.undo();
  require(h.session.markdownText().toString() == QStringLiteral("alpha\n\n---\n\nbeta"), "undo should restore the source");
  require(reparsedRootHasBlockType(h.session, BlockType::ThematicBreak), "undo should restore the rule");
}

// Clicking a thematic break SELECTS the whole block (Typora-style): the selection becomes a non-
// collapsed range spanning the rule. From that state Backspace, Forward-Delete AND Enter each
// remove it cleanly (no leftover blank lines, neighbours stay distinct).
void testSelectRuleThenBackspaceRemovesIt() {
  Harness h;
  h.load(QStringLiteral("alpha\n\n---\n\nbeta"));
  MarkdownNode* hr = findThematicBreak(&h.session.document().root());
  require(hr != nullptr, "document should contain a thematic break");
  h.selectBlock(hr);
  const SelectionRange sel = h.controller.selection().selection();
  require(!sel.isCollapsed(), "clicking the rule should select the whole block");
  require(sel.anchor.blockId == hr->id() && sel.focus.blockId == hr->id(), "selection should be on the rule");

  require(h.controller.inputController().deleteBackward(), "backspace should be handled");
  require(h.session.markdownText().toString() == QStringLiteral("alpha\n\nbeta"), "rule should be removed cleanly");
  require(!reparsedRootHasBlockType(h.session, BlockType::ThematicBreak), "thematic break should be gone");
  require(h.controller.selection().hasCursor(), "caret should remain valid");
}

void testSelectRuleThenDeleteRemovesIt() {
  Harness h;
  h.load(QStringLiteral("alpha\n\n---\n\nbeta"));
  MarkdownNode* hr = findThematicBreak(&h.session.document().root());
  h.selectBlock(hr);
  require(!h.controller.selection().selection().isCollapsed(), "rule should be selected");

  require(h.controller.inputController().deleteForward(), "delete should be handled");
  require(h.session.markdownText().toString() == QStringLiteral("alpha\n\nbeta"), "rule should be removed cleanly");
  require(!reparsedRootHasBlockType(h.session, BlockType::ThematicBreak), "thematic break should be gone");
}

void testSelectRuleThenEnterRemovesIt() {
  Harness h;
  h.load(QStringLiteral("alpha\n\n---\n\nbeta"));
  MarkdownNode* hr = findThematicBreak(&h.session.document().root());
  h.selectBlock(hr);
  require(!h.controller.selection().selection().isCollapsed(), "rule should be selected");

  require(h.controller.inputController().insertParagraphBreak(), "enter should be handled");
  require(h.session.markdownText().toString() == QStringLiteral("alpha\n\nbeta"), "selected rule should be removed on enter");
  require(!reparsedRootHasBlockType(h.session, BlockType::ThematicBreak), "thematic break should be gone");
}

// Guard: the caret resting ON a virtual empty paragraph in the blank gap below a rule (a real click
// target, not the afterBlock area) must keep a valid caret — and a valid view cursorHit (what
// paintInsertionCursor gates on) — across repeated Enter, in mid-doc / trailing / leading-rule / lazy
// (far-below-viewport) configurations. A sibling regression (3rd-dash → afterBlock → Enter) is
// covered by testEnterAfterThirdDashConversionKeepsCaret.
void testEnterOnEmptyParagraphBelowRuleKeepsCaret() {
  struct Case { const char* name; const char* md; int prependParagraphs = 0; };
  const Case cases[] = {
    {"mid-doc 2-blank", "alpha\n\n---\n\n\n\nbeta"},
    {"mid-doc 3-blank", "alpha\n\n---\n\n\n\n\nbeta"},
    {"trailing 2-blank", "alpha\n\n---\n\n\n"},
    {"trailing 3-blank", "alpha\n\n---\n\n\n\n"},
    {"leading-rule trailing", "---\n\n\n"},
    {"far-below-viewport (lazy)", "alpha\n\n---\n\n\n\nbeta", 120},
  };
  for (const Case& tc : cases) {
    Harness h;
    QString md = QString::fromLatin1(tc.md);
    if (tc.prependParagraphs > 0) {
      // Push the rule + its VEP far below the viewport so the VEP's layout slot starts in the
      // lazy/estimated state — the real-app condition the tiny-doc harness misses.
      QString prefix;
      for (int i = 0; i < tc.prependParagraphs; ++i) {
        prefix += QStringLiteral("para %1\n\n").arg(i);
      }
      md = prefix + md;
    }
    h.load(md);
    MarkdownNode* hr = findThematicBreak(&h.session.document().root());
    require(hr != nullptr, QString::fromLatin1(tc.name) + ": document should contain a thematic break");
    // The VEP is the paragraph immediately following the rule at the top level.
    MarkdownNode* vep = nullptr;
    const auto& kids = h.session.document().root().children();
    for (qsizetype i = 0; i + 1 < static_cast<qsizetype>(kids.size()); ++i) {
      if (kids.at(static_cast<size_t>(i)).get() == hr && kids.at(i + 1)->type() == BlockType::Paragraph) {
        vep = kids.at(i + 1).get();
        break;
      }
    }
    require(vep != nullptr, QString::fromLatin1(tc.name) + ": a virtual empty paragraph should sit below the rule");
    setCursor(h.controller.selection(), vep, 0);

    // Repeated Enter on the resulting empty paragraphs — the user's "press Enter, caret disappears"
    // flow. Both the caret AND the view's resolved cursorHit must stay valid on every iteration (the
    // view's hit is what paintInsertionCursor gates on — if it goes invalid the caret stops painting).
    for (int iter = 0; iter < 4; ++iter) {
      require(h.controller.inputController().insertParagraphBreak(),
              QString::fromLatin1(tc.name) + ": enter #" + QString::number(iter) + " should be handled");
      // Flush deferred timers / animations the real event loop would run between keystrokes. If an
      // async op invalidates the view's caret-hit after Enter, this surfaces it (the headless harness
      // otherwise resolves the hit synchronously and masks the disappearance).
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
      const CursorPosition c = h.controller.selection().cursorPosition();
      require(c.blockId.isValid(),
              QString::fromLatin1(tc.name) + ": caret #" + QString::number(iter) + " was cleared");
      require(h.view.cursorHit().isValid(),
              QString::fromLatin1(tc.name) + ": view.cursorHit #" + QString::number(iter) + " went invalid");
    }
  }
}

// The user's report: type `--` then `-` (the 3rd dash) → the paragraph converts to a thematic break,
// and the caret lands in the rule's afterBlock area (between the rule and the next paragraph). Pressing
// Enter from THAT state lost the caret: insertBlockAfterCurrentBlock set caretOffset past the inserted
// "\n\n", which landed in a blank gap no block covers, so the caret fell back to END-OF-DOCUMENT (the
// end of the following paragraph) and looked like it vanished. Now it lands on the new empty paragraph.
void testEnterAfterThirdDashConversionKeepsCaret() {
  struct Case { const char* name; const char* md; };
  const Case cases[] = {
    {"-- then beta", "--\n\nbeta"},
    {"-- trailing", "--"},
    {"-- then blank then beta", "--\n\n\nbeta"},
  };
  for (const Case& tc : cases) {
    Harness h;
    h.load(QString::fromLatin1(tc.md));
    MarkdownNode* dashPara = findFirstParagraph(&h.session.document().root());
    require(dashPara != nullptr, QString::fromLatin1(tc.name) + ": should start with a '--' paragraph");
    setCursor(h.controller.selection(), dashPara, 2);  // caret at end of "--"

    // Type the 3rd dash: "--" + "-" -> "---" -> a thematic break; the caret lands afterBlock on the rule.
    require(h.controller.inputController().insertText(QStringLiteral("-")),
            QString::fromLatin1(tc.name) + ": typing the 3rd dash should be handled");
    require(findThematicBreak(&h.session.document().root()) != nullptr,
            QString::fromLatin1(tc.name) + ": the 3rd dash should create a rule");

    // Enter from that afterBlock caret (the user's "Enter loses focus" step).
    require(h.controller.inputController().insertParagraphBreak(),
            QString::fromLatin1(tc.name) + ": enter should be handled");
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    const CursorPosition afterEnter = h.controller.selection().cursorPosition();
    require(afterEnter.blockId.isValid(),
            QString::fromLatin1(tc.name) + ": caret should remain valid after Enter (not cleared)");
    require(h.view.cursorHit().isValid(),
            QString::fromLatin1(tc.name) + ": view.cursorHit should remain valid after Enter");
    // For a rule followed by a paragraph, the caret must land on the NEW empty paragraph in the gap
    // (a VEP just below the rule) — NOT fall back to end-of-document (end of the following paragraph).
    if (QString::fromLatin1(tc.md).contains(QStringLiteral("beta"))) {
      require(afterEnter.text.sourceOffset < h.session.markdownText().toString().indexOf(QStringLiteral("beta")),
              QString::fromLatin1(tc.name) + ": caret should land in the gap below the rule, not at end-of-doc");
    }
  }
}

// Guard for the structureEdit=false change in insertBlockAfterCurrentBlock: typing after a non-text
// block is now a plain TextDeltaCommand edit (not the O(document) snapshot-undo path that
// structureEdit=true forced). Undo must restore the source exactly and leave the rule intact + caret
// valid — for both a mid-doc and a trailing rule. If TextDeltaCommand mishandles the "\n\n…" insert
// that creates a new top-level paragraph, this catches it.
void testUndoTypeAfterBlock() {
  {
    Harness h;
    h.load(QStringLiteral("alpha\n\n---\n\nbeta"));
    MarkdownNode* hr = findThematicBreak(&h.session.document().root());
    require(hr != nullptr, "document should contain a thematic break");
    h.placeAfter(hr);
    require(h.controller.inputController().insertText(QStringLiteral("hi")), "typing after the rule should be handled");
    const QString after = h.session.markdownText().toString();
    require(after.contains(QStringLiteral("hi")) && !after.contains(QStringLiteral("hibeta")),
            "typed text should form its own paragraph; got: " + after);

    h.controller.undo();
    require(h.session.markdownText().toString() == QStringLiteral("alpha\n\n---\n\nbeta"),
            "undo should restore the source after type-after-block");
    require(reparsedRootHasBlockType(h.session, BlockType::ThematicBreak), "rule should survive undo");
    require(h.controller.selection().hasCursor(), "caret should remain valid after undo");
  }
  {
    Harness h;
    h.load(QStringLiteral("---"));
    MarkdownNode* hr = findThematicBreak(&h.session.document().root());
    require(hr != nullptr, "document should contain a thematic break");
    h.placeAfter(hr);
    require(h.controller.inputController().insertText(QStringLiteral("123")), "typing after trailing rule should be handled");
    require(h.session.markdownText().toString() == QStringLiteral("---\n\n123"), "trailing rule + '123' -> '---\\n\\n123'");

    h.controller.undo();
    require(h.session.markdownText().toString() == QStringLiteral("---"), "undo should restore the trailing rule");
    require(reparsedRootHasBlockType(h.session, BlockType::ThematicBreak), "rule should survive undo");
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
  testBackspaceAfterRuleEatsIt();
  testBackspaceStartOfParagraphAfterRuleEatsIt();
  testDeleteEndOfParagraphBeforeRuleEatsIt();
  testBackspaceOnLeadingRuleRemovesIt();
  testBackspaceAfterRulePreservesPrecedingList();
  testUndoRestoresRule();
  testEnterBeforeRuleInsertsParagraph();
  testEnterAfterRuleThenRepeat();
  testTypeAfterMidDocRuleStaysSeparate();
  testTypeAfterTrailingRule();
  testUndoTypeAfterBlock();
  testDeleteOnRuleRemovesIt();
  testBackspaceOnRuleRemovesIt();
  testTrailingCaretBelowRuleEatsIt();
  testUndoRestoresRuleRemovedFromOnIt();
  testDeleteFromBlockBeforeRuleAcrossContainer();
  testDeleteFromLastListItemBeforeRule();
  testDeleteMergesParagraphOutOfBlockquote();
  testDeleteMergesParagraphOutOfBlockquoteIntoList();
  testDeleteRuleFromNestedListItemKeepsLiveTreeConsistent();
  testSelectRuleThenBackspaceRemovesIt();
  testSelectRuleThenDeleteRemovesIt();
  testSelectRuleThenEnterRemovesIt();
  testEnterOnEmptyParagraphBelowRuleKeepsCaret();
  testEnterAfterThirdDashConversionKeepsCaret();
  return 0;
}
