#include "mermaid/sequence/SequenceLabel.h"

#include "math/OpenTypeMathFont.h"

#include <QPainter>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <utility>

namespace muffin::mermaid::sequence {

SequenceLabelDocument parseSequenceLabel(const QString& source, SequenceLabelKind kind) {
  static const QRegularExpression breaks(QStringLiteral("<br\\s*/?>"),
                                          QRegularExpression::CaseInsensitiveOption);
  QString text = source;
  const bool mathEnabled = (kind == SequenceLabelKind::Message || kind == SequenceLabelKind::Note) &&
                           text.contains(QStringLiteral("$$"));
  text.replace(breaks, QStringLiteral("\n"));
  if (kind == SequenceLabelKind::Fragment)
    text = QLatin1Char('[') + text + QLatin1Char(']');
  if (mathEnabled) {
    auto richText = flowchart::parseFlowLabel(text, QStringLiteral("string"), true);
    // Sequence emits the surrounding text and MathML as direct children of a
    // nowrap flex row. Markdown markers remain literal; DOM item boundaries
    // determine whitespace collapse and shaping independently of their text.
    richText.literalMarkdownMathFallback = true;
    richText.sequenceMathMlModel = true;
    richText.direction = Qt::LeftToRight;
    return {std::move(richText), false, kind};
  }
  flowchart::FlowLabelDocument plain;
  plain.text = std::move(text);
  plain.direction = Qt::LeftToRight;
  return {std::move(plain), false, kind};
}

SequenceLabelDocument wrapSequenceLabel(SequenceLabelDocument label,
                                        const QString& fontFamily,
                                        qreal fontPixelSize,
                                        qreal maximumWidth) {
  if (maximumWidth <= 0.0 || !label.richText.math.isEmpty() ||
      label.richText.text.contains(QLatin1Char('\n')))
    return label;
  const auto width = [&](const QString& text) {
    flowchart::FlowLabelDocument candidate;
    candidate.text = text;
    candidate.direction = Qt::LeftToRight;
    return std::round(flowchart::measureFlowTextInkWidth(
        candidate, fontFamily, fontPixelSize));
  };
  QStringList completed;
  QString nextLine;
  const QStringList words = label.richText.text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  for (qsizetype wordIndex = 0; wordIndex < words.size(); ++wordIndex) {
    const QString& word = words[wordIndex];
    if (width(word + QLatin1Char(' ')) > maximumWidth) {
      if (!nextLine.isEmpty()) completed.append(std::exchange(nextLine, {}));
      QString current;
      for (qsizetype characterIndex = 0; characterIndex < word.size(); ++characterIndex) {
        current += word[characterIndex];
        if (width(current) < maximumWidth) continue;
        if (characterIndex + 1 < word.size()) current += QLatin1Char('-');
        completed.append(std::exchange(current, {}));
      }
      nextLine = std::move(current);
    } else {
      // Mermaid wrapLabel measures the existing line and the next word (with
      // its trailing space) independently before comparing their sum.
      const qreal candidateWidth =
          width(nextLine) + width(word + QLatin1Char(' '));
      if (!nextLine.isEmpty() && candidateWidth >= maximumWidth) {
        completed.append(std::exchange(nextLine, {}));
        nextLine = word;
      } else {
        if (!nextLine.isEmpty()) nextLine += QLatin1Char(' ');
        nextLine += word;
      }
    }
    if (wordIndex + 1 == words.size() && !nextLine.isEmpty())
      completed.append(nextLine);
  }
  label.richText.text = completed.join(QLatin1Char('\n'));
  return label;
}

SequenceLabelDocument prepareSequenceLabel(SequenceLabelDocument label,
                                           qreal fontPixelSize) {
  flowchart::prepareFlowLabelMath(label.richText, fontPixelSize);
  return label;
}

SequenceLabelLayoutMetrics layoutSequenceLabel(const SequenceLabelDocument& label,
                                               const QString& fontFamily,
                                               qreal fontPixelSize,
                                               qreal lineHeight) {
  SequenceLabelLayoutMetrics result =
      flowchart::layoutFlowLabel(label.richText, fontFamily, fontPixelSize, lineHeight);
  const qreal containerWidth = result.size.width();
  qreal maximumWidth = 0.0;
  for (auto& line : result.lines) {
    const auto primaryMath = std::find_if(line.runs.cbegin(), line.runs.cend(),
        [](const auto& run) { return run.math; });
    if (label.kind == SequenceLabelKind::Participant || label.kind == SequenceLabelKind::Box) {
      line.baseline = line.ascent = line.height / 2.0;
      line.descent = line.height - line.baseline;
    } else if (label.kind == SequenceLabelKind::Message) {
      line.baseline = line.ascent = line.height * (12.719 / 22.0);
      line.descent = line.height - line.baseline;
    } else if (label.kind == SequenceLabelKind::Note && label.richText.math.isEmpty()) {
      line.baseline = line.ascent = line.height * (12.719 / 22.0);
      line.descent = line.height - line.baseline;
    } else if (label.kind == SequenceLabelKind::Note ||
               label.kind == SequenceLabelKind::Fragment) {
      if (primaryMath != line.runs.cend() && primaryMath->mathBoxHeight > 0.0) {
        const qreal containerHeight = line.height;
        const qreal mathTop = (containerHeight - primaryMath->mathBoxHeight) / 2.0;
        const qreal textTop = (containerHeight - lineHeight) / 2.0;
        const qreal containerBaseline = containerHeight / 2.0 + 6.0;
        const qreal inkTop = std::min(
            textTop, mathTop + primaryMath->mathInkTop);
        const qreal inkBottom = std::max(textTop + lineHeight,
            mathTop + primaryMath->mathInkBottom);
        line.baseline = line.ascent = containerBaseline - inkTop;
        line.descent = inkBottom - containerBaseline;
      } else {
        line.baseline = line.ascent = 17.0;
        line.descent = 5.0;
      }
    }
    const bool allRtl = !line.runs.isEmpty() &&
        std::all_of(line.runs.cbegin(), line.runs.cend(), [](const auto& run) {
          return run.rightToLeft;
        });
    if (allRtl) {
      auto visual = line.runs.first();
      visual.start = line.start;
      visual.length = line.length;
      visual.x = 0.0;
      visual.width = line.width;
      line.runs = {std::move(visual)};
    }
    maximumWidth = std::max(maximumWidth, line.width);
  }
  result.size.setWidth(label.richText.math.isEmpty() ? maximumWidth
                                                     : containerWidth);
  if (!label.richText.math.isEmpty())
    result.size.setHeight(std::round(result.size.height()));
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
