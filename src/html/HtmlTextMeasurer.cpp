#include "html/HtmlTextMeasurer.h"
#include "html/HtmlBox.h"
#include "render/RenderMetrics.h"

#include <QFontMetricsF>
#include <algorithm>
#include <QTextOption>

#include <cmath>

namespace muffin::html {
namespace {

qreal resolvedLineHeight(const HtmlComputedStyle& style, const QFont& font) {
  QFontMetricsF metrics(font);
  if (style.lineHeight > 0) {
    // The shared computed-style projection has already resolved this to px.
    return std::ceil(qMax<qreal>(metrics.height(), style.lineHeight));
  }
  return std::ceil(metrics.height() * kLineHeightFactor);
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

void HtmlTextMeasurer::setDefaultTextColor(QColor color) { defaultTextColor_ = color; }

QSizeF HtmlTextMeasurer::measure(const QString& text, const QFont& font, qreal availableWidth) const {
  if (text.isEmpty()) {
    QFontMetricsF fm(font);
    return QSizeF(0, fm.height());
  }
  // Same QTextLayout line loop as buildLayout (default alignment); delegate so the two can
  // never drift — they used to be byte-identical copies.
  const std::unique_ptr<HtmlTextLayout> laidOut = buildLayout(text, font, availableWidth, Qt::Alignment());
  return QSizeF(laidOut->width, laidOut->height);
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
  option.setWrapMode(QTextOption::WordWrap);
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
  std::vector<HtmlTextLayout::AtomicInline> atoms;
  std::vector<HtmlTextLayout::TextSourceSpan> sources;
  collectInlineText(blockBox, text, spans, links, offset, false, false, false, false, HtmlTextDecoration::None, defaultTextColor_, QColor(),
                    QTextCharFormat::AlignNormal, QString(), fontSize, fontSize, &atoms, availableWidth, &blockBox, &sources);

  QFont baseFont;
  baseFont.setFamilies(blockBox.style().font.families());
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
    result->height = resolvedLineHeight(blockBox.style(), baseFont);
    return result;
  }

  auto result = std::make_unique<HtmlTextLayout>();
  result->text = text;
  result->font = baseFont;
  result->lineHeight = resolvedLineHeight(blockBox.style(), baseFont);
  result->atoms = std::move(atoms);
  result->formatSpans = spans;
  result->linkSpans = std::move(links);

  struct Run {
    int start, end;
    quintptr id;
    LayoutBox box;
  };
  std::vector<Run> runs;
  for (const auto& span : spans) {
    if (!span.inlineBoxId) continue;
    if (!runs.empty() && runs.back().id == span.inlineBoxId && runs.back().end == span.start) {
      runs.back().end = span.start + span.length;
    } else {
      auto used = span.inlineUsedBox;
      used.padding = used.paddingLengths.used(used.padding, availableWidth, true);
      runs.push_back({span.start, span.start + span.length, span.inlineBoxId,
                      LayoutBox::place(span.inlineBoxStyle.key, span.inlineBoxStyle, used, {}, span.font)});
    }
  }
  struct Edge {
    int position;
    qreal width;
  };
  std::vector<Edge> edges;
  for (const auto& run : runs) {
    const auto inset = LayoutBox::insets(run.box.usedBox);
    if (inset.left() > 0) edges.push_back({run.start, inset.left()});
    if (inset.right() > 0) edges.push_back({run.end, inset.right()});
  }
  std::stable_sort(edges.begin(), edges.end(), [](auto a, auto b) { return a.position < b.position; });
  const auto before = [&](int position) {
    return position + std::count_if(edges.begin(), edges.end(), [&](auto e) { return e.position < position; });
  };
  const auto after = [&](int position) {
    return position + std::count_if(edges.begin(), edges.end(), [&](auto e) { return e.position <= position; });
  };
  for (auto& span : spans) {
    const int end = before(span.start + span.length);
    span.start = after(span.start);
    span.length = end - span.start;
  }
  for (auto& span : result->linkSpans) {
    const int end = before(span.start + span.length);
    span.start = after(span.start);
    span.length = end - span.start;
  }
  for (auto& atom : result->atoms) atom.start = after(atom.start);
  for (auto& span : sources) {
    const auto end = before(span.start + span.length);
    span.start = after(span.start);
    span.length = end - span.start;
  }
  result->sourceSpans = std::move(sources);
  QString layoutText = text;
  for (auto edge = edges.rbegin(); edge != edges.rend(); ++edge) layoutText.insert(edge->position, QChar(0x200a));
  auto layout = std::make_unique<QTextLayout>(layoutText, baseFont);

  QTextOption option;
  const auto& wrapping = blockBox.style().computed.layout;
  option.setWrapMode(blockBox.style().whiteSpace == HtmlWhiteSpace::Pre ? QTextOption::NoWrap
                     : wrapping.wordBreak == "break-all"                ? QTextOption::WrapAnywhere
                     : wrapping.overflowWrap == "normal"                ? QTextOption::WordWrap
                                                                        : QTextOption::WrapAtWordBoundaryOrAnywhere);
  option.setAlignment(alignment);
  layout->setTextOption(option);

  // Apply format ranges from collected spans
  QVector<QTextLayout::FormatRange> formats;
  for (const auto& span : spans) {
    QTextCharFormat fmt;
    if (span.fontSet) fmt.setFont(span.font);
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
  for (size_t i = 0; i < edges.size(); ++i) {
    const QChar spacer(0x200a);
    QFont font = baseFont;
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    font.setLetterSpacing(QFont::AbsoluteSpacing, edges[i].width - QFontMetricsF(font).horizontalAdvance(spacer));
    QTextCharFormat format;
    format.setFont(font);
    format.setForeground(Qt::transparent);
    formats.push_back({edges[i].position + static_cast<int>(i), 1, format});
  }
  for (const auto& atom : result->atoms) {
    QFont font = baseFont;
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    font.setLetterSpacing(QFont::AbsoluteSpacing, atom.size.width() + atom.margin.left() + atom.margin.right() -
                                                      QFontMetricsF(font).horizontalAdvance(QChar(0xfffc)));
    QTextCharFormat format;
    format.setFont(font);
    format.setForeground(Qt::transparent);
    formats.push_back({atom.start, 1, format});
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
    const QFontMetricsF strut(baseFont);
    const qreal leading = (result->lineHeight - strut.height()) * .5;
    qreal ascent = qMax(line.ascent(), strut.ascent() + leading);
    qreal descent = qMax(line.descent(), strut.descent() + leading);
    for (const auto& atom : result->atoms)
      if (atom.start >= line.textStart() && atom.start < line.textStart() + line.textLength()) {
        ascent = qMax(ascent, atom.baseline + atom.margin.top());
        descent = qMax(descent, atom.size.height() - atom.baseline + atom.margin.bottom());
      }
    const qreal lineHeight = qMax(result->lineHeight, ascent + descent);
    line.setPosition(QPointF(0, height + (lineHeight - ascent - descent) * .5 + ascent - line.ascent()));
    for (auto& atom : result->atoms)
      if (atom.start >= line.textStart() && atom.start < line.textStart() + line.textLength()) {
        atom.rect = QRectF(line.cursorToX(atom.start) + atom.margin.left(), line.y() + line.ascent() - atom.baseline, atom.size.width(),
                           atom.size.height());
        atom.box->geometry().left = atom.rect.x() + blockBox.style().padding.left() + blockBox.style().borderWidth.left();
        atom.box->geometry().top = atom.rect.y() + blockBox.style().padding.top() + blockBox.style().borderWidth.top();
      }
    maxWidth = qMax(maxWidth, line.naturalTextWidth());
    height += lineHeight;
  }
  layout->endLayout();

  for (const auto& run : runs) {
    const int start = after(run.start), end = before(run.end);
    const auto inset = LayoutBox::insets(run.box.usedBox);
    for (int i = 0; i < layout->lineCount(); ++i) {
      const auto line = layout->lineAt(i);
      const int first = qMax(start, line.textStart()), last = qMin(end, line.textStart() + line.textLength());
      if (first >= last) continue;
      const qreal left = line.cursorToX(first), right = line.cursorToX(last);
      const QRectF rect(qMin(left, right) - inset.left(), line.y() - inset.top(), qAbs(right - left) + inset.left() + inset.right(),
                        line.height() + inset.top() + inset.bottom());
      result->inlineBoxes.push_back(LayoutBox::place(run.box.hostKey, run.box.style, run.box.usedBox, rect, run.box.font));
    }
  }
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

void HtmlTextMeasurer::collectInlineTextFromRoot(const HtmlBox& box, QString& outText, std::vector<TextFormatSpan>& outSpans,
                                                 std::vector<HtmlTextLayout::LinkSpan>& outLinks, int& offset, bool, bool,
                                                 bool parentMonospace, bool parentKeyboard, HtmlTextDecoration parentDecoration,
                                                 QColor parentColor, QColor parentBackgroundColor,
                                                 QTextCharFormat::VerticalAlignment parentVerticalAlignment, QString parentHref,
                                                 qreal parentFontSize, qreal baseFontSize) const {
  collectInlineText(box, outText, outSpans, outLinks, offset, false, false, parentMonospace, parentKeyboard, parentDecoration, parentColor,
                    parentBackgroundColor, parentVerticalAlignment, parentHref, parentFontSize, baseFontSize);
}

void HtmlTextMeasurer::collectInlineText(const HtmlBox& box, QString& outText, std::vector<TextFormatSpan>& outSpans,
                                         std::vector<HtmlTextLayout::LinkSpan>& outLinks, int& offset, bool, bool, bool parentMonospace,
                                         bool parentKeyboard, HtmlTextDecoration parentDecoration, QColor parentColor,
                                         QColor parentBackgroundColor, QTextCharFormat::VerticalAlignment parentVerticalAlignment,
                                         QString parentHref, qreal parentFontSize, qreal baseFontSize,
                                         std::vector<HtmlTextLayout::AtomicInline>* atoms, qreal availableWidth, const HtmlBox* contextRoot,
                                         std::vector<HtmlTextLayout::TextSourceSpan>* sources) const {
  if (!box.style().visible || box.style().display == HtmlDisplay::None) return;
  const auto& display = box.style().computed.layout.display;
  if (&box != contextRoot && atoms && atomicLayout_ && (display == "inline-flex" || display == "inline-grid")) {
    auto atom = atomicLayout_(const_cast<HtmlBox&>(box), availableWidth);
    atom.start = offset++;
    outText += QChar(0xfffc);
    atoms->push_back(std::move(atom));
    return;
  }
  const bool bold = box.style().fontWeight >= QFont::Bold;
  const bool italic = box.style().fontStyle == QFont::StyleItalic;
  bool mono = parentMonospace || box.tag() == HtmlTag::Code || box.tag() == HtmlTag::Kbd;
  const bool keyboard = parentKeyboard || box.tag() == HtmlTag::Kbd;
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
    if (sources) sources->push_back({start, int(box.text().size()), box.plainTextStart});

    if (bold || italic || mono || keyboard || decoration != HtmlTextDecoration::None || color.isValid() ||
        backgroundColor.isValid() ||
        (fontSize > 0 && !qFuzzyCompare(fontSize, baseFontSize)) ||
        verticalAlignment != QTextCharFormat::AlignNormal) {
      outSpans.push_back(TextFormatSpan{
          start, offset - start, bold, italic, decoration, color, backgroundColor, mono, keyboard,
          (fontSize > 0 && !qFuzzyCompare(fontSize, baseFontSize)) ? fontSize : 0.0,
          verticalAlignment});
      auto& span = outSpans.back();
      span.fontFamilies = box.style().font.families();
      span.font = box.style().font;
      span.fontSet = true;
      for (const HtmlBox* ancestor = box.parent(); ancestor; ancestor = ancestor->parent()) {
        if (ancestor->tag() != HtmlTag::Kbd && ancestor->tag() != HtmlTag::Code) continue;
        span.inlineBoxStyle = ancestor->style().computed;
        auto& used = span.inlineUsedBox;
        used = span.inlineBoxStyle.box;
        const auto& style = ancestor->style();
        used.padding = style.padding;
        used.paddingLengths = style.paddingLengths;
        used.borderLeftWidth = style.borderWidth.left();
        used.borderTopWidth = style.borderWidth.top();
        used.borderRightWidth = style.borderWidth.right();
        used.borderBottomWidth = style.borderWidth.bottom();
        used.borderRadius = style.borderRadius;
        span.inlineBoxId = reinterpret_cast<quintptr>(ancestor);
        break;
      }
    }
    if (!href.isEmpty() && offset > start) {
      outLinks.push_back(HtmlTextLayout::LinkSpan{start, offset - start, href});
    }
  } else if (box.tag() == HtmlTag::Break) {
    outText += QChar::LineSeparator;
    offset += 1;
  } else {
    // Recurse into inline children
    for (const auto& child : box.children()) {
      collectInlineText(*child, outText, outSpans, outLinks, offset, bold, italic, mono, keyboard, decoration, color, backgroundColor,
                        verticalAlignment, href, fontSize, baseFontSize, atoms, availableWidth, contextRoot, sources);
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
