#include "mermaid/sequence/SequenceLabel.h"

#include "mermaid/MermaidFontRegistry.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QRegularExpression>

#include <algorithm>

namespace muffin::mermaid::sequence {

SequenceLabelDocument parseSequenceLabel(const QString& source, SequenceLabelKind kind) {
  static const QRegularExpression breaks(QStringLiteral("<br\\s*/?>"),
                                          QRegularExpression::CaseInsensitiveOption);
  QString text = source;
  text.replace(breaks, QStringLiteral("\n"));
  if (kind == SequenceLabelKind::Fragment)
    text = QLatin1Char('[') + text + QLatin1Char(']');
  const bool mathEnabled = (kind == SequenceLabelKind::Message || kind == SequenceLabelKind::Note) &&
                           text.contains(QStringLiteral("$$"));
  if (mathEnabled) {
    auto richText = flowchart::parseFlowLabel(text, QStringLiteral("string"), true);
    // Sequence emits the surrounding text as an unscaled foreignObject range;
    // Markdown markers remain literal even when the Math span is MathML.
    richText.literalMarkdownMathFallback = true;
    return {std::move(richText), false, kind};
  }
  flowchart::FlowLabelDocument plain;
  plain.text = std::move(text);
  return {std::move(plain), false, kind};
}

SequenceLabelDocument wrapSequenceLabel(SequenceLabelDocument label,
                                        const QString& fontFamily,
                                        qreal fontPixelSize,
                                        qreal maximumWidth) {
  if (maximumWidth <= 0.0 || !label.richText.math.isEmpty() ||
      label.richText.text.contains(QLatin1Char('\n')))
    return label;
  QFont font(fontFamily);
  MermaidFontRegistry::configureFont(font, fontFamily);
  font.setPixelSize(qRound(fontPixelSize));
  font.setHintingPreference(QFont::PreferNoHinting);
  const QFontMetricsF metrics(font);
  qsizetype lineStart = 0;
  qsizetype separator = label.richText.text.indexOf(QLatin1Char(' '));
  while (separator >= 0) {
    const qsizetype nextSeparator = label.richText.text.indexOf(QLatin1Char(' '), separator + 1);
    const qsizetype candidateEnd = nextSeparator >= 0 ? nextSeparator : label.richText.text.size();
    if (metrics.horizontalAdvance(label.richText.text.mid(lineStart, candidateEnd - lineStart)) >
            maximumWidth && separator > lineStart) {
      label.richText.text[separator] = QLatin1Char('\n');
      lineStart = separator + 1;
    }
    separator = nextSeparator;
  }
  return label;
}

SequenceLabelLayoutMetrics layoutSequenceLabel(const SequenceLabelDocument& label,
                                               const QString& fontFamily,
                                               qreal fontPixelSize,
                                               qreal lineHeight) {
  SequenceLabelLayoutMetrics result =
      flowchart::layoutFlowLabel(label.richText, fontFamily, fontPixelSize, lineHeight);
  qreal maximumWidth = 0.0;
  for (auto& line : result.lines) {
    if (label.kind == SequenceLabelKind::Participant || label.kind == SequenceLabelKind::Box) {
      line.baseline = line.ascent = line.height / 2.0;
      line.descent = line.height - line.baseline;
    } else if (label.kind == SequenceLabelKind::Message) {
      line.baseline = line.ascent = line.height * (12.719 / 22.0);
      line.descent = line.height - line.baseline;
    } else if (label.kind == SequenceLabelKind::Note ||
               label.kind == SequenceLabelKind::Fragment) {
      line.baseline = line.ascent = 17.0;
      line.descent = 5.0;
    }
    if (label.richText.literalMarkdownMathFallback && !line.runs.isEmpty()) {
      constexpr qreal kSequenceMathTextScale = 86.281 / 90.359375;
      constexpr qreal kSequenceMathSpanScale = 15.469 / 15.9482421875;
      constexpr qreal kSequenceMathTrailingGap = 0.656;
      qreal cursor = 0.0;
      bool previousMath = false;
      for (auto& run : line.runs) {
        if (previousMath && !run.math) cursor += kSequenceMathTrailingGap;
        run.x = cursor;
        if (run.math)
          run.width *= kSequenceMathSpanScale;
        else if (run.length > 1)
          run.width *= kSequenceMathTextScale;
        cursor += run.width;
        previousMath = run.math;
      }
      line.width = cursor;
    }
    const bool allRtl = !line.runs.isEmpty() &&
        std::all_of(line.runs.cbegin(), line.runs.cend(), [](const auto& run) {
          return run.rightToLeft;
        });
    const QString lineText = label.richText.text.mid(line.start, line.length);
    const bool containsHebrew = std::any_of(lineText.cbegin(), lineText.cend(), [](QChar ch) {
      return ch.unicode() >= 0x0590 && ch.unicode() <= 0x05ff;
    });
    if (allRtl && containsHebrew) {
      constexpr qreal kSequenceRtlAdvanceScale = 73.469 / 71.75;
      line.width *= kSequenceRtlAdvanceScale;
      for (auto& run : line.runs) {
        run.x *= kSequenceRtlAdvanceScale;
        run.width *= kSequenceRtlAdvanceScale;
      }
    }
    maximumWidth = std::max(maximumWidth, line.width);
  }
  result.size.setWidth(maximumWidth);
  return result;
}

void paintSequenceLabel(QPainter& painter, const SequenceLabelDocument& label,
                        const QRectF& rect, const QString& fontFamily,
                        qreal fontPixelSize, qreal lineHeight,
                        const QColor& color, bool centerVertically) {
  flowchart::paintFlowLabel(painter, label.richText, rect, fontFamily, fontPixelSize,
                            lineHeight, color, centerVertically);
}

}  // namespace muffin::mermaid::sequence
