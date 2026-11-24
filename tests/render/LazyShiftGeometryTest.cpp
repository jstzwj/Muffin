// Geometry that survives lazy displacement.
//
// DocumentLayout keeps an already-built block when something changes above it: instead of
// rebuilding the block it moves it with BlockLayout::translate() and re-anchors its source
// offsets with shiftSourceOffsets(). Every member that stores document coordinates has to
// follow those two calls, or paint and hit-test read positions from before the edit.

#include "document/DefinitionBlock.h"
#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "document/NodeId.h"
#include "render/BlockLayout.h"
#include "render/DocumentLayout.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QVector>

#include <cmath>

#include "RenderTestUtils.h"

using namespace muffin;

namespace {

// Grow the first paragraph so that everything after it has to move down.
void growFirstParagraph(DocumentSession& session, DocumentLayout& layout, const RenderTheme& theme) {
  const NodeId paragraphId = mutableBlockAt(session.document(), 0)->id();
  const QString text = QStringLiteral("alpha beta gamma delta ");
  require(session.applyTextDelta(5, 0, text.repeated(14), true, {LocalEditNodeHint{paragraphId, 0, BlockType::Paragraph}}),
          QStringLiteral("the height-changing edit should apply"));
  require(layout.rebuildBlock(paragraphId, session.document(), theme, {}).rebuilt,
          QStringLiteral("the edited block should rebuild in place"));
}

// Find the top-level Paragraph whose single text inline is "[TOC]".
NodeId findTocNodeId(const MarkdownDocument& document) {
  for (const auto& child : document.root().children()) {
    if (child->type() == BlockType::Paragraph && child->inlines().size() == 1 &&
        child->inlines().front().type() == InlineType::Text &&
        child->inlines().front().text().trimmed().compare(QStringLiteral("[TOC]"), Qt::CaseInsensitive) == 0)
      return child->id();
  }
  return {};
}

// `[TOC]` rows store document coordinates shared by paint and hit-test, so a row has to keep
// its place inside the block when the block itself is translated.
void testTocRowsFollowTheirBlock() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("alpha\n\n[TOC]\n\n# A\n\n## B\n"), false);
  const NodeId tocId = findTocNodeId(session.document());
  require(tocId.isValid(), QStringLiteral("[TOC] paragraph should parse"));

  const RenderTheme theme = RenderTheme::github();
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  const BlockLayout* before = layout.block(tocId);
  require(before && before->isToc() && before->tocEntries().size() == 2,
          QStringLiteral("[TOC] should render two rows before the edit"));
  const qreal blockTop = before->rect().top();
  const NodeId firstTarget = before->tocEntries().front().target;
  QVector<qreal> rowOffsets;
  for (const BlockLayout::TocEntryLayout& entry : before->tocEntries())
    rowOffsets.append(entry.rect.top() - blockTop);

  growFirstParagraph(session, layout, theme);

  const BlockLayout* after = layout.block(tocId);
  require(after->rect().top() - blockTop > 0.0, QStringLiteral("the [TOC] block should have moved down"));
  require(after->tocEntries().size() == rowOffsets.size(), QStringLiteral("the rows should be kept, not rebuilt"));
  for (int i = 0; i < rowOffsets.size(); ++i)
    require(std::abs((after->tocEntries().at(i).rect.top() - after->rect().top()) - rowOffsets.at(i)) < 0.5,
            QStringLiteral("row %1 did not follow its block: offset %2 before, %3 after")
                .arg(i)
                .arg(rowOffsets.at(i))
                .arg(after->tocEntries().at(i).rect.top() - after->rect().top()));
  // The rect that is painted is the rect that is clicked.
  const HitTestResult hit = after->hitTest(after->tocEntries().front().rect.center(), theme);
  require(NodeId::fromString(hit.linkHref.mid(5)) == firstTarget,
          QStringLiteral("clicking the moved first row should target its heading (href '%1')").arg(hit.linkHref));
}

// Link-reference and footnote definitions copy their field ranges out of the document. An edit
// above them moves the source, so the ranges have to move with it.
void testDefinitionRangesFollowTheirBlock() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("alpha\n\nText with a note[^1]\n\n[^1]: the note\n\n[ref]: "
                                         "https://example.com/ \"Title\""),
                          false);
  const auto firstOf = [&session](BlockType type) {
    for (const auto& child : session.document().root().children())
      if (child->type() == type)
        return child->id();
    return NodeId{};
  };
  const NodeId footnoteId = firstOf(BlockType::FootnoteDefinition);
  const NodeId referenceId = firstOf(BlockType::LinkDefinition);
  require(footnoteId.isValid(), QStringLiteral("[^1]: should parse as a footnote definition"));
  require(referenceId.isValid(), QStringLiteral("[ref]: should parse as a link definition"));

  const RenderTheme theme = RenderTheme::github();
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  // A range addresses the document while the text under it is the field it was parsed from.
  const auto requireRangesAddressTheDocument = [&](const BlockLayout& block, const QString& label) {
    const QString text = session.markdownText().toString();
    const DefinitionBlock& definition = block.definition();
    require(definition.isValid(), QStringLiteral("%1: the block should carry its definition").arg(label));
    const auto addresses = [&](const DefinitionFieldRange& range, const QString& field, const char* name) {
      const bool valid = range.isValid() && text.mid(range.start, range.length()) == field;
      require(valid, QStringLiteral("%1: the %2 range [%3, %4) should hold '%5' but holds '%6'")
                         .arg(label, QLatin1String(name))
                         .arg(range.start)
                         .arg(range.end)
                         .arg(field, text.mid(range.start, range.length())));
      return valid;
    };
    const bool footnote = definition.kind == DefinitionBlock::Kind::Footnote;
    addresses(definition.markerRange, footnote ? QStringLiteral("[^1]:") : QStringLiteral("[ref]:"), "marker");
    addresses(definition.labelRange, definition.label, "label");
    if (footnote) {
      addresses(definition.noteRange, definition.note, "note");
    } else {
      addresses(definition.destinationRange, definition.destination, "destination");
      addresses(definition.titleRange, definition.title, "title");
    }
  };
  requireRangesAddressTheDocument(*layout.block(footnoteId), QStringLiteral("footnote before"));
  requireRangesAddressTheDocument(*layout.block(referenceId), QStringLiteral("reference before"));

  growFirstParagraph(session, layout, theme);

  requireRangesAddressTheDocument(*layout.block(footnoteId), QStringLiteral("footnote after"));
  requireRangesAddressTheDocument(*layout.block(referenceId), QStringLiteral("reference after"));
}

} // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testTocRowsFollowTheirBlock);
  RUN_TEST(testDefinitionRangesFollowTheirBlock);
#undef RUN_TEST
  return 0;
}
