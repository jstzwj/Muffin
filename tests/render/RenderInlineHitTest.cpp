#include "render/InlineLayout.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QDebug>

#include <functional>
#include <iostream>

#include "RenderTestUtils.h"

using namespace muffin;

namespace {

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
  const QVector<QRectF> openerMarker = active.selectionRectsForSourceOffsets(7, 9);
  require(openerMarker.size() == 1, QStringLiteral("inline active marker source selection should stay single-line"));
  requireValidSelectionRects(openerMarker, QStringLiteral("inline active marker source"));
  require(qAbs(openerMarker.first().left() - markerStart.left()) < 0.5,
          QStringLiteral("inline active marker source selection should start at opener marker"));
  require(openerMarker.first().right() >= markerEnd.left() - 0.5,
          QStringLiteral("inline active marker source selection should cover opener marker width"));
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testInlineLayoutHitTesting);
  RUN_TEST(testInlineLayoutCursorRects);
  RUN_TEST(testInlineLayoutSelectionRects);
#undef RUN_TEST
  return 0;
}
