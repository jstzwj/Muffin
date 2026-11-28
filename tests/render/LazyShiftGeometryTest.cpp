// Geometry that survives lazy displacement.
//
// DocumentLayout keeps an already-built block when something changes above it: instead of
// rebuilding the block it moves it with BlockLayout::translate() and re-anchors its source
// offsets with shiftSourceOffsets(). Every member that stores document coordinates has to
// follow those two calls, or paint and hit-test read positions from before the edit.
//
// A built block also has to stay built through an edit. Under the Lazy policy an unbuilt block
// is only estimated, so wherever an edit laid the whole document out again, the viewport and the
// caret were resolved against estimates and landed away from the text.

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
  require(session.applyTextEdit(5, 0, text.repeated(14), {LocalEditNodeHint{paragraphId, 0, BlockType::Paragraph}}) !=
              muffin::TextEditResult::Rejected,
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

// Forty wrapped paragraphs, forty more between matched HTML boundaries, then forty more. The
// paragraphs between the boundaries are children of the container's slot.
QString containerDocument() {
  const auto paragraph = [](int i) {
    return QStringLiteral("Paragraph %1 is long enough to wrap onto a second line in the view.\n\n").arg(i);
  };
  QString text;
  for (int i = 0; i < 40; ++i)
    text += paragraph(i);
  text += QStringLiteral("<div style=\"text-align: left;\">\n\n");
  for (int i = 40; i < 80; ++i)
    text += paragraph(i);
  text += QStringLiteral("</div>\n\n");
  for (int i = 80; i < 120; ++i)
    text += paragraph(i);
  text.chop(1); // a document ends with one newline, not with an empty paragraph
  return text;
}

NodeId paragraphStartingWith(const MarkdownDocument& document, const QString& prefix) {
  for (const auto& child : document.root().children()) {
    if (child->type() == BlockType::Paragraph && child->inlines().size() == 1 &&
        child->inlines().front().type() == InlineType::Text && child->inlines().front().text().startsWith(prefix))
      return child->id();
  }
  return {};
}

// A container document laid out lazily, with a reading window built around its container.
struct ContainerReading {
  DocumentSession session;
  RenderTheme theme = RenderTheme::github();
  DocumentLayout layout;
  NodeId container, after;
  qreal containerTop = 0.0;

  ContainerReading() {
    session.setMarkdownText(containerDocument(), false);
    layout.rebuild(session.document(), theme, 800.0, SelectionRange{}, QString(), DocumentLayout::BuildPolicy::Lazy);
    const NodeId inside = paragraphStartingWith(session.document(), QStringLiteral("Paragraph 60 "));
    require(inside.isValid(), "the paragraphs between the boundaries should parse");
    const qsizetype slot = layout.topLevelIndexFor(inside);
    require(slot == 40 && layout.slotNodeId(slot) != inside && layout.slotCount() == 81,
            QStringLiteral("the boundaries should group the paragraphs between them into one slot: slot %1 of %2")
                .arg(slot)
                .arg(layout.slotCount()));
    container = layout.slotNodeId(slot);
    after = layout.slotNodeId(slot + 1);
    layout.ensureBuilt(slot - 2, slot + 2, theme);
    containerTop = layout.block(container)->rect().top();
  }

  qsizetype offsetOf(const QString& words) const {
    const qsizetype at = session.markdownText().toString().indexOf(words);
    require(at >= 0, "the edited words should be in the document");
    return at;
  }

  void requireTheWindowKeepsItsPlace(const char* where) const {
    require(layout.blockIfPromoted(container) != nullptr && layout.blockIfPromoted(after) != nullptr,
            QStringLiteral("%1: the blocks that were built should still be built").arg(QString::fromLatin1(where)));
    require(std::abs(layout.block(container)->rect().top() - containerTop) < 0.5,
            QStringLiteral("%1: the container should keep its place: top %2 before, %3 after")
                .arg(QString::fromLatin1(where))
                .arg(containerTop)
                .arg(layout.block(container)->rect().top()));
  }
};

// An edit inside a container used to lay the whole document out again. The Lazy skeleton estimates
// every block, so the window being read lost its measured geometry and the viewport and the caret
// that followed were resolved against estimates, the more the deeper into the document.
void testAnEditInsideAContainerKeepsTheBuiltBlocks() {
  ContainerReading reading;
  const NodeId inside = paragraphStartingWith(reading.session.document(), QStringLiteral("Paragraph 60 "));
  const qsizetype at = reading.offsetOf(QStringLiteral("Paragraph 60 "));
  require(reading.session.applyTextEdit(at, 0, QStringLiteral("Typed "),
                                        {LocalEditNodeHint{inside, 0, BlockType::Paragraph}}) !=
              TextEditResult::Rejected,
          "the edit inside the container should apply");
  const DocumentLayout::BlockRebuildResult result =
      reading.layout.rebuildBlock(inside, reading.session.document(), reading.theme, SelectionRange{});
  require(result.rebuilt, "the edit should be laid out");
  reading.requireTheWindowKeepsItsPlace("after an edit inside the container");
  require(std::abs(result.oldRect.top() - reading.containerTop) < 0.5,
          "the edit should be laid out in the container's own place, not with the whole document");
}

// A structural edit still lays a container document out again, since a boundary may have moved.
// The blocks that were built are measured again before anything reads the layout.
void testAStructuralEditKeepsTheBuiltWindowOfAContainerDocument() {
  ContainerReading reading;
  const NodeId split = paragraphStartingWith(reading.session.document(), QStringLiteral("Paragraph 61 "));
  const qsizetype at =
      reading.offsetOf(QStringLiteral("Paragraph 61 ")) + QStringLiteral("Paragraph 61 is long").size();
  require(reading.session.applyTextEdit(at, 0, QStringLiteral("\n\n"),
                                        {LocalEditNodeHint{split, 0, BlockType::Paragraph}}) !=
              TextEditResult::Rejected,
          "the structural edit inside the container should apply");
  const TopLevelRangeChange range = reading.session.lastLocalTopLevelRangeChange();
  require(range.isValid() && range.first <= 61 && range.newCount == range.oldCount + 1,
          "splitting a paragraph should report a range that holds one block more than before");
  const DocumentLayout::RangeRebuildResult result =
      reading.layout.rebuildTopLevelRange(range, reading.session.document(), reading.theme, SelectionRange{});
  require(result.rebuilt, "the changed range should be laid out");
  reading.requireTheWindowKeepsItsPlace("after a structural edit inside the container");
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
  RUN_TEST(testAnEditInsideAContainerKeepsTheBuiltBlocks);
  RUN_TEST(testAStructuralEditKeepsTheBuiltWindowOfAContainerDocument);
#undef RUN_TEST
  return 0;
}
