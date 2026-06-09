#include "html/HtmlTextMeasurer.h"
#include "html/HtmlBox.h"

#include <QFontMetricsF>
#include <QTextOption>

#include <cmath>

namespace muffin::html {
namespace {

qreal resolvedLineHeight(const HtmlComputedStyle& style, const QFont& font) {
  QFontMetricsF metrics(font);
  if (style.lineHeight > 0) {
    if (style.lineHeight < 4.0) {
      return qMax<qreal>(metrics.height(), std::ceil(style.fontSize * style.lineHeight));
    }
    return qMax<qreal>(metrics.height(), style.lineHeight);
  }
  return metrics.height();
}

QString normalizePreText(QString text) {
  text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
  text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  if (text.startsWith(QLatin1String("\r\n"))) {
    text.remove(0, 2);
  } else if (text.startsWith(QLatin1Char('\n')) || text.startsWith(QLatin1Char('\r'))) {
    text.remove(0, 1);
  }
  text.replace(QLatin1Char('\n'), QChar::LineSeparator);
  return text;
}

}  // namespace

QSizeF HtmlTextMeasurer::measure(const QString& text, const QFont& font, qreal availableWidth) const {
  if (text.isEmpty()) {
    QFontMetricsF fm(font);
    return QSizeF(0, fm.height());
  }

  QTextLayout layout(text, font);
  QTextOption option;
  option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  layout.setTextOption(option);
  layout.beginLayout();

  qreal height = 0;
  qreal maxWidth = 0;
  while (true) {
    QTextLine line = layout.createLine();
    if (!line.isValid()) {
      break;
    }
    line.setLineWidth(qMax<qreal>(1.0, availableWidth));
    line.setPosition(QPointF(0, height));
    maxWidth = qMax(maxWidth, line.naturalTextWidth());
    height += line.height();
  }
  layout.endLayout();

  return QSizeF(maxWidth, height);
}

std::unique_ptr<HtmlTextLayout> HtmlTextMeasurer::buildLayout(
    const QString& text,
    const QFont& font,
    qreal availableWidth,
    Qt::Alignment alignment) const {
  auto result = std::make_unique<HtmlTextLayout>();
  result->text = text;
  result->font = font;

  auto layout = std::make_unique<QTextLayout>(text, font);
  QTextOption option;
  option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  option.setAlignment(alignment);
  layout->setTextOption(option);
  layout->beginLayout();

  qreal height = 0;
  qreal maxWidth = 0;
  while (true) {
    QTextLine line = layout->createLine();
    if (!line.isValid()) {
      break;
    }
    line.setLineWidth(qMax<qreal>(1.0, availableWidth));
    line.setPosition(QPointF(0, height));
    maxWidth = qMax(maxWidth, line.naturalTextWidth());
    height += line.height();
  }
  layout->endLayout();

  result->width = maxWidth;
  result->height = height;
  result->layout = std::move(layout);
  return result;
}

QSizeF HtmlTextMeasurer::measureInlineContext(
    const HtmlBox& blockBox,
    qreal fontSize,
    qreal availableWidth) const {
  // Use buildInlineLayout to account for bold, monospace, font-size changes, etc.
  auto layout = buildInlineLayout(blockBox, fontSize, availableWidth);
  return QSizeF(layout->width, layout->height);
}

std::unique_ptr<HtmlTextLayout> HtmlTextMeasurer::buildInlineLayout(
    const HtmlBox& blockBox,
    qreal fontSize,
    qreal availableWidth,
    Qt::Alignment alignment) const {
  QString text;
  std::vector<TextFormatSpan> spans;
  std::vector<HtmlTextLayout::LinkSpan> links;
  int offset = 0;
  collectInlineText(blockBox, text, spans, links, offset, false, false, false, HtmlTextDecoration::None, QColor(),
                    QColor(), QTextCharFormat::AlignNormal, QString(), fontSize, fontSize);

  QFont baseFont;
  baseFont.setPointSizeF(fontSize);
  // Apply letter-spacing from the block box style (inherited property)
  if (blockBox.style().letterSpacing != 0) {
    baseFont.setLetterSpacing(QFont::AbsoluteSpacing, blockBox.style().letterSpacing);
  }

  if (text.isEmpty()) {
    auto result = std::make_unique<HtmlTextLayout>();
    result->text = text;
    result->font = baseFont;
    result->layout = std::make_unique<QTextLayout>(text, baseFont);
    QFontMetricsF fm(baseFont);
    result->height = fm.height();
    return result;
  }

  auto result = std::make_unique<HtmlTextLayout>();
  result->text = text;
  result->font = baseFont;
  result->linkSpans = std::move(links);

  auto layout = std::make_unique<QTextLayout>(text, baseFont);
  QTextOption option;
  option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  option.setAlignment(alignment);
  layout->setTextOption(option);

  // Apply format ranges from collected spans
  QVector<QTextLayout::FormatRange> formats;
  for (const auto& span : spans) {
    QTextCharFormat fmt;
    if (span.bold) {
      fmt.setFontWeight(QFont::Bold);
    }
    if (span.italic) {
      fmt.setFontItalic(true);
    }
    if (span.monospace) {
      fmt.setFontFamily(QStringLiteral("Courier New"));
    }
    if (span.color.isValid()) {
      fmt.setForeground(span.color);
    }
    if (span.fontSize > 0 && !qFuzzyCompare(span.fontSize, fontSize)) {
      fmt.setFontPointSize(span.fontSize);
    }
    if (span.verticalAlignment != QTextCharFormat::AlignNormal) {
      fmt.setVerticalAlignment(span.verticalAlignment);
    }
    if (hasDecoration(span.decoration, HtmlTextDecoration::Underline)) {
      fmt.setFontUnderline(true);
    }
    if (hasDecoration(span.decoration, HtmlTextDecoration::LineThrough)) {
      fmt.setFontStrikeOut(true);
    }
    if (span.backgroundColor.isValid()) {
      fmt.setBackground(span.backgroundColor);
    }

    if (fmt != QTextCharFormat()) {
      QTextLayout::FormatRange range;
      range.start = span.start;
      range.length = span.length;
      range.format = fmt;
      formats.append(range);
    }
  }
  if (!formats.isEmpty()) {
    layout->setFormats(formats);
  }

  layout->beginLayout();
  qreal height = 0;
  qreal maxWidth = 0;
  while (true) {
    QTextLine line = layout->createLine();
    if (!line.isValid()) {
      break;
    }
    line.setLineWidth(qMax<qreal>(1.0, availableWidth));
    line.setPosition(QPointF(0, height));
    maxWidth = qMax(maxWidth, line.naturalTextWidth());
    height += line.height();
  }
  layout->endLayout();

  result->width = maxWidth;
  result->height = height;
  result->layout = std::move(layout);
  return result;
}

std::unique_ptr<HtmlTextLayout> HtmlTextMeasurer::buildPreLayout(
    const HtmlBox& preBox,
    qreal fontSize,
    qreal availableWidth) const {
  auto result = std::make_unique<HtmlTextLayout>();
  QString text = normalizePreText(collectPlainText(preBox));

  QFont font = preBox.style().font;
  font.setPointSizeF(fontSize);
  if (font.family().isEmpty()) {
    font.setFamily(QStringLiteral("Courier New"));
  }

  if (text.isEmpty()) {
    text = QStringLiteral(" ");
  }

  result->text = text;
  result->font = font;
  result->lineHeight = resolvedLineHeight(preBox.style(), font);

  auto layout = std::make_unique<QTextLayout>(text, font);
  QTextOption option;
  option.setWrapMode(preBox.style().whiteSpace == HtmlWhiteSpace::PreWrap
                         ? QTextOption::WrapAtWordBoundaryOrAnywhere
                         : QTextOption::NoWrap);
  option.setAlignment(preBox.style().textAlign);
  layout->setTextOption(option);

  layout->beginLayout();
  qreal height = 0;
  qreal maxWidth = 0;
  while (true) {
    QTextLine line = layout->createLine();
    if (!line.isValid()) {
      break;
    }
    const qreal lineWidth = preBox.style().whiteSpace == HtmlWhiteSpace::PreWrap
                                ? qMax<qreal>(1.0, availableWidth)
                                : 1000000.0;
    line.setLineWidth(lineWidth);
    const qreal naturalLineHeight = line.height();
    const qreal lineHeight = qMax(result->lineHeight, naturalLineHeight);
    line.setPosition(QPointF(0, height + (lineHeight - naturalLineHeight) * 0.5));
    maxWidth = qMax(maxWidth, line.naturalTextWidth());
    height += lineHeight;
  }
  layout->endLayout();

  result->width = preBox.style().whiteSpace == HtmlWhiteSpace::PreWrap
                      ? qMin(maxWidth, qMax<qreal>(1.0, availableWidth))
                      : maxWidth;
  result->height = height;
  result->layout = std::move(layout);
  return result;
}

void HtmlTextMeasurer::collectInlineTextFromRoot(
    const HtmlBox& box,
    QString& outText,
    std::vector<TextFormatSpan>& outSpans,
    std::vector<HtmlTextLayout::LinkSpan>& outLinks,
    int& offset,
    bool parentBold,
    bool parentItalic,
    bool parentMonospace,
    HtmlTextDecoration parentDecoration,
    QColor parentColor,
    QColor parentBackgroundColor,
    QTextCharFormat::VerticalAlignment parentVerticalAlignment,
    QString parentHref,
    qreal parentFontSize,
    qreal baseFontSize) const {
  collectInlineText(box, outText, outSpans, outLinks, offset,
                    parentBold, parentItalic, parentMonospace, parentDecoration,
                    parentColor, parentBackgroundColor, parentVerticalAlignment,
                    parentHref, parentFontSize, baseFontSize);
}

void HtmlTextMeasurer::collectInlineText(
    const HtmlBox& box,
    QString& outText,
    std::vector<TextFormatSpan>& outSpans,
    std::vector<HtmlTextLayout::LinkSpan>& outLinks,
    int& offset,
    bool parentBold,
    bool parentItalic,
    bool parentMonospace,
    HtmlTextDecoration parentDecoration,
    QColor parentColor,
    QColor parentBackgroundColor,
    QTextCharFormat::VerticalAlignment parentVerticalAlignment,
    QString parentHref,
    qreal parentFontSize,
    qreal baseFontSize) const {
  bool bold = parentBold || box.style().fontWeight >= QFont::Bold;
  bool italic = parentItalic || box.style().fontStyle == QFont::StyleItalic;
  bool mono = parentMonospace || box.tag() == HtmlTag::Code || box.tag() == HtmlTag::Kbd;
  auto decoration = parentDecoration;
  if (box.style().textDecoration != HtmlTextDecoration::None) {
    decoration |= box.style().textDecoration;
  }
  QColor color = box.style().color.isValid() ? box.style().color : parentColor;
  QColor backgroundColor = box.style().backgroundColor.isValid() ? box.style().backgroundColor : parentBackgroundColor;
  const qreal fontSize = box.style().fontSize > 0 ? box.style().fontSize : parentFontSize;
  const QString href = box.tag() == HtmlTag::Anchor && !box.href().isEmpty() ? box.href() : parentHref;
  QTextCharFormat::VerticalAlignment verticalAlignment = parentVerticalAlignment;
  if (box.tag() == HtmlTag::Sub) {
    verticalAlignment = QTextCharFormat::AlignSubScript;
  } else if (box.tag() == HtmlTag::Sup) {
    verticalAlignment = QTextCharFormat::AlignSuperScript;
  }

  if (box.tag() == HtmlTag::TextRun) {
    int start = offset;
    outText += box.text();
    offset += box.text().length();

    if (bold || italic || mono || decoration != HtmlTextDecoration::None || color.isValid() ||
        backgroundColor.isValid() ||
        (fontSize > 0 && !qFuzzyCompare(fontSize, baseFontSize)) ||
        verticalAlignment != QTextCharFormat::AlignNormal) {
      outSpans.push_back(TextFormatSpan{
          start, offset - start, bold, italic, decoration, color, backgroundColor, mono,
          (fontSize > 0 && !qFuzzyCompare(fontSize, baseFontSize)) ? fontSize : 0.0,
          verticalAlignment});
    }
    if (!href.isEmpty() && offset > start) {
      outLinks.push_back(HtmlTextLayout::LinkSpan{start, offset - start, href});
    }
  } else if (box.tag() == HtmlTag::Break) {
    outText += QLatin1Char('\n');
    offset += 1;
  } else {
    // Recurse into inline children
    for (const auto& child : box.children()) {
      collectInlineText(*child, outText, outSpans, outLinks, offset, bold, italic, mono, decoration, color,
                        backgroundColor, verticalAlignment, href, fontSize, baseFontSize);
    }
  }
}

QString HtmlTextMeasurer::collectPlainText(const HtmlBox& box) const {
  if (box.tag() == HtmlTag::TextRun) {
    return box.text();
  }
  if (box.tag() == HtmlTag::Break) {
    return QStringLiteral("\n");
  }

  QString result;
  for (const auto& child : box.children()) {
    result += collectPlainText(*child);
  }
  return result;
}

}  // namespace muffin::html
