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
//
// Documents that render through HTML containers are the case to watch: their slots are borrowed
// from the theme's topology, and an edit there has to stay as local as one anywhere else.

#include "document/DefinitionBlock.h"
#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "document/NodeId.h"
#include "render/BlockLayout.h"
#include "render/DocumentLayout.h"
#include "theme/MarkdownFlow.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QVector>

#include <algorithm>
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
  const auto& document = reading.session.document();
  const NodeId inside = paragraphStartingWith(document, QStringLiteral("Paragraph 60 "));
  const NodeId neighbour = paragraphStartingWith(document, QStringLiteral("Paragraph 50 "));
  const BlockLayout* neighbourLayout = reading.layout.block(neighbour);
  const NodeId closer = document.root().children()[81]->id();
  const qsizetype at = reading.offsetOf(QStringLiteral("Paragraph 60 "));
  require(reading.session.applyTextEdit(at, 0, QStringLiteral("Typed "),
                                        {LocalEditNodeHint{inside, at, BlockType::Paragraph}}) !=
              TextEditResult::Rejected,
          "the edit inside the container should apply");
  const DocumentLayout::BlockRebuildResult result =
      reading.layout.rebuildBlock(inside, document, reading.theme, SelectionRange{});
  require(result.rebuilt, "the edit should be laid out");
  reading.requireTheWindowKeepsItsPlace("after an edit inside the container");
  require(std::abs(result.oldRect.top() - reading.containerTop) < 0.5,
          "the edit should be laid out in the container's own place, not with the whole document");
  // A paragraph the edit did not touch keeps its layout, and the borrowed topology addresses the
  // paragraph that replaced the edited one.
  require(reading.layout.block(neighbour) == neighbourLayout, "an untouched paragraph keeps its layout");
  const auto flow = reading.theme.markdownFlow(document.root());
  const MarkdownNode* opener = document.node(reading.container);
  const MarkdownNode* edited = document.node(inside);
  const MarkdownNode* next = document.node(paragraphStartingWith(document, QStringLiteral("Paragraph 61 ")));
  const auto grouped = flow->children(*opener);
  require(grouped.size() == 40 && std::find(grouped.cbegin(), grouped.cend(), edited) != grouped.cend(),
          "the container holds the replacement paragraph");
  require(flow->parent(*edited) == opener && flow->next(*edited) == next && flow->previous(*next) == edited,
          "the links of the topology lead to and from the replacement paragraph");
  require(flow->container(*opener)->closing == document.node(closer), "the closer is where the document has it");
}

// A structural edit inside a container moves no slot: the container is laid out again where it
// stands, and nothing else is. It used to lay the whole document out again.
void testAStructuralEditInsideAContainerStaysLocal() {
  ContainerReading reading;
  const NodeId split = paragraphStartingWith(reading.session.document(), QStringLiteral("Paragraph 61 "));
  const BlockLayout* afterLayout = reading.layout.block(reading.after);
  const qsizetype at =
      reading.offsetOf(QStringLiteral("Paragraph 61 ")) + QStringLiteral("Paragraph 61 is long").size();
  require(reading.session.applyTextEdit(at, 0, QStringLiteral("\n\n"),
                                        {LocalEditNodeHint{split, at, BlockType::Paragraph}}) !=
              TextEditResult::Rejected,
          "the structural edit inside the container should apply");
  const TopLevelRangeChange range = reading.session.lastLocalTopLevelRangeChange();
  require(range.isValid() && range.first <= 61 && range.newCount == range.oldCount + 1,
          "splitting a paragraph should report a range that holds one block more than before");
  const DocumentLayout::RangeRebuildResult result =
      reading.layout.rebuildTopLevelRange(range, reading.session.document(), reading.theme, SelectionRange{});
  require(result.rebuilt, "the changed range should be laid out");
  require(std::abs(result.oldRect.top() - reading.containerTop) < 0.5,
          "the range should be laid out in the container's own place, not with the whole document");
  reading.requireTheWindowKeepsItsPlace("after a structural edit inside the container");
  require(reading.layout.slotCount() == 81, "no slot should move");
  const auto& document = reading.session.document();
  const NodeId secondHalf = paragraphStartingWith(document, QStringLiteral("enough to wrap onto"));
  require(secondHalf.isValid() && reading.layout.block(secondHalf) != nullptr &&
              reading.layout.topLevelBlockIdFor(secondHalf) == reading.container,
          "the new paragraph is laid out inside the container");
  require(reading.layout.block(reading.after) == afterLayout, "the slot after the container keeps its layout");
}

// An edit that breaks a pairing releases the blocks it grouped. They become slots of their own
// between the slots that stayed, which keep their layouts.
void testABrokenPairingSplicesTheSlots() {
  ContainerReading reading;
  const qsizetype at = reading.offsetOf(QStringLiteral("</div>"));
  require(reading.session.applyTextEdit(at, 6, QStringLiteral("</span>")) != TextEditResult::Rejected,
          "the closing tag should be editable");
  const TopLevelRangeChange range = reading.session.lastLocalTopLevelRangeChange();
  require(range.isValid(), "editing the closing tag should report the range it changed");
  const DocumentLayout::RangeRebuildResult result =
      reading.layout.rebuildTopLevelRange(range, reading.session.document(), reading.theme, SelectionRange{});
  require(result.rebuilt, "the changed range should be laid out");
  const auto& document = reading.session.document();
  require(reading.theme.markdownFlow(document.root())->empty(), "a broken pairing is no container");
  // Forty paragraphs, the opener, forty paragraphs, the closer, forty paragraphs.
  require(reading.layout.slotCount() == 122,
          QStringLiteral("every block is a slot of its own: %1 slots").arg(reading.layout.slotCount()));
  const NodeId former = paragraphStartingWith(document, QStringLiteral("Paragraph 50 "));
  require(reading.layout.topLevelIndexFor(former) == 51, "a released paragraph takes a slot of its own");
  const BlockLayout* opener = reading.layout.block(reading.container);
  require(opener && opener->children().empty(), "the opener groups nothing any more");
  for (qsizetype i = 1; i < reading.layout.slotCount(); ++i)
    require(reading.layout.slotTop(i) >= reading.layout.slotTop(i - 1) + reading.layout.slotHeight(i - 1) - 0.01,
            QStringLiteral("slot %1 should start where slot %2 ends").arg(i).arg(i - 1));
  require(reading.layout.blockIfPromoted(reading.after) != nullptr,
          "the slots after the edit are still built where they are now");
}

// A plain document that gains its first pairing: the blocks between the boundaries stop being
// slots of their own.
void testTheFirstContainerTakesItsBlocks() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("alpha\n\nbeta\n\ngamma\n\ndelta\n"), false);
  RenderTheme theme = RenderTheme::github();
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  require(layout.slotCount() == 4, "four paragraphs are four slots");
  const NodeId alpha = paragraphStartingWith(session.document(), QStringLiteral("alpha"));
  const NodeId delta = paragraphStartingWith(session.document(), QStringLiteral("delta"));
  const BlockLayout* alphaLayout = layout.block(alpha);
  const BlockLayout* deltaLayout = layout.block(delta);
  const QString text = session.markdownText().toString();
  const qsizetype beta = text.indexOf(QStringLiteral("beta"));
  require(session.applyTextEdit(beta, text.indexOf(QStringLiteral("delta")) - beta,
                                QStringLiteral("<div>\n\ngamma\n\n</div>\n\n")) != TextEditResult::Rejected,
          "the paragraphs should be wrapped in one edit");
  const TopLevelRangeChange range = session.lastLocalTopLevelRangeChange();
  require(range.isValid() && range.first <= 1, "wrapping paragraphs should report the range it changed");
  const DocumentLayout::RangeRebuildResult result =
      layout.rebuildTopLevelRange(range, session.document(), theme, SelectionRange{});
  require(result.rebuilt, "the changed range should be laid out");
  require(layout.slotCount() == 3, "the first paragraph, the container and the last paragraph");
  const auto& document = session.document();
  const MarkdownNode* opener = document.root().children()[1].get();
  require(theme.markdownFlow(document.root())->container(*opener) != nullptr, "the boundaries pair up");
  const NodeId wrapped = paragraphStartingWith(document, QStringLiteral("gamma"));
  require(layout.topLevelBlockIdFor(wrapped) == opener->id() && layout.block(wrapped) != nullptr,
          "the paragraph between the boundaries is laid out inside the container");
  require(layout.slotTop(2) >= layout.slotTop(1) + layout.slotHeight(1) - 0.01,
          "the paragraph after the container starts where the container ends");
  require(layout.block(alpha) == alphaLayout && layout.block(delta) == deltaLayout,
          "the paragraphs outside the edit keep their layouts");
}

// The signatures a pass keeps are worth the next pass only while what they depend on stays: a new
// text size lays every paragraph of the container out again.
void testANewTextSizeLaysTheKeptParagraphsOutAgain() {
  ContainerReading reading;
  const NodeId inside = paragraphStartingWith(reading.session.document(), QStringLiteral("Paragraph 60 "));
  const NodeId neighbour = paragraphStartingWith(reading.session.document(), QStringLiteral("Paragraph 50 "));
  const qsizetype at = reading.offsetOf(QStringLiteral("Paragraph 60 "));
  require(reading.session.applyTextEdit(at, 0, QStringLiteral("Typed "),
                                        {LocalEditNodeHint{inside, at, BlockType::Paragraph}}) !=
              TextEditResult::Rejected,
          "the edit inside the container should apply");
  require(reading.layout.rebuildBlock(inside, reading.session.document(), reading.theme, SelectionRange{}).rebuilt,
          "the edit should be laid out");
  const BlockLayout* typed = reading.layout.block(neighbour);
  const qreal height = typed->height();
  reading.theme.setFontSizePx(reading.theme.fontSizePx() + 6);
  require(reading.layout.rebuildBlock(reading.container, reading.session.document(), reading.theme, SelectionRange{})
              .rebuilt,
          "the container should be laid out at the new text size");
  const BlockLayout* relaid = reading.layout.block(neighbour);
  require(relaid && relaid != typed && relaid->height() > height, "a paragraph is laid out again at the new text size");
}

// The topology names blocks by id, so a replacement that was named anew cannot take the place of
// the block it replaces; one that keeps its id moves the topology with it, boundaries included.
void testTheTopologyFollowsReplacedBlocks() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("<div class=\"hero\">\n\nInside the container.\n\n</div>\n\nOutside.\n"),
                          false);
  RenderTheme theme = RenderTheme::github();
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  const auto& blocks = session.document().root().children();
  const NodeId opener = blocks[0]->id(), inside = blocks[1]->id(), closer = blocks[2]->id();
  const MarkdownNode* other = blocks[3].get();
  MarkdownFlow copy = *theme.markdownFlow(session.document().root());
  require(!copy.rebind({{opener, other}}), "a replacement with another id is refused");
  require(copy.container(*blocks[0]) != nullptr && copy.children(*blocks[0]).size() == 1, "and nothing moved");

  // The closing tag is written again as it was: the edit replaces its block in place and names it,
  // as an edit of the tag in the editor does.
  const qsizetype at = session.markdownText().toString().indexOf(QStringLiteral("</div>"));
  require(session.applyTextEdit(at, 6, QStringLiteral("</div>"),
                                {LocalEditNodeHint{closer, at, BlockType::HtmlBlock}}) != TextEditResult::Rejected,
          "the closing tag should be editable");
  const TopLevelRangeChange range = session.lastLocalTopLevelRangeChange();
  require(range.isValid() && layout.rebuildTopLevelRange(range, session.document(), theme, SelectionRange{}).rebuilt,
          "writing the closing tag again should be laid out");
  const auto flow = theme.markdownFlow(session.document().root());
  require(flow->container(*session.document().node(opener)) != nullptr, "the pairing survives");
  require(flow->container(*session.document().node(opener))->closing == session.document().node(closer) &&
              flow->children(*session.document().node(opener)).front() == session.document().node(inside),
          "the container's closer and children are where the document has them now");
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
  RUN_TEST(testAStructuralEditInsideAContainerStaysLocal);
  RUN_TEST(testABrokenPairingSplicesTheSlots);
  RUN_TEST(testTheFirstContainerTakesItsBlocks);
  RUN_TEST(testANewTextSizeLaysTheKeptParagraphsOutAgain);
  RUN_TEST(testTheTopologyFollowsReplacedBlocks);
#undef RUN_TEST
  return 0;
}
