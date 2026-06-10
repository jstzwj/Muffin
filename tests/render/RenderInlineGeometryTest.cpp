#include "render/InlineLayout.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QDebug>
#include <QImage>
#include <QPainter>
#include <QTextLayout>

#include <functional>
#include <iostream>

#include "RenderTestUtils.h"

using namespace muffin;

namespace {

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

void testInlineLayoutZeroWidthTabIndentGeometry() {
  RenderTheme theme = RenderTheme::github();
  QVector<InlineNode> inlines;
  inlines.push_back(InlineNode::text(QStringLiteral("​alpha")));

  InlineLayout layout;
  layout.build(inlines, theme, 400.0, theme.paragraphFont());
  require(layout.displayText() == QStringLiteral("​alpha"), QStringLiteral("zero-width tab source should remain in display text"));

  const qreal actualIndent = layout.cursorRect(1).left() - layout.cursorRect(0).left();
  const qreal expectedIndent = QFontMetricsF(theme.paragraphFont()).horizontalAdvance(QStringLiteral("汉汉"));
  require(qAbs(actualIndent - expectedIndent) < 1.0,
          QStringLiteral("zero-width tab indent should render as two CJK characters, actual=%1 expected=%2").arg(actualIndent).arg(expectedIndent));
  require(actualIndent < QFontMetricsF(theme.paragraphFont()).horizontalAdvance(QStringLiteral("汉汉汉")),
          QStringLiteral("zero-width tab indent should not expand to a wide editor tab stop"));
}

void testInlineLayoutStyleFormats() {
  RenderTheme theme = RenderTheme::github();
  QVector<InlineNode> inlines;
  inlines.push_back(InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("bold"))}));
  inlines.push_back(InlineNode::text(QStringLiteral(" ")));
  inlines.push_back(InlineNode::emphasis(QStringLiteral("*"), {InlineNode::text(QStringLiteral("em"))}));
  inlines.push_back(InlineNode::text(QStringLiteral(" ")));
  inlines.push_back(InlineNode::strikethrough(QStringLiteral("~~"), {InlineNode::text(QStringLiteral("gone"))}));
  inlines.push_back(InlineNode::text(QStringLiteral(" ")));
  inlines.push_back(InlineNode::strong(
      QStringLiteral("**"),
      {InlineNode::emphasis(QStringLiteral("*"), {InlineNode::text(QStringLiteral("both"))})}));

  InlineLayout layout;
  layout.build(inlines, QStringLiteral("**bold** *em* ~~gone~~ ***both***"), theme, 500.0, theme.paragraphFont(), InlineLayout::BuildOptions{});
  require(layout.displayText() == QStringLiteral("bold em gone both"), QStringLiteral("style format fixture display text mismatch"));

  const QVector<QTextLayout::FormatRange> formats = layout.debugTextFormats(theme, theme.paragraphFont());
  auto formatAt = [&formats](int offset) -> QTextCharFormat {
    QTextCharFormat result;
    for (const QTextLayout::FormatRange& range : formats) {
      if (offset >= range.start && offset < range.start + range.length) {
        result = range.format;
      }
    }
    return result;
  };

  require(formatAt(0).fontWeight() >= QFont::Bold, QStringLiteral("strong text should use bold font weight"));
  require(formatAt(5).fontItalic(), QStringLiteral("emphasis text should use italic font"));
  require(formatAt(8).fontStrikeOut(), QStringLiteral("strikethrough text should use strikeout font"));
  require(formatAt(13).fontWeight() >= QFont::Bold && formatAt(13).fontItalic(),
          QStringLiteral("nested strong/emphasis text should combine style flags"));
}

void testInlineHtmlKeyboardLayoutContract() {
  RenderTheme theme = RenderTheme::github();
  QVector<InlineNode> inlines;

  InlineNode openCtrl(InlineType::HtmlInline);
  openCtrl.setText(QStringLiteral("<kbd>"));
  InlineNode ctrlText = InlineNode::text(QStringLiteral("Ctrl"));
  InlineNode closeCtrl(InlineType::HtmlInline);
  closeCtrl.setText(QStringLiteral("</kbd>"));
  InlineNode plus = InlineNode::text(QStringLiteral("+"));
  InlineNode openC(InlineType::HtmlInline);
  openC.setText(QStringLiteral("<kbd>"));
  InlineNode cText = InlineNode::text(QStringLiteral("C"));
  InlineNode closeC(InlineType::HtmlInline);
  closeC.setText(QStringLiteral("</kbd>"));

  inlines << openCtrl << ctrlText << closeCtrl << plus << openC << cText << closeC;

  InlineLayout layout;
  layout.build(
      inlines,
      QStringLiteral("<kbd>Ctrl</kbd>+<kbd>C</kbd>"),
      theme,
      500.0,
      theme.paragraphFont(),
      InlineLayout::BuildOptions{});

  require(layout.displayText() == QStringLiteral("Ctrl+C"),
          QStringLiteral("inline html kbd fixture should collapse to visible keyboard chord"));

  const QVector<QTextLayout::FormatRange> formats = layout.debugTextFormats(theme, theme.paragraphFont());
  auto formatAt = [&formats](int offset) -> QTextCharFormat {
    QTextCharFormat result;
    for (const QTextLayout::FormatRange& range : formats) {
      if (offset >= range.start && offset < range.start + range.length) {
        result = range.format;
      }
    }
    return result;
  };

  require(formatAt(0).fontFamilies().toStringList().contains(QStringLiteral("Courier New")),
          QStringLiteral("first kbd segment should use monospace keyboard font"));
  require(formatAt(5).fontFamilies().toStringList().contains(QStringLiteral("Courier New")),
          QStringLiteral("last kbd segment should map even at end of html fragment"));
  require(!formatAt(4).fontFamilies().toStringList().contains(QStringLiteral("Courier New")),
          QStringLiteral("keyboard format should not bleed into separator"));
}

void testInlineLayoutProjectionDisplayMappingAfterCollapsedMath() {
  RenderTheme theme = RenderTheme::github();
  QVector<InlineNode> inlines;
  inlines.push_back(InlineNode::text(QStringLiteral("math ")));
  inlines.push_back(InlineNode::inlineMath(QStringLiteral("E = mc^2")));
  inlines.push_back(InlineNode::text(QStringLiteral(" then ")));
  inlines.push_back(InlineNode::code(QStringLiteral("code")));
  inlines.push_back(InlineNode::text(QStringLiteral(" and ")));
  inlines.push_back(InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("bold"))}));

  InlineLayout layout;
  layout.build(
      inlines,
      QStringLiteral("math $E = mc^2$ then `code` and **bold**"),
      theme,
      500.0,
      theme.paragraphFont(),
      InlineLayout::BuildOptions{});
  require(layout.mathAtomCount() == 1, QStringLiteral("mapping fixture should collapse first inline math"));
  require(layout.displayText().contains(QStringLiteral(" then code and bold")),
          QStringLiteral("mapping fixture should keep text after collapsed math"));

  const qsizetype codeStart = layout.displayText().indexOf(QStringLiteral("code"));
  const qsizetype boldStart = layout.displayText().indexOf(QStringLiteral("bold"));
  require(codeStart >= 0 && boldStart > codeStart, QStringLiteral("mapping fixture display offsets should be discoverable"));

  const QVector<QTextLayout::FormatRange> formats = layout.debugTextFormats(theme, theme.paragraphFont());
  auto formatAt = [&formats](int offset) -> QTextCharFormat {
    QTextCharFormat result;
    for (const QTextLayout::FormatRange& range : formats) {
      if (offset >= range.start && offset < range.start + range.length) {
        result = range.format;
      }
    }
    return result;
  };

  require(formatAt(static_cast<int>(codeStart)).fontFamilies().toStringList().first() == theme.codeFont().family(),
          QStringLiteral("code format after collapsed math should use rebuilt display offset"));
  require(formatAt(static_cast<int>(boldStart)).fontWeight() >= QFont::Bold,
          QStringLiteral("strong format after collapsed math should use rebuilt display offset"));

  const QVector<QRectF> codeSelection =
      layout.selectionRectsForSourceOffsets(QStringLiteral("math $E = mc^2$ then `").size(),
                                            QStringLiteral("math $E = mc^2$ then `code").size());
  require(codeSelection.size() == 1, QStringLiteral("source selection after collapsed math should select code text"));
  const QRectF codeCursor = layout.cursorRectForSourceOffset(QStringLiteral("math $E = mc^2$ then `").size());
  require(!codeCursor.isEmpty(), QStringLiteral("source cursor after collapsed math should exist"));
  require(qAbs(codeSelection.first().left() - codeCursor.left()) < 1.0,
          QStringLiteral("source selection after collapsed math should start at rebuilt code cursor"));
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

  QImage img(QSize(320, qCeil(layout.height()) + 20), QImage::Format_ARGB32);
  img.fill(theme.backgroundColor());
  QPainter painter(&img);
  layout.paint(painter, QPointF(8.0, 8.0));
  painter.end();

  int changedPixels = 0;
  const QRgb background = theme.backgroundColor().rgb();
  for (int y = 0; y < img.height(); ++y) {
    for (int x = 0; x < img.width(); ++x) {
      if ((img.pixel(x, y) & 0x00ffffff) != (background & 0x00ffffff)) {
        ++changedPixels;
      }
    }
  }
  require(changedPixels > 25, QStringLiteral("inline layout paint should draw visible pixels"));
}

void testInlineHtmlSimpleFormattingPassthrough() {
  RenderTheme theme = RenderTheme::github();

  // --- <u>**bold**</u>: underline + bold ---
  {
    QVector<InlineNode> inlines;
    InlineNode openU(InlineType::HtmlInline);
    openU.setText(QStringLiteral("<u>"));
    inlines << openU
            << InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("bold"))});
    InlineNode closeU(InlineType::HtmlInline);
    closeU.setText(QStringLiteral("</u>"));
    inlines << closeU;

    InlineLayout layout;
    layout.build(inlines, QStringLiteral("<u>**bold**</u>"), theme, 500.0, theme.paragraphFont(), InlineLayout::BuildOptions{});

    require(layout.displayText() == QStringLiteral("bold"),
            QStringLiteral("<u>**bold**</u> should display as 'bold', got '%1'").arg(layout.displayText()));

    const QVector<QTextLayout::FormatRange> formats = layout.debugTextFormats(theme, theme.paragraphFont());
    QTextCharFormat fmt;
    for (const auto& range : formats) {
      if (0 >= range.start && 0 < range.start + range.length) {
        fmt = range.format;
      }
    }
    require(fmt.fontWeight() >= QFont::Bold,
            QStringLiteral("<u>**bold**</u> should render bold"));
    require(fmt.fontUnderline(),
            QStringLiteral("<u>**bold**</u> should render underline"));
  }

  // --- <u>text</u>: underline only ---
  {
    QVector<InlineNode> inlines;
    InlineNode openU(InlineType::HtmlInline);
    openU.setText(QStringLiteral("<u>"));
    inlines << openU
            << InlineNode::text(QStringLiteral("text"));
    InlineNode closeU(InlineType::HtmlInline);
    closeU.setText(QStringLiteral("</u>"));
    inlines << closeU;

    InlineLayout layout;
    layout.build(inlines, QStringLiteral("<u>text</u>"), theme, 500.0, theme.paragraphFont(), InlineLayout::BuildOptions{});

    require(layout.displayText() == QStringLiteral("text"),
            QStringLiteral("<u>text</u> should display as 'text', got '%1'").arg(layout.displayText()));

    const QVector<QTextLayout::FormatRange> formats = layout.debugTextFormats(theme, theme.paragraphFont());
    QTextCharFormat fmt;
    for (const auto& range : formats) {
      if (0 >= range.start && 0 < range.start + range.length) {
        fmt = range.format;
      }
    }
    require(fmt.fontUnderline(),
            QStringLiteral("<u>text</u> should render underline"));
    require(fmt.fontWeight() < QFont::Bold,
            QStringLiteral("<u>text</u> should not render bold"));
  }

  // --- <b>*italic*</b>: bold + italic ---
  {
    QVector<InlineNode> inlines;
    InlineNode openB(InlineType::HtmlInline);
    openB.setText(QStringLiteral("<b>"));
    inlines << openB
            << InlineNode::emphasis(QStringLiteral("*"), {InlineNode::text(QStringLiteral("italic"))});
    InlineNode closeB(InlineType::HtmlInline);
    closeB.setText(QStringLiteral("</b>"));
    inlines << closeB;

    InlineLayout layout;
    layout.build(inlines, QStringLiteral("<b>*italic*</b>"), theme, 500.0, theme.paragraphFont(), InlineLayout::BuildOptions{});

    require(layout.displayText() == QStringLiteral("italic"),
            QStringLiteral("<b>*italic*</b> should display as 'italic', got '%1'").arg(layout.displayText()));

    const QVector<QTextLayout::FormatRange> formats = layout.debugTextFormats(theme, theme.paragraphFont());
    QTextCharFormat fmt;
    for (const auto& range : formats) {
      if (0 >= range.start && 0 < range.start + range.length) {
        fmt = range.format;
      }
    }
    require(fmt.fontWeight() >= QFont::Bold,
            QStringLiteral("<b>*italic*</b> should render bold"));
    require(fmt.fontItalic(),
            QStringLiteral("<b>*italic*</b> should render italic"));
  }

  // --- <s>text</s>: strikethrough passthrough ---
  {
    QVector<InlineNode> inlines;
    InlineNode openS(InlineType::HtmlInline);
    openS.setText(QStringLiteral("<s>"));
    inlines << openS
            << InlineNode::text(QStringLiteral("gone"));
    InlineNode closeS(InlineType::HtmlInline);
    closeS.setText(QStringLiteral("</s>"));
    inlines << closeS;

    InlineLayout layout;
    layout.build(inlines, QStringLiteral("<s>gone</s>"), theme, 500.0, theme.paragraphFont(), InlineLayout::BuildOptions{});

    require(layout.displayText() == QStringLiteral("gone"),
            QStringLiteral("<s>gone</s> should display as 'gone', got '%1'").arg(layout.displayText()));

    const QVector<QTextLayout::FormatRange> formats = layout.debugTextFormats(theme, theme.paragraphFont());
    QTextCharFormat fmt;
    for (const auto& range : formats) {
      if (0 >= range.start && 0 < range.start + range.length) {
        fmt = range.format;
      }
    }
    require(fmt.fontStrikeOut(),
            QStringLiteral("<s>gone</s> should render strikethrough"));
  }

  // --- <kbd>text</kbd>: complex tag, still uses InlineHtmlRenderer (backward compat) ---
  {
    QVector<InlineNode> inlines;
    InlineNode openKbd(InlineType::HtmlInline);
    openKbd.setText(QStringLiteral("<kbd>"));
    inlines << openKbd
            << InlineNode::text(QStringLiteral("Ctrl"));
    InlineNode closeKbd(InlineType::HtmlInline);
    closeKbd.setText(QStringLiteral("</kbd>"));
    inlines << closeKbd;

    InlineLayout layout;
    layout.build(inlines, QStringLiteral("<kbd>Ctrl</kbd>"), theme, 500.0, theme.paragraphFont(), InlineLayout::BuildOptions{});

    require(layout.displayText() == QStringLiteral("Ctrl"),
            QStringLiteral("<kbd>Ctrl</kbd> should display as 'Ctrl', got '%1'").arg(layout.displayText()));

    const QVector<QTextLayout::FormatRange> formats = layout.debugTextFormats(theme, theme.paragraphFont());
    QTextCharFormat fmt;
    for (const auto& range : formats) {
      if (0 >= range.start && 0 < range.start + range.length) {
        fmt = range.format;
      }
    }
    require(fmt.fontFamilies().toStringList().contains(QStringLiteral("Courier New")),
            QStringLiteral("<kbd>Ctrl</kbd> should still use monospace keyboard font (backward compat)"));
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testInlineLayoutGeometryContract);
  RUN_TEST(testInlineLayoutZeroWidthTabIndentGeometry);
  RUN_TEST(testInlineLayoutStyleFormats);
  RUN_TEST(testInlineHtmlKeyboardLayoutContract);
  RUN_TEST(testInlineLayoutProjectionDisplayMappingAfterCollapsedMath);
  RUN_TEST(testInlineLayoutPainting);
  RUN_TEST(testInlineHtmlSimpleFormattingPassthrough);
#undef RUN_TEST
  return 0;
}
