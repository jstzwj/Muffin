// `[TOC]` rendering: a paragraph containing the literal marker "[TOC]" renders, while
// the caret is elsewhere, as a generated indented link list of the document's headings.
// Ctrl+clicking an entry scrolls to the heading (the entry's hit-test emits a
// `#toc:<nodeId>` fragment that EditorView intercepts). When the caret is in the block,
// the builder rebuilds it as a normal paragraph showing the literal "[TOC]" for editing.

#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "document/NodeId.h"
#include "render/BlockLayout.h"
#include "render/DocumentLayout.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QRectF>

#include <functional>

#include "RenderTestUtils.h"

using namespace muffin;

namespace {

// Find the top-level Paragraph whose single text inline is "[TOC]" (case-insensitive).
NodeId findTocNodeId(const MarkdownDocument& doc) {
  for (const auto& child : doc.root().children()) {
    if (child->type() == BlockType::Paragraph && child->inlines().size() == 1 &&
        child->inlines().constFirst().type() == InlineType::Text &&
        child->inlines().constFirst().text().trimmed().compare(QStringLiteral("[TOC]"), Qt::CaseInsensitive) == 0) {
      return child->id();
    }
  }
  return {};
}

// While the caret is outside the block, [TOC] renders as a generated list with one row
// per heading, each carrying the heading's title, level, and target NodeId.
void testTocRendersEntryList() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("# A\n## B\n\n[TOC]\n"), false);
  const NodeId tocId = findTocNodeId(session.document());
  require(tocId.isValid(), QStringLiteral("[TOC] paragraph should parse"));

  RenderTheme theme = RenderTheme::github();
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  const BlockLayout* blk = layout.block(tocId);
  require(blk != nullptr, QStringLiteral("TOC block should be built"));
  require(blk->isToc(), QStringLiteral("[TOC] should render as a generated list when the caret is outside"));
  require(blk->tocEntries().size() == 2,
          QStringLiteral("TOC should list both headings (got %1)").arg(blk->tocEntries().size()));
  require(blk->tocEntries().at(0).title == QStringLiteral("A"), QStringLiteral("first entry title"));
  require(blk->tocEntries().at(0).level == 1, QStringLiteral("first entry level"));
  require(blk->tocEntries().at(1).title == QStringLiteral("B"), QStringLiteral("second entry title"));
  require(blk->tocEntries().at(1).level == 2, QStringLiteral("second entry level"));
  require(blk->tocEntries().at(0).target.isValid(), QStringLiteral("each entry should carry a target heading NodeId"));
}

// When the caret is focused inside the [TOC] block, the builder flips it back to a plain
// paragraph so the literal "[TOC]" shows for editing (Typora-style live toggle).
void testTocRevealsLiteralWhenCaretInside() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("# A\n\n[TOC]\n"), false);
  const NodeId tocId = findTocNodeId(session.document());
  require(tocId.isValid(), QStringLiteral("[TOC] paragraph should parse"));

  RenderTheme theme = RenderTheme::github();
  SelectionRange sel;
  sel.focus.blockId = tocId;
  sel.focus.text.nodeId = tocId;
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0, sel);
  const BlockLayout* blk = layout.block(tocId);
  require(blk != nullptr, QStringLiteral("TOC block should be built"));
  require(!blk->isToc(),
          QStringLiteral("with the caret in the block, [TOC] reveals the literal marker for editing"));
}

// Indentation follows the heading level, so a level-4 heading sits deeper than a level-1.
void testTocEntryIndentationByLevel() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("# A\n#### B\n\n[TOC]\n"), false);
  const NodeId tocId = findTocNodeId(session.document());
  RenderTheme theme = RenderTheme::github();
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  const BlockLayout* blk = layout.block(tocId);
  require(blk->tocEntries().size() == 2, QStringLiteral("both headings listed"));
  require(blk->tocEntries().at(0).level == 1, QStringLiteral("h1 → level 1"));
  require(blk->tocEntries().at(1).level == 4, QStringLiteral("h4 → level 4"));
}

// Clicking a TOC entry resolves to a `#toc:<nodeId>` href encoding the target heading;
// the EditorView intercepts that fragment to scroll. Clicking outside any entry selects
// the block (no caret, no navigation href).
void testTocEntryHitTestEmitsFragmentHref() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("# A\n## B\n\n[TOC]\n"), false);
  const NodeId tocId = findTocNodeId(session.document());
  RenderTheme theme = RenderTheme::github();
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  const BlockLayout* blk = layout.block(tocId);
  require(blk->isToc(), QStringLiteral("block should be in preview mode"));

  const QRectF firstRect = blk->tocEntries().at(0).rect;
  const HitTestResult hit = blk->hitTest(firstRect.center(), theme);
  require(hit.linkHref.startsWith(QStringLiteral("#toc:")),
          QStringLiteral("clicking a TOC entry yields a #toc: href (got '%1')").arg(hit.linkHref));
  const NodeId resolved = NodeId::fromString(hit.linkHref.mid(5));
  require(resolved.isValid() && resolved == blk->tocEntries().at(0).target,
          QStringLiteral("the #toc: href encodes the entry's target heading"));
  // The preview is non-editable: a click selects the whole block (no caret is placed),
  // even though it also carries the navigation href for Ctrl+click.
  require(hit.zone == HitTestResult::Zone::SelectBlock,
          QStringLiteral("a TOC block is non-editable: a click selects the block, never places a caret"));
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testTocRendersEntryList);
  RUN_TEST(testTocRevealsLiteralWhenCaretInside);
  RUN_TEST(testTocEntryIndentationByLevel);
  RUN_TEST(testTocEntryHitTestEmitsFragmentHref);
#undef RUN_TEST
  return 0;
}
