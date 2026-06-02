#include "app/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "parser/CmarkGfmParser.h"
#include "render/DocumentLayout.h"
#include "render/TreeSitterHighlighter.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QImage>
#include <QPainter>

using namespace muffin;

namespace {

[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) {
    fail(message);
  }
}

QString readFixture(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly | QIODevice::Text), QStringLiteral("Could not open fixture: %1").arg(path));
  return QString::fromUtf8(file.readAll());
}

const MarkdownNode* findFirstBlock(const MarkdownNode& node, BlockType type) {
  if (node.type() == type) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (const MarkdownNode* found = findFirstBlock(*child, type)) {
      return found;
    }
  }
  return nullptr;
}

const MarkdownNode* findBlockWithLiteral(const MarkdownNode& node, BlockType type, const QString& literalPart) {
  if (node.type() == type && node.literal().contains(literalPart)) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (const MarkdownNode* found = findBlockWithLiteral(*child, type, literalPart)) {
      return found;
    }
  }
  return nullptr;
}

const MarkdownNode* findFirstTableCell(const MarkdownNode& node) {
  if (node.type() == BlockType::TableCell) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (const MarkdownNode* found = findFirstTableCell(*child)) {
      return found;
    }
  }
  return nullptr;
}

MarkdownNode* mutableBlockAt(MarkdownDocument& document, qsizetype index) {
  auto& children = document.root().children();
  require(index >= 0 && index < static_cast<qsizetype>(children.size()), QStringLiteral("mutable block index out of range"));
  return children.at(static_cast<size_t>(index)).get();
}

void replaceDocumentText(MarkdownDocument& document, const QString& markdown) {
  CmarkGfmParser parser;
  ParseOptions options;
  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("incremental test parse returned null root"));
  document.setMarkdownText(markdown, std::move(parsed.root));
}

void requireUsableRect(const QRectF& rect, const QString& label) {
  require(rect.isValid(), QStringLiteral("%1 rect is invalid").arg(label));
  require(rect.width() > 20.0, QStringLiteral("%1 rect width too small").arg(label));
  require(rect.height() > 10.0, QStringLiteral("%1 rect height too small").arg(label));
}

void testIncrementalBlockRebuildContract() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("alpha\n\nbeta\n\n| A | B |\n| --- | --- |\n| 1 | 2 |"), false);

  RenderTheme theme = RenderTheme::github();
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);

  const NodeId firstParagraphId = mutableBlockAt(session.document(), 0)->id();
  const NodeId secondParagraphId = mutableBlockAt(session.document(), 1)->id();
  const QRectF firstBefore = layout.block(firstParagraphId)->rect();
  const QRectF secondBefore = layout.block(secondParagraphId)->rect();
  const qreal totalBefore = layout.totalHeight();

  require(session.applyTextDelta(
              5,
              0,
              QStringLiteral(" alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha"),
              true,
              {LocalEditNodeHint{firstParagraphId, 0, BlockType::Paragraph}}),
          QStringLiteral("paragraph local delta should apply"));
  const DocumentLayout::BlockRebuildResult paragraphResult = layout.rebuildBlock(firstParagraphId, session.document(), theme, {});
  require(paragraphResult.rebuilt, QStringLiteral("paragraph block rebuild should succeed"));
  require(paragraphResult.blockId == firstParagraphId, QStringLiteral("paragraph rebuild result id mismatch"));
  require(paragraphResult.oldRect == firstBefore, QStringLiteral("paragraph rebuild old rect mismatch"));
  require(paragraphResult.newRect == layout.block(firstParagraphId)->rect(), QStringLiteral("paragraph rebuild new rect mismatch"));
  require(paragraphResult.heightDelta != 0, QStringLiteral("paragraph rebuild should report height delta"));
  require(!paragraphResult.shiftedRect.isEmpty(), QStringLiteral("paragraph rebuild should report shifted rect"));
  require(layout.block(secondParagraphId)->rect().top() != secondBefore.top(), QStringLiteral("paragraph rebuild should shift following block"));
  require(layout.totalHeight() != totalBefore, QStringLiteral("paragraph rebuild should update total height"));

  const MarkdownNode* tableCell = findFirstTableCell(session.document().root());
  require(tableCell != nullptr, QStringLiteral("incremental table cell missing"));
  MarkdownNode* table = mutableBlockAt(session.document(), 2);
  require(layout.block(tableCell->id()) != nullptr, QStringLiteral("layout index should include table cell"));
  const QRectF tableBefore = layout.block(table->id())->rect();
  const DocumentLayout::BlockRebuildResult tableResult = layout.rebuildBlock(tableCell->id(), session.document(), theme, {});
  require(tableResult.rebuilt, QStringLiteral("table cell rebuild should map to top-level table"));
  require(tableResult.blockId == table->id(), QStringLiteral("table cell rebuild should report table block id"));
  require(tableResult.oldRect == tableBefore, QStringLiteral("table cell rebuild old rect mismatch"));
  require(tableResult.newRect == layout.block(table->id())->rect(), QStringLiteral("table cell rebuild new rect mismatch"));
}

void testInlineMarkerExpansion() {
  QVector<InlineNode> inlines;
  inlines.push_back(InlineNode::text(QStringLiteral("before ")));
  inlines.push_back(InlineNode::strong(QStringLiteral("**"), QVector<InlineNode>{InlineNode::text(QStringLiteral("bold"))}));
  inlines.push_back(InlineNode::text(QStringLiteral(" after")));

  RenderTheme theme = RenderTheme::github();
  InlineLayout collapsed;
  collapsed.build(inlines, theme, 400.0, theme.paragraphFont());
  require(!collapsed.displayText().contains(QStringLiteral("**")), QStringLiteral("collapsed inline should hide strong markers"));

  InlineLayout expanded;
  InlineLayout::BuildOptions options;
  options.projectionState.cursorVisibleOffset = 8;
  options.projectionState.cursorSourceOffset = 10;
  expanded.build(inlines, QStringLiteral("before **bold** after"), theme, 400.0, theme.paragraphFont(), options);
  require(expanded.displayText().contains(QStringLiteral("**")), QStringLiteral("active inline should show strong markers"));
  require(expanded.plainText() == QStringLiteral("before bold after"), QStringLiteral("expanded plain text should stay collapsed"));
  require(expanded.hitTestTextOffset(expanded.cursorRect(8).center()) == 8,
          QStringLiteral("expanded hit test should map display marker offsets back to visible offsets"));
  require(expanded.hitTestSourceOffset(expanded.cursorRectForSourceOffset(9).center()) == 9,
          QStringLiteral("expanded hit test should map opener marker display to source offset"));

  QVector<InlineNode> mathInlines;
  mathInlines.push_back(InlineNode::inlineMath(QStringLiteral("a123")));
  InlineLayout math;
  InlineLayout::BuildOptions mathOptions;
  mathOptions.projectionState.cursorSourceOffset = 2;
  math.build(mathInlines, QStringLiteral("$a123$"), theme, 400.0, theme.paragraphFont(), mathOptions);
  require(math.hitTestSourceOffset(math.cursorRectForSourceOffset(2).center()) == 2,
          QStringLiteral("math cursor rect should round-trip source offset after first char"));

  InlineLayout selectionExpanded;
  InlineLayout::BuildOptions selectionOptions;
  selectionOptions.projectionState.selectionVisibleStart = 8;
  selectionOptions.projectionState.selectionVisibleEnd = 10;
  selectionExpanded.build(inlines, QStringLiteral("before **bold** after"), theme, 400.0, theme.paragraphFont(), selectionOptions);
  require(selectionExpanded.displayText().contains(QStringLiteral("**")), QStringLiteral("selection touching inline should show strong markers"));
}

void testInlineProjectionContract() {
  RenderTheme theme = RenderTheme::github();
  InlineLayout::BuildOptions options;
  const QVector<InlineNode> linkInlines{
      InlineNode::link(QStringLiteral("https://example.com"), QString(), {InlineNode::text(QStringLiteral("label"))})};
  const QString linkMarkdown = QStringLiteral("[label](https://example.com)");

  InlineLayout collapsedLink;
  collapsedLink.build(linkInlines, linkMarkdown, theme, 400.0, theme.paragraphFont(), options);
  require(collapsedLink.displayText() == QStringLiteral("label"), QStringLiteral("inactive link projection text mismatch"));
  require(!collapsedLink.displayText().contains(QStringLiteral("](")), QStringLiteral("inactive link should not render source syntax"));

  InlineLayout::BuildOptions activeLinkOptions = options;
  activeLinkOptions.projectionState.cursorSourceOffset = 2;
  InlineLayout activeLink;
  activeLink.build(linkInlines, linkMarkdown, theme, 400.0, theme.paragraphFont(), activeLinkOptions);
  require(activeLink.displayText() == linkMarkdown, QStringLiteral("active link projection text mismatch"));
  require(activeLink.displayText().contains(QStringLiteral("](")), QStringLiteral("active link should render source syntax"));
  require(activeLink.hitTestSourceOffset(activeLink.cursorRectForSourceOffset(2).center()) == 2,
          QStringLiteral("active link cursor rect should round-trip source offset"));
  require(!activeLink.selectionRects(0, 5).isEmpty(), QStringLiteral("active link selection rects should remain valid"));

  const QVector<InlineNode> imageInlines{InlineNode::image(QStringLiteral("https://example.com/image.png"), QStringLiteral("alt"), QString())};
  const QString imageMarkdown = QStringLiteral("![alt](https://example.com/image.png)");

  InlineLayout collapsedImage;
  collapsedImage.build(imageInlines, imageMarkdown, theme, 400.0, theme.paragraphFont(), options);
  require(collapsedImage.displayText() == QStringLiteral("alt"), QStringLiteral("inactive image projection text mismatch"));
  require(!collapsedImage.displayText().contains(QStringLiteral("![")), QStringLiteral("inactive image should not render source syntax"));

  InlineLayout::BuildOptions activeImageOptions = options;
  activeImageOptions.projectionState.cursorSourceOffset = 2;
  InlineLayout activeImage;
  activeImage.build(imageInlines, imageMarkdown, theme, 400.0, theme.paragraphFont(), activeImageOptions);
  require(activeImage.displayText() == imageMarkdown, QStringLiteral("active image projection text mismatch"));
  require(activeImage.displayText().contains(QStringLiteral("![")), QStringLiteral("active image should render source syntax"));
  require(activeImage.hitTestSourceOffset(activeImage.cursorRectForSourceOffset(2).center()) == 2,
          QStringLiteral("active image cursor rect should round-trip source offset"));
  require(!activeImage.selectionRects(0, 3).isEmpty(), QStringLiteral("active image selection rects should remain valid"));
}

void testInlineLayoutGeometryContract() {
  RenderTheme theme = RenderTheme::github();
  QVector<InlineNode> plainInlines;
  plainInlines.push_back(InlineNode::text(QStringLiteral("alpha beta gamma delta epsilon zeta eta theta")));

  InlineLayout plain;
  plain.build(plainInlines, theme, 130.0, theme.paragraphFont());
  require(plain.height() > 0.0, QStringLiteral("inline layout should measure plain inline height"));
  require(plain.size().height() == plain.height(), QStringLiteral("inline layout size should expose measured height"));

  const QRectF plainCursor = plain.cursorRect(6);
  require(!plainCursor.isEmpty(), QStringLiteral("inline layout cursor rect should be available"));
  require(plain.hitTestTextOffset(QPointF(plainCursor.left(), plainCursor.center().y())) == 6,
          QStringLiteral("inline layout cursor point should round-trip plain offset"));
  require(!plain.selectionRects(1, 18).isEmpty(), QStringLiteral("inline layout selection rects should be available"));

  QVector<InlineNode> styledInlines;
  styledInlines.push_back(InlineNode::text(QStringLiteral("before ")));
  styledInlines.push_back(InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("bold"))}));
  styledInlines.push_back(InlineNode::text(QStringLiteral(" ")));
  styledInlines.push_back(InlineNode::link(QStringLiteral("u"), QString(), {InlineNode::text(QStringLiteral("link"))}));
  styledInlines.push_back(InlineNode::text(QStringLiteral(" ")));
  styledInlines.push_back(InlineNode::code(QStringLiteral("code")));

  InlineLayout::BuildOptions options;
  options.projectionState.revealMarkdownMarkers = true;
  InlineLayout styled;
  styled.build(styledInlines, QStringLiteral("before **bold** [link](u) `code`"), theme, 180.0, theme.paragraphFont(), options);
  require(styled.height() > 0.0, QStringLiteral("inline layout should measure styled inline height"));
  require(styled.displayText() == QStringLiteral("before **bold** [link](u) `code`"), QStringLiteral("styled display text should match projection"));
  const QRectF styledCursor = styled.cursorRect(9);
  require(!styledCursor.isEmpty(), QStringLiteral("inline layout styled cursor rect should be available"));
  const qsizetype styledHit = styled.hitTestTextOffset(QPointF(styledCursor.left(), styledCursor.center().y()));
  require(styledHit >= 0 && styledHit <= styled.plainText().size(), QStringLiteral("inline layout hit-test should return a valid styled offset"));
  require(!styled.selectionRects(0, styled.plainText().size()).isEmpty(), QStringLiteral("inline layout should produce styled selection rects"));
}

void testInlineLayoutPainting() {
  RenderTheme theme = RenderTheme::github();
  QVector<InlineNode> inlines;
  inlines.push_back(InlineNode::text(QStringLiteral("before ")));
  inlines.push_back(InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("bold"))}));
  inlines.push_back(InlineNode::text(QStringLiteral(" ")));
  inlines.push_back(InlineNode::link(QStringLiteral("https://example.com"), QString(), {InlineNode::text(QStringLiteral("link"))}));
  inlines.push_back(InlineNode::text(QStringLiteral(" ")));
  inlines.push_back(InlineNode::code(QStringLiteral("code")));
  inlines.push_back(InlineNode::text(QStringLiteral(" after")));

  InlineLayout::BuildOptions options;
  options.projectionState.revealMarkdownMarkers = true;

  InlineLayout layout;
  layout.build(inlines, QStringLiteral("before **bold** [link](https://example.com) `code` after"), theme, 280.0, theme.paragraphFont(), options);
  require(layout.height() > 0.0, QStringLiteral("inline layout paint fixture should have height"));

  QImage image(QSize(320, qCeil(layout.height()) + 20), QImage::Format_ARGB32);
  image.fill(theme.backgroundColor());
  QPainter painter(&image);
  layout.paint(painter, QPointF(8.0, 8.0));
  painter.end();

  int changedPixels = 0;
  const QRgb background = theme.backgroundColor().rgb();
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      if ((image.pixel(x, y) & 0x00ffffff) != (background & 0x00ffffff)) {
        ++changedPixels;
      }
    }
  }
  require(changedPixels > 25, QStringLiteral("inline layout paint should draw visible pixels"));
}

void requireTextLayoutCursorRoundTrip(const InlineLayout& layout, qsizetype offset, const QString& label) {
  const QRectF cursor = layout.cursorRect(offset);
  require(!cursor.isEmpty(), label + QStringLiteral(" cursor rect should exist"));
  require(layout.hitTestTextOffset(QPointF(cursor.left(), cursor.center().y())) == offset,
          label + QStringLiteral(" cursor-left hit-test should round-trip"));
}

void requireTextLayoutCharacterBias(const InlineLayout& layout, qsizetype offset, const QString& label) {
  const QRectF leftCursor = layout.cursorRect(offset);
  const QRectF rightCursor = layout.cursorRect(offset + 1);
  require(!leftCursor.isEmpty() && !rightCursor.isEmpty(), label + QStringLiteral(" bias cursor rects should exist"));
  const qreal left = leftCursor.left();
  const qreal right = rightCursor.left();
  if (qAbs(right - left) < 0.5) {
    return;
  }
  const qreal quarter = left + (right - left) * 0.25;
  const qreal threeQuarter = left + (right - left) * 0.75;
  const qreal y = leftCursor.center().y();
  require(layout.hitTestTextOffset(QPointF(quarter, y)) == offset, label + QStringLiteral(" left half hit-test mismatch"));
  require(layout.hitTestTextOffset(QPointF(threeQuarter, y)) == offset + 1, label + QStringLiteral(" right half hit-test mismatch"));
}

void testInlineLayoutHitTesting() {
  RenderTheme theme = RenderTheme::github();
  QVector<InlineNode> plainInlines;
  plainInlines.push_back(InlineNode::text(QStringLiteral("alpha beta gamma delta epsilon zeta eta theta iota kappa lambda")));

  InlineLayout::BuildOptions probeOptions;
  InlineLayout plain;
  plain.build(plainInlines, theme, 125.0, theme.paragraphFont(), probeOptions);

  const QVector<qsizetype> offsets{0, 1, 5, 6, 12, 20, plain.plainText().size()};
  for (qsizetype offset : offsets) {
    requireTextLayoutCursorRoundTrip(plain, offset, QStringLiteral("plain offset %1").arg(offset));
  }
  requireTextLayoutCharacterBias(plain, 0, QStringLiteral("plain first char"));
  bool testedWrappedBias = false;
  for (qsizetype offset = 1; offset + 1 < plain.plainText().size(); ++offset) {
    const QRectF leftCursor = plain.cursorRect(offset);
    const QRectF rightCursor = plain.cursorRect(offset + 1);
    if (!leftCursor.isEmpty() && !rightCursor.isEmpty() && qAbs(leftCursor.center().y() - rightCursor.center().y()) < 0.5 &&
        qAbs(leftCursor.left() - rightCursor.left()) >= 0.5) {
      requireTextLayoutCharacterBias(plain, offset, QStringLiteral("plain same-line char"));
      testedWrappedBias = true;
      break;
    }
  }
  require(testedWrappedBias, QStringLiteral("inline hit-test should find a same-line bias fixture"));

  const QVector<QRectF> wrappedSelection = plain.selectionRects(0, plain.plainText().size());
  require(wrappedSelection.size() >= 2, QStringLiteral("inline hit-test fixture should wrap across lines"));
  for (const QRectF& rect : wrappedSelection) {
    require(plain.hitTestTextOffset(QPointF(rect.left(), rect.center().y())) >= 0, QStringLiteral("wrapped line start hit should be valid"));
    require(plain.hitTestTextOffset(rect.center()) >= 0, QStringLiteral("wrapped line middle hit should be valid"));
    require(plain.hitTestTextOffset(QPointF(rect.right(), rect.center().y())) >= 0, QStringLiteral("wrapped line end hit should be valid"));
  }

  QVector<InlineNode> activeInlines;
  activeInlines.push_back(InlineNode::text(QStringLiteral("before ")));
  activeInlines.push_back(InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("bold"))}));
  activeInlines.push_back(InlineNode::text(QStringLiteral(" after")));
  InlineLayout::BuildOptions activeOptions = probeOptions;
  activeOptions.projectionState.revealMarkdownMarkers = true;
  InlineLayout active;
  active.build(activeInlines, QStringLiteral("before **bold** after"), theme, 240.0, theme.paragraphFont(), activeOptions);
  requireTextLayoutCursorRoundTrip(active, 7, QStringLiteral("active visible bold start"));
  require(active.hitTestSourceOffset(QPointF(active.cursorRectForSourceOffset(9).left(), active.cursorRectForSourceOffset(9).center().y())) == 9,
          QStringLiteral("active marker source hit-test should not drift"));
}

void testInlineLayoutCursorRects() {
  RenderTheme theme = RenderTheme::github();
  QVector<InlineNode> plainInlines;
  plainInlines.push_back(InlineNode::text(QStringLiteral("alpha beta gamma delta epsilon zeta eta theta iota kappa lambda")));

  InlineLayout layout;
  InlineLayout::BuildOptions options;
  layout.build(plainInlines, theme, 125.0, theme.paragraphFont(), options);

  const QVector<qsizetype> offsets{0, 1, 5, 6, 12, 20, layout.plainText().size()};
  for (qsizetype offset : offsets) {
    const QRectF cursor = layout.cursorRect(offset);
    require(!cursor.isEmpty(), QStringLiteral("inline cursor rect should be non-empty"));
  }

  bool testedMonotonic = false;
  for (qsizetype offset = 0; offset + 3 < layout.plainText().size(); ++offset) {
    const QRectF first = layout.cursorRect(offset);
    const QRectF second = layout.cursorRect(offset + 1);
    const QRectF third = layout.cursorRect(offset + 2);
    if (!first.isEmpty() && !second.isEmpty() && !third.isEmpty() &&
        qAbs(first.center().y() - second.center().y()) < 0.5 && qAbs(second.center().y() - third.center().y()) < 0.5) {
      require(first.left() <= second.left() && second.left() <= third.left(), QStringLiteral("inline cursor x should be monotonic on same line"));
      testedMonotonic = true;
      break;
    }
  }
  require(testedMonotonic, QStringLiteral("inline cursor test should find same-line offsets"));

  bool foundWrap = false;
  for (qsizetype offset = 0; offset + 1 < layout.plainText().size(); ++offset) {
    const QRectF previous = layout.cursorRect(offset);
    const QRectF next = layout.cursorRect(offset + 1);
    if (!previous.isEmpty() && !next.isEmpty() && next.center().y() > previous.center().y() + previous.height() * 0.5) {
      require(next.left() < previous.left(), QStringLiteral("inline cursor x should return toward line start after wrap"));
      foundWrap = true;
      break;
    }
  }
  require(foundWrap, QStringLiteral("inline cursor test should observe a wrapped line"));

  QVector<InlineNode> activeInlines;
  activeInlines.push_back(InlineNode::text(QStringLiteral("before ")));
  activeInlines.push_back(InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("bold"))}));
  activeInlines.push_back(InlineNode::text(QStringLiteral(" after")));
  InlineLayout::BuildOptions activeOptions = options;
  activeOptions.projectionState.revealMarkdownMarkers = true;
  InlineLayout active;
  active.build(activeInlines, QStringLiteral("before **bold** after"), theme, 240.0, theme.paragraphFont(), activeOptions);
  const QRectF markerRect = active.cursorRectForSourceOffset(9);
  require(!markerRect.isEmpty(), QStringLiteral("inline source marker cursor rect should be non-empty"));
  require(active.hitTestSourceOffset(QPointF(markerRect.left(), markerRect.center().y())) == 9,
          QStringLiteral("inline source marker cursor rect should hit marker source offset"));
  const QRectF visibleRect = active.cursorRect(7);
  require(!visibleRect.isEmpty(), QStringLiteral("inline visible active cursor rect should be non-empty"));
  require(qAbs(visibleRect.center().y() - markerRect.center().y()) < qMax(visibleRect.height(), markerRect.height()),
          QStringLiteral("inline visible/source active cursor rects should stay on same line"));
}

void requireValidSelectionRects(const QVector<QRectF>& rects, const QString& label) {
  require(!rects.isEmpty(), label + QStringLiteral(" selection rects should not be empty"));
  for (const QRectF& rect : rects) {
    require(rect.width() > 0.0, label + QStringLiteral(" selection rect width should be positive"));
    require(rect.height() > 0.0, label + QStringLiteral(" selection rect height should be positive"));
  }
}

void testInlineLayoutSelectionRects() {
  RenderTheme theme = RenderTheme::github();

  QVector<InlineNode> shortInlines;
  shortInlines.push_back(InlineNode::text(QStringLiteral("alpha beta")));
  InlineLayout singleLine;
  singleLine.build(shortInlines, theme, 400.0, theme.paragraphFont());
  const QVector<QRectF> single = singleLine.selectionRects(1, 6);
  require(single.size() == 1, QStringLiteral("inline single-line selection should produce one rect"));
  requireValidSelectionRects(single, QStringLiteral("inline single-line"));
  const QVector<QRectF> reverseSingle = singleLine.selectionRects(6, 1);
  require(reverseSingle.size() == single.size(), QStringLiteral("inline reverse selection should keep rect count"));
  require(qAbs(reverseSingle.first().left() - single.first().left()) < 0.5 &&
              qAbs(reverseSingle.first().width() - single.first().width()) < 0.5,
          QStringLiteral("inline reverse selection should match forward rect"));
  require(singleLine.selectionRects(3, 3).isEmpty(), QStringLiteral("inline collapsed selection should return no rects"));

  QVector<InlineNode> wrappedInlines;
  wrappedInlines.push_back(InlineNode::text(QStringLiteral("alpha beta gamma delta epsilon zeta eta theta iota kappa lambda")));
  InlineLayout wrapped;
  wrapped.build(wrappedInlines, theme, 125.0, theme.paragraphFont());
  const QVector<QRectF> wrappedRects = wrapped.selectionRects(0, wrapped.plainText().size());
  require(wrappedRects.size() >= 2, QStringLiteral("inline wrapped selection should span multiple rects"));
  requireValidSelectionRects(wrappedRects, QStringLiteral("inline wrapped"));
  for (qsizetype i = 1; i < wrappedRects.size(); ++i) {
    require(wrappedRects.at(i).top() > wrappedRects.at(i - 1).top(), QStringLiteral("inline wrapped selection rects should move downward"));
  }

  QVector<InlineNode> activeInlines;
  activeInlines.push_back(InlineNode::text(QStringLiteral("before ")));
  activeInlines.push_back(InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("bold"))}));
  activeInlines.push_back(InlineNode::text(QStringLiteral(" after")));
  InlineLayout::BuildOptions activeOptions;
  activeOptions.projectionState.revealMarkdownMarkers = true;
  InlineLayout active;
  active.build(activeInlines, QStringLiteral("before **bold** after"), theme, 240.0, theme.paragraphFont(), activeOptions);
  const QVector<QRectF> visibleContent = active.selectionRects(7, 11);
  require(visibleContent.size() == 1, QStringLiteral("inline active visible content selection should stay single-line"));
  requireValidSelectionRects(visibleContent, QStringLiteral("inline active visible content"));
  const QRectF markerStart = active.cursorRectForSourceOffset(7);
  const QRectF markerEnd = active.cursorRectForSourceOffset(9);
  require(!markerStart.isEmpty() && !markerEnd.isEmpty(), QStringLiteral("inline active marker cursor rects should exist"));
  require(markerEnd.left() > markerStart.left(), QStringLiteral("inline active marker source rect should cover visible marker width"));
}

bool hasRole(const QVector<CodeHighlightSpan>& spans, CodeHighlightRole role) {
  for (const CodeHighlightSpan& span : spans) {
    if (span.role == role) {
      return true;
    }
  }
  return false;
}

const BlockLayout::TableCellLayout* findTableCellLayout(const BlockLayout& tableLayout, NodeId cellId) {
  for (const BlockLayout::TableRowLayout& row : tableLayout.tableRows()) {
    for (const BlockLayout::TableCellLayout& cell : row.cells) {
      if (cell.nodeId == cellId) {
        return &cell;
      }
    }
  }
  return nullptr;
}

int changedPixelCount(const QImage& image, QColor background) {
  int changedPixels = 0;
  const QRgb backgroundRgb = background.rgb();
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      if ((image.pixel(x, y) & 0x00ffffff) != (backgroundRgb & 0x00ffffff)) {
        ++changedPixels;
      }
    }
  }
  return changedPixels;
}

void testDocumentLayoutInlineLayoutContract() {
  DocumentSession session;
  session.setMarkdownText(
      QStringLiteral("alpha `code` beta gamma delta epsilon zeta eta theta\n\n| A | B |\n| --- | --- |\n| `one` | two |"),
      false);

  RenderTheme theme = RenderTheme::github();
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 360.0);

  const MarkdownNode* paragraph = findFirstBlock(session.document().root(), BlockType::Paragraph);
  const MarkdownNode* table = findFirstBlock(session.document().root(), BlockType::Table);
  const MarkdownNode* tableCell = findFirstTableCell(session.document().root());
  require(paragraph != nullptr && table != nullptr && tableCell != nullptr, QStringLiteral("inline layout document fixture missing blocks"));

  const BlockLayout* paragraphLayout = layout.block(paragraph->id());
  const BlockLayout* tableLayout = layout.block(table->id());
  require(paragraphLayout != nullptr && paragraphLayout->inlineLayout() != nullptr, QStringLiteral("inline layout paragraph layout missing"));
  require(tableLayout != nullptr && tableLayout->type() == BlockType::Table, QStringLiteral("inline layout table layout missing"));
  const BlockLayout::TableCellLayout* cellLayout = findTableCellLayout(*tableLayout, tableCell->id());
  require(cellLayout != nullptr, QStringLiteral("inline layout table cell layout missing"));

  require(paragraphLayout->inlineLayout()->height() > 0.0, QStringLiteral("paragraph inline layout should have height"));
  require(paragraphLayout->inlineLayout()->size().height() == paragraphLayout->inlineLayout()->height(),
          QStringLiteral("paragraph inline layout size should expose height"));
  require(cellLayout->text.height() > 0.0, QStringLiteral("table cell inline layout should have height"));
  require(cellLayout->text.size().height() == cellLayout->text.height(), QStringLiteral("table cell inline layout size should expose height"));
  require(!cellLayout->text.displayText().contains(QLatin1Char('|')), QStringLiteral("table cell layout should hide markdown pipe separators"));

  QImage image(QSize(420, qCeil(layout.totalHeight()) + 20), QImage::Format_ARGB32);
  image.fill(theme.backgroundColor());
  QPainter painter(&image);
  for (const auto& block : layout.blocks()) {
    block->paint(painter, theme, 0.0);
  }
  painter.end();
  require(changedPixelCount(image, theme.backgroundColor()) > 100, QStringLiteral("document inline layout paint should draw visible pixels"));

  const HitTestResult paragraphHit = layout.hitTest(paragraphLayout->rect().center(), theme);
  require(paragraphHit.isValid() && paragraphHit.zone == HitTestResult::Zone::Text,
          QStringLiteral("document inline layout paragraph hit-test should hit text"));
  require(!paragraphLayout->selectionRectsForOffsets(0, 5, theme).isEmpty(),
          QStringLiteral("document inline layout paragraph selection should be available"));

  const HitTestResult tableHit = layout.hitTest(cellLayout->rect.center(), theme);
  require(tableHit.isValid() && tableHit.zone == HitTestResult::Zone::TableCell,
          QStringLiteral("document inline layout table hit-test should hit table cell"));
  require(!tableLayout->selectionRectsForOffsets(0, 1, theme).isEmpty(),
          QStringLiteral("document inline layout table selection should be available"));
}

bool hasExactRoleSpan(const QVector<CodeHighlightSpan>& spans, CodeHighlightRole role, qsizetype start, qsizetype end) {
  for (const CodeHighlightSpan& span : spans) {
    if (span.role == role && span.start == start && span.end == end) {
      return true;
    }
  }
  return false;
}

void testTreeSitterCodeHighlighting() {
  TreeSitterHighlighter highlighter;
  require(highlighter.supportsLanguage(QStringLiteral("python")), QStringLiteral("python highlighting should be registered"));
  const QVector<CodeHighlightSpan> python = highlighter.highlight(
      QStringLiteral("python"),
      QStringLiteral("def greet(name):\n    return \"hello \" + name\n"));
  require(hasRole(python, CodeHighlightRole::Keyword), QStringLiteral("python should highlight keywords"));
  require(hasRole(python, CodeHighlightRole::Function), QStringLiteral("python should highlight functions"));
  require(hasRole(python, CodeHighlightRole::String), QStringLiteral("python should highlight strings"));

  const QVector<CodeHighlightSpan> cpp = highlighter.highlight(
      QStringLiteral("cpp"),
      QStringLiteral("#include <QApplication>\nint main() { return 0; }\n"));
  require(hasRole(cpp, CodeHighlightRole::Preprocessor), QStringLiteral("cpp should highlight preprocessor"));
  require(hasRole(cpp, CodeHighlightRole::Keyword), QStringLiteral("cpp should highlight keywords"));
  require(hasRole(cpp, CodeHighlightRole::Function), QStringLiteral("cpp should highlight functions"));
  const QString cppText = QStringLiteral("#include <QApplication>\nint main() { return 0; }\n");
  const QVector<CodeHighlightSpan> cppExact = highlighter.highlight(QStringLiteral("cpp"), cppText);
  const qsizetype returnStart = cppText.indexOf(QStringLiteral("return"));
  require(hasExactRoleSpan(cppExact, CodeHighlightRole::Keyword, returnStart, returnStart + 6),
          QStringLiteral("cpp keyword span should align exactly with return"));
}

void testLayoutForTheme(const MarkdownDocument& document, const RenderTheme& theme, const QString& themeName) {
  DocumentLayout layout;
  layout.rebuild(document, theme, 1000.0);

  require(layout.pageWidth() > 300.0, QStringLiteral("%1 page width too small").arg(themeName));
  require(layout.totalHeight() > 700.0, QStringLiteral("%1 total height should exceed smoke viewport").arg(themeName));
  require(!layout.blocks().empty(), QStringLiteral("%1 layout produced no blocks").arg(themeName));

  const MarkdownNode* heading = findFirstBlock(document.root(), BlockType::Heading);
  const MarkdownNode* paragraph = findFirstBlock(document.root(), BlockType::Paragraph);
  const MarkdownNode* table = findFirstBlock(document.root(), BlockType::Table);
  const MarkdownNode* tableCell = findFirstTableCell(document.root());
  const MarkdownNode* code = findBlockWithLiteral(document.root(), BlockType::CodeFence, QStringLiteral("return 0"));
  const MarkdownNode* math = findBlockWithLiteral(document.root(), BlockType::MathBlock, QStringLiteral("E = mc"));

  require(heading != nullptr, QStringLiteral("Heading node missing"));
  require(paragraph != nullptr, QStringLiteral("Paragraph node missing"));
  require(table != nullptr, QStringLiteral("Table node missing"));
  require(tableCell != nullptr, QStringLiteral("Table cell node missing"));
  require(code != nullptr, QStringLiteral("Code node missing"));
  require(math != nullptr, QStringLiteral("Math node missing"));

  requireUsableRect(layout.block(heading->id())->rect(), QStringLiteral("%1 heading").arg(themeName));
  requireUsableRect(layout.block(paragraph->id())->rect(), QStringLiteral("%1 paragraph").arg(themeName));
  requireUsableRect(layout.block(table->id())->rect(), QStringLiteral("%1 table").arg(themeName));
  requireUsableRect(layout.block(code->id())->rect(), QStringLiteral("%1 code").arg(themeName));
  requireUsableRect(layout.block(math->id())->rect(), QStringLiteral("%1 math").arg(themeName));
  require(!layout.block(code->id())->literal().endsWith(QLatin1Char('\n')),
          QStringLiteral("%1 code display literal should hide structural trailing newline").arg(themeName));
  require(!layout.block(code->id())->codeHighlightSpans().isEmpty(),
          QStringLiteral("%1 code block should have syntax highlight spans").arg(themeName));
  require(layout.block(math->id())->rect().height() >= 20.0,
          QStringLiteral("%1 math block height should leave room for displayed text").arg(themeName));

  const QRectF paragraphRect = layout.block(paragraph->id())->rect();
  const HitTestResult paragraphHit = layout.hitTest(paragraphRect.center(), theme);
  require(paragraphHit.isValid(), QStringLiteral("%1 paragraph hit invalid").arg(themeName));
  require(paragraphHit.zone == HitTestResult::Zone::Text, QStringLiteral("%1 paragraph hit should be text").arg(themeName));
  require(paragraphHit.blockId == paragraph->id(), QStringLiteral("%1 paragraph hit id mismatch").arg(themeName));

  const QRectF tableRect = layout.block(table->id())->rect();
  const HitTestResult tableHit = layout.hitTest(tableRect.center(), theme);
  require(tableHit.isValid(), QStringLiteral("%1 table hit invalid").arg(themeName));
  require(tableHit.zone == HitTestResult::Zone::TableCell, QStringLiteral("%1 table hit should be cell").arg(themeName));
  require(tableHit.tableRow >= 0 && tableHit.tableColumn >= 0, QStringLiteral("%1 table hit indices missing").arg(themeName));

  const QRectF codeRect = layout.block(code->id())->rect();
  const HitTestResult codeHit = layout.hitTest(codeRect.center(), theme);
  require(codeHit.isValid(), QStringLiteral("%1 code hit invalid").arg(themeName));
  require(codeHit.zone == HitTestResult::Zone::Code, QStringLiteral("%1 code hit should be code").arg(themeName));
  SelectionRange codeSelection;
  codeSelection.anchor.blockId = code->id();
  codeSelection.anchor.text.nodeId = code->id();
  codeSelection.anchor.text.textOffset = 0;
  codeSelection.focus.blockId = code->id();
  codeSelection.focus.text.nodeId = code->id();
  codeSelection.focus.text.textOffset = 5;
  require(!layout.block(code->id())->selectionRects(codeSelection, theme).isEmpty(),
          QStringLiteral("%1 code selection rects should not be empty").arg(themeName));
  SelectionRange crossSelection;
  crossSelection.anchor.blockId = paragraph->id();
  crossSelection.anchor.text.nodeId = paragraph->id();
  crossSelection.anchor.text.textOffset = 0;
  crossSelection.focus.blockId = code->id();
  crossSelection.focus.text.nodeId = code->id();
  crossSelection.focus.text.textOffset = 5;
  require(!layout.block(code->id())->selectionRectsForOffsets(0, 5, theme).isEmpty(),
          QStringLiteral("%1 code cross-block selection rects should not be empty").arg(themeName));
  require(!layout.block(table->id())->selectionRectsForOffsets(0, 1, theme).isEmpty(),
          QStringLiteral("%1 table cross-block selection rects should not be empty").arg(themeName));

  const QRectF mathRect = layout.block(math->id())->rect();
  const HitTestResult mathHit = layout.hitTest(mathRect.center(), theme);
  require(mathHit.isValid(), QStringLiteral("%1 math hit invalid").arg(themeName));
  require(mathHit.zone == HitTestResult::Zone::Math, QStringLiteral("%1 math hit should be math").arg(themeName));
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", qgetenv("QT_QPA_PLATFORM").isEmpty() ? QByteArray("offscreen") : qgetenv("QT_QPA_PLATFORM"));
  QApplication app(argc, argv);
  require(argc >= 2, QStringLiteral("Fixture path argument is required"));

  const QString markdown = readFixture(QString::fromLocal8Bit(argv[1]));
  CmarkGfmParser parser;
  ParseOptions options;
  ParseResult parsed = parser.parseDocument(markdown, options);
  require(parsed.root != nullptr, QStringLiteral("Parser returned null root"));

  MarkdownDocument document;
  document.setMarkdownText(markdown, std::move(parsed.root));

  testInlineMarkerExpansion();
  testInlineProjectionContract();
  testInlineLayoutGeometryContract();
  testInlineLayoutPainting();
  testInlineLayoutHitTesting();
  testInlineLayoutCursorRects();
  testInlineLayoutSelectionRects();
  testIncrementalBlockRebuildContract();
  testDocumentLayoutInlineLayoutContract();
  testTreeSitterCodeHighlighting();
  testLayoutForTheme(document, RenderTheme::github(), QStringLiteral("github"));
  testLayoutForTheme(document, RenderTheme::newsprint(), QStringLiteral("newsprint"));
  testLayoutForTheme(document, RenderTheme::night(), QStringLiteral("night"));
  return 0;
}
