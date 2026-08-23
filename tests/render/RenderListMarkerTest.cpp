#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "render/DocumentLayout.h"
#include "theme/CssThemeMapper.h"
#include "theme/RenderTheme.h"
#include "theme/ThemeDefinition.h"

#include <QApplication>
#include <QDebug>
#include <QImage>
#include <QPainter>

#include <functional>
#include <iostream>

#include "RenderTestUtils.h"

using namespace muffin;

namespace {

bool hasTealPixel(const QImage& image, const QRect& area) {
  const QRect clipped = area.intersected(image.rect());
  for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
    for (int x = clipped.left(); x <= clipped.right(); ++x) {
      const QColor c = QColor::fromRgba(image.pixel(x, y));
      if (c.green() > 120 && c.blue() > 120 && c.red() < 90) { return true; }
    }
  }
  return false;
}

void testListMarkerColorPaintsFromCssMarker() {
  const QString css = QStringLiteral(
      "#write { color:#000000; }"
      "#write li::marker { color:#3db8bf; }");
  const ThemeDefinition def = CssThemeMapper::fromCss(css, QStringLiteral("marker"), QString());
  const RenderTheme theme = RenderTheme::fromDefinition(def);
  require(theme.listMarkerColor().name(QColor::HexRgb) == QStringLiteral("#3db8bf"),
          QStringLiteral("theme marker colour should come from li::marker"));

  DocumentSession session;
  session.setMarkdownText(QStringLiteral("- marker colour\n"), false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  QVector<const MarkdownNode*> items;
  collectListItems(session.document().root(), items);
  require(items.size() == 1, QStringLiteral("fixture should parse one list item"));
  const BlockLayout* block = layout.block(items.first()->id());
  require(block != nullptr, QStringLiteral("list item block should be promoted"));
  const QRectF rect = block->rect();
  QImage image(int(rect.width()), int(rect.height()), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::white);
  QPainter painter(&image);
  painter.translate(-rect.left(), -rect.top());
  block->paint(painter, theme, 0.0, nullptr);
  painter.end();
  const QRect markerArea(0, 0, qMax(1, int(theme.listIndent())), image.height());
  require(hasTealPixel(image, markerArea), QStringLiteral("marker gutter should contain the CSS marker colour"));
}

void testUnorderedListMarkerKindsByDepth() {
  DocumentSession session;
  session.setMarkdownText(
      QStringLiteral("- level one\n"
                     "    - level two\n"
                     "        - level three\n"
                     "            - level four\n"
                     "\n"
                     "1. ordered one\n"
                     "    - ordered child bullet"),
      false);

  RenderTheme theme = RenderTheme::github();
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);

  QVector<const MarkdownNode*> items;
  collectListItems(session.document().root(), items);
  require(items.size() == 6, QStringLiteral("list marker fixture should parse six list items"));
  require(layout.block(items.at(0)->id())->listMarkerKind() == BlockLayout::ListMarkerKind::BulletDisc,
          QStringLiteral("first unordered level should use a filled disc marker"));
  require(layout.block(items.at(1)->id())->listMarkerKind() == BlockLayout::ListMarkerKind::BulletCircle,
          QStringLiteral("second unordered level should use a hollow circle marker"));
  require(layout.block(items.at(2)->id())->listMarkerKind() == BlockLayout::ListMarkerKind::BulletSquare,
          QStringLiteral("third unordered level should use a filled square marker"));
  require(layout.block(items.at(3)->id())->listMarkerKind() == BlockLayout::ListMarkerKind::BulletSquare,
          QStringLiteral("fourth unordered level should continue using a filled square marker"));
  require(layout.block(items.at(4)->id())->listMarkerKind() == BlockLayout::ListMarkerKind::OrderedText,
          QStringLiteral("ordered list marker should remain text"));
  require(layout.block(items.at(5)->id())->listMarkerKind() == BlockLayout::ListMarkerKind::BulletDisc,
          QStringLiteral("unordered child inside ordered list should start its own bullet depth"));
}

// CSS `list-style-type` overrides the legacy Arabic "1." marker. lower-roman →
// "i."/"ii."/"iii.", and `none` suppresses the marker entirely. Inherited, so a
// value on `ol` reaches every `li`.
void testListStyleTypeFormatsMarker() {
  const QString css = QStringLiteral(
      "#write { color:#000000; }"
      "#write ol { list-style-type: lower-roman; }");
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("lst"), QString()));
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("1. first\n2. second\n3. third\n"), false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  QVector<const MarkdownNode*> items;
  collectListItems(session.document().root(), items);
  require(items.size() == 3, QStringLiteral("three ordered items"));
  require(layout.block(items.at(0)->id())->listMarker() == QStringLiteral("i."),
          QStringLiteral("lower-roman first marker 'i.' (got '%1')").arg(layout.block(items.at(0)->id())->listMarker()));
  require(layout.block(items.at(1)->id())->listMarker() == QStringLiteral("ii."),
          QStringLiteral("lower-roman second marker 'ii.' (got '%1')").arg(layout.block(items.at(1)->id())->listMarker()));
  require(layout.block(items.at(2)->id())->listMarker() == QStringLiteral("iii."),
          QStringLiteral("lower-roman third marker 'iii.' (got '%1')").arg(layout.block(items.at(2)->id())->listMarker()));

  // `none` ⇒ no marker kind at all.
  const QString noneCss = QStringLiteral("#write { color:#000000; } #write ul { list-style-type: none; }");
  const RenderTheme noneTheme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(noneCss, QStringLiteral("n"), QString()));
  DocumentSession ns;
  ns.setMarkdownText(QStringLiteral("- a\n- b\n"), false);
  DocumentLayout nl;
  nl.rebuild(ns.document(), noneTheme, 800.0);
  QVector<const MarkdownNode*> nitems;
  collectListItems(ns.document().root(), nitems);
  require(nl.block(nitems.at(0)->id())->listMarkerKind() == BlockLayout::ListMarkerKind::None,
          QStringLiteral("list-style-type: none ⇒ no marker"));
}

// CSS counters in `li::marker` content: `counter(list-item)` resolves to the
// item's position, with literal text and a style both honoured. Content wins over
// list-style-type (author owns the format).
void testCounterInMarkerContent() {
  const QString css = QStringLiteral(
      "#write { color:#000000; }"
      "#write ol { list-style-type: lower-roman; }"
      "li::marker { content: \"(\" counter(list-item) \")\"; }");
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("ctr"), QString()));
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("1. first\n2. second\n"), false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  QVector<const MarkdownNode*> items;
  collectListItems(session.document().root(), items);
  require(items.size() == 2, QStringLiteral("two ordered items"));
  require(layout.block(items.at(0)->id())->listMarker() == QStringLiteral("(1)"),
          QStringLiteral("content '(':counter:')' first marker '(1)' (got '%1')").arg(layout.block(items.at(0)->id())->listMarker()));
  require(layout.block(items.at(1)->id())->listMarker() == QStringLiteral("(2)"),
          QStringLiteral("second marker '(2)' (got '%1')").arg(layout.block(items.at(1)->id())->listMarker()));

  // counter(list-item, lower-roman) formats the value with the given style.
  const QString css2 = QStringLiteral("#write { color:#000000; } li::marker { content: counter(list-item, lower-roman) \".\"; }");
  const RenderTheme theme2 = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css2, QStringLiteral("ctr2"), QString()));
  DocumentSession s2;
  s2.setMarkdownText(QStringLiteral("1. a\n2. b\n3. c\n"), false);
  DocumentLayout l2;
  l2.rebuild(s2.document(), theme2, 800.0);
  QVector<const MarkdownNode*> it2;
  collectListItems(s2.document().root(), it2);
  require(l2.block(it2.at(2)->id())->listMarker() == QStringLiteral("iii."),
          QStringLiteral("counter(list-item, lower-roman) third marker 'iii.' (got '%1')").arg(l2.block(it2.at(2)->id())->listMarker()));
}

// The nested-list guide line's X must follow the CSS `left` (relative to the li's
// left edge), NOT be pinned to the marker column. With `left:5px` the guide
// paints at image x≈5 (rect.left()+5 in document space), well left of the marker
// column (listIndent()*0.45 ≈ 13px at the default 30px indent). A regression that
// ignores guide.leftOffset paints it at the marker column instead. The guide
// colour (#3db8bf) is distinct from the black marker/text, so its pixels are
// unambiguous and the assertion is font-metric independent (the guide X is pure
// geometry: rect.left() + leftOffset).
void testListGuideLineHonorsCssLeftOffset() {
  const QString css = QStringLiteral(
      "#write { color:#000000; }"
      "li::before { content:''; border-left:2px solid #3db8bf; left:5px; }");
  const ThemeDefinition def = CssThemeMapper::fromCss(css, QStringLiteral("guide"), QString());
  const RenderTheme theme = RenderTheme::fromDefinition(def);
  require(theme.listGuide().present, QStringLiteral("li::before border-left should produce a guide"));
  require(qAbs(theme.listGuide().leftOffset - 5.0) < 0.5, QStringLiteral("guide left captured as 5px"));

  DocumentSession session;
  session.setMarkdownText(QStringLiteral("- guide item\n"), false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  QVector<const MarkdownNode*> items;
  collectListItems(session.document().root(), items);
  require(items.size() == 1, QStringLiteral("fixture should parse one list item"));
  const BlockLayout* block = layout.block(items.first()->id());
  require(block != nullptr, QStringLiteral("list item block should be promoted"));
  const QRectF rect = block->rect();
  QImage image(int(rect.width()), int(rect.height()), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::white);
  QPainter painter(&image);
  painter.translate(-rect.left(), -rect.top());
  block->paint(painter, theme, 0.0, nullptr);
  painter.end();

  const qreal markerColumnX = theme.listIndent() * 0.45;  // where the pre-fix code drew the guide
  int leftmostTeal = -1;
  for (int x = 0; x < image.width() && leftmostTeal < 0; ++x) {
    for (int y = 0; y < image.height(); ++y) {
      const QColor c = QColor::fromRgba(image.pixel(x, y));
      if (c.green() > 120 && c.blue() > 120 && c.red() < 90) { leftmostTeal = x; break; }
    }
  }
  require(leftmostTeal >= 0, QStringLiteral("guide line should be painted in its border-left colour"));
  require(qAbs(leftmostTeal - 5) <= 2,
          QStringLiteral("guide line at CSS left:5px (image x≈5), honouring guide.leftOffset"));
  require(qAbs(leftmostTeal - markerColumnX) > 2,
          QStringLiteral("guide must not be pinned to the marker column (the pre-fix behaviour)"));
}

// Display markers follow the AUTHORED source numbers, not positional index. A blank-line split
// between same-marker items parses as ONE loose list (cmark), so positional numbering rendered a
// source-authored "1." after the split as "3.". Authored delimiters ("1)") are preserved too.
void testAuthoredNumbersInSplitLooseList() {
  DocumentSession session;
  session.setMarkdownText(
      QStringLiteral("1. First step\n2. Second step\n\n\n1. Third step\n   1. Nested one\n   2. Nested two"), false);
  RenderTheme theme = RenderTheme::github();
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  QVector<const MarkdownNode*> items;
  collectListItems(session.document().root(), items);
  require(items.size() == 5, QStringLiteral("split loose list should parse five items (got %1)").arg(items.size()));
  const auto markerAt = [&layout, &items](int index) {
    return layout.block(items.at(index)->id())->listMarker();
  };
  require(markerAt(0) == QStringLiteral("1."), QStringLiteral("first marker '1.' (got '%1')").arg(markerAt(0)));
  require(markerAt(1) == QStringLiteral("2."), QStringLiteral("second marker '2.' (got '%1')").arg(markerAt(1)));
  require(markerAt(2) == QStringLiteral("1."),
          QStringLiteral("authored restart after the blank-line split must display '1.', not positional '3.' (got '%1')").arg(markerAt(2)));
  require(markerAt(3) == QStringLiteral("1."), QStringLiteral("nested first marker '1.' (got '%1')").arg(markerAt(3)));
  require(markerAt(4) == QStringLiteral("2."), QStringLiteral("nested second marker '2.' (got '%1')").arg(markerAt(4)));

  DocumentSession parenSession;
  parenSession.setMarkdownText(QStringLiteral("1) a\n2) b"), false);
  DocumentLayout parenLayout;
  parenLayout.rebuild(parenSession.document(), theme, 800.0);
  QVector<const MarkdownNode*> parenItems;
  collectListItems(parenSession.document().root(), parenItems);
  require(parenItems.size() == 2, QStringLiteral("paren list should parse two items"));
  require(parenLayout.block(parenItems.at(0)->id())->listMarker() == QStringLiteral("1)"),
          QStringLiteral("authored paren delimiter must be preserved (got '%1')").arg(parenLayout.block(parenItems.at(0)->id())->listMarker()));
  require(parenLayout.block(parenItems.at(1)->id())->listMarker() == QStringLiteral("2)"),
          QStringLiteral("second paren marker '2)' (got '%1')").arg(parenLayout.block(parenItems.at(1)->id())->listMarker()));
}

// The authored split (two blank lines — what the editor's Enter-Enter writes) renders a
// top-level-VEP-sized gap between the items so the display reads as TWO lists; cmark still
// parses one loose list. One blank line (loose list) keeps normal inter-item spacing.
void testAuthoredListSplitGap() {
  const RenderTheme theme = RenderTheme::github();
  const qreal splitGap = QFontMetricsF(theme.paragraphFont()).height() + theme.blockSpacing();

  DocumentSession session;
  session.setMarkdownText(QStringLiteral("1. First\n2. Second\n\n\n1. Third"), false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  QVector<const MarkdownNode*> items;
  collectListItems(session.document().root(), items);
  require(items.size() == 3, QStringLiteral("split fixture should parse three items"));
  const auto topOf = [&layout, &items](int index) { return layout.block(items.at(index)->id())->rect().top(); };
  const qreal inListGap = topOf(1) - topOf(0);
  const qreal splitListGap = topOf(2) - topOf(1);
  require(splitListGap - inListGap >= splitGap - 1.0,
          QStringLiteral("two blank lines must render a list-break gap (+%1, got +%2)")
              .arg(splitGap, 0, 'f', 1)
              .arg(splitListGap - inListGap, 0, 'f', 1));

  // The split blank line is a REAL block: a zero-width virtual empty paragraph synthesized
  // between the items (so the caret can land on it at normal size and typing works — text at
  // column 0 interrupts the list in cmark). It renders as its own paragraph-height block.
  const MarkdownNode* list = items.first()->parent();
  const MarkdownNode* splitVep = nullptr;
  for (const auto& child : list->children()) {
    const SourceRange range = child->sourceRange();
    if (child->type() == BlockType::Paragraph && range.byteEnd == range.byteStart) {
      require(splitVep == nullptr, QStringLiteral("only one split VEP should be synthesized"));
      splitVep = child.get();
    }
  }
  require(splitVep != nullptr, QStringLiteral("two blank lines must synthesize a VEP between the items"));
  require(splitVep->sourceRange().byteStart > items.at(1)->children().front()->sourceRange().byteEnd &&
              splitVep->sourceRange().byteStart <= items.at(2)->sourceRange().byteStart,
          QStringLiteral("split VEP must sit in the blank gap between item 2 and item 3"));
  const BlockLayout* vepBlock = layout.block(splitVep->id());
  require(vepBlock != nullptr, QStringLiteral("split VEP must have a rendered block"));
  require(qAbs(vepBlock->rect().height() - layout.block(items.at(0)->id())->rect().height()) < 2.0,
          QStringLiteral("split VEP block must be one line tall (got %1)")
              .arg(vepBlock->rect().height(), 0, 'f', 1));

  // One blank line = loose list: inter-item spacing only, no split gap.
  DocumentSession looseSession;
  looseSession.setMarkdownText(QStringLiteral("1. First\n2. Second\n\n1. Third"), false);
  DocumentLayout looseLayout;
  looseLayout.rebuild(looseSession.document(), theme, 800.0);
  QVector<const MarkdownNode*> looseItems;
  collectListItems(looseSession.document().root(), looseItems);
  require(looseItems.size() == 3, QStringLiteral("loose fixture should parse three items"));
  const auto looseTop = [&looseLayout, &looseItems](int index) {
    return looseLayout.block(looseItems.at(index)->id())->rect().top();
  };
  const qreal looseGap = looseTop(2) - looseTop(1);
  require(looseGap < looseTop(1) - looseTop(0) + splitGap * 0.5,
          QStringLiteral("one blank line must NOT render a list-break gap"));
  int looseVepCount = 0;
  for (const auto& child : looseItems.first()->parent()->children()) {
    const SourceRange range = child->sourceRange();
    if (child->type() == BlockType::Paragraph && range.byteEnd == range.byteStart) { ++looseVepCount; }
  }
  require(looseVepCount == 0, QStringLiteral("one blank line must not synthesize a VEP"));
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testUnorderedListMarkerKindsByDepth);
  RUN_TEST(testListMarkerColorPaintsFromCssMarker);
  RUN_TEST(testListStyleTypeFormatsMarker);
  RUN_TEST(testCounterInMarkerContent);
  RUN_TEST(testListGuideLineHonorsCssLeftOffset);
  RUN_TEST(testAuthoredNumbersInSplitLooseList);
  RUN_TEST(testAuthoredListSplitGap);
#undef RUN_TEST
  return 0;
}
