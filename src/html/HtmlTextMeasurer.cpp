#include "html/HtmlTextMeasurer.h"
#include "html/HtmlBox.h"

#include <QFontMetricsF>

namespace muffin::html {

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
  QString text;
  std::vector<TextFormatSpan> spans;
  std::vector<HtmlTextLayout::LinkSpan> links;
  int offset = 0;
  collectInlineText(blockBox, text, spans, links, offset, false, false, false, HtmlTextDecoration::None, QColor(),
                    QColor(), QTextCharFormat::AlignNormal, QString(), fontSize, fontSize);

  if (text.isEmpty()) {
    QFont font;
    font.setPointSizeF(fontSize);
    QFontMetricsF fm(font);
    return QSizeF(0, fm.height());
  }

  QFont font;
  font.setPointSizeF(fontSize);
  return measure(text, font, availableWidth);
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
          start, offset - start, bold, italic, decoration, color, backgroundColor, mono, fontSize, verticalAlignment});
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

}  // namespace muffin::html
