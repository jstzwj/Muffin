#include "mermaid/sequence/SequenceLabel.h"

#include "mermaid/MermaidFontRegistry.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QRegularExpression>

#include <algorithm>
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
    // Sequence emits the surrounding text as an unscaled foreignObject range;
    // Markdown markers remain literal even when the Math span is MathML.
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
  QFont font(fontFamily);
  MermaidFontRegistry::configureFont(font, fontFamily);
  font.setPixelSize(qRound(fontPixelSize));
  font.setHintingPreference(QFont::PreferNoHinting);
  const QFontMetricsF metrics(font);
  const auto width = [&](const QString& text) {
    return std::round(metrics.horizontalAdvance(text));
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
      // SVG getBBox accumulates spaced words slightly more tightly than Qt's
      // per-string advances; character splitting below intentionally stays raw.
      constexpr qreal kSvgWordMeasureScale = 0.9;
      const qreal candidateWidth =
          (width(nextLine) + width(word + QLatin1Char(' '))) * kSvgWordMeasureScale;
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
      if (primaryMath != line.runs.cend() &&
          primaryMath->mathStructure == flowchart::FlowLabelMathStructure::Fraction) {
        const QString lineText = label.richText.text.mid(line.start, line.length);
        const bool mixedFallback = std::any_of(lineText.cbegin(), lineText.cend(), [](QChar ch) {
          return (ch.unicode() >= 0x2e80 && ch.unicode() <= 0x9fff) ||
                 (ch.unicode() >= 0x0600 && ch.unicode() <= 0x08ff);
        });
        line.baseline = line.ascent = mixedFallback ? 25.906 : 26.0;
        line.descent = mixedFallback ? 12.906 : 12.813;
      } else if (primaryMath != line.runs.cend() &&
                 primaryMath->mathStructure == flowchart::FlowLabelMathStructure::Radical) {
        line.baseline = line.ascent = 20.75;
        line.descent = 5.016;
      } else if (primaryMath != line.runs.cend() &&
                 primaryMath->mathStructure == flowchart::FlowLabelMathStructure::Array) {
        line.baseline = line.ascent = 21.5;
        line.descent = 12.5;
      } else {
        line.baseline = line.ascent = 17.0;
        line.descent = 5.0;
      }
    }
    const bool literalMarkdownMarkers = label.richText.text.contains(QLatin1Char('`')) ||
                                        label.richText.text.contains(QStringLiteral("**"));
    if (label.richText.literalMarkdownMathFallback && literalMarkdownMarkers) {
      constexpr qreal kSequenceMathTextScale = 86.281 / 90.359375;
      qreal cursor = 0.0;
      for (auto& run : line.runs) {
        run.x = cursor;
        if (!run.math && run.length > 1)
          run.width *= kSequenceMathTextScale;
        else if (!run.math && run.length == 1)
          run.width *= 4.5 / 4.484375;
        cursor += run.width;
      }
      line.width = cursor;
    }
    const bool allRtl = !line.runs.isEmpty() &&
        std::all_of(line.runs.cbegin(), line.runs.cend(), [](const auto& run) {
          return run.rightToLeft;
        });
    const bool hasRtl = std::any_of(line.runs.cbegin(), line.runs.cend(),
                                    [](const auto& run) { return run.rightToLeft; });
    const bool hasLtr = std::any_of(line.runs.cbegin(), line.runs.cend(),
                                    [](const auto& run) { return !run.rightToLeft; });
    const QString lineText = label.richText.text.mid(line.start, line.length);
    const bool containsHebrew = std::any_of(lineText.cbegin(), lineText.cend(), [](QChar ch) {
      return ch.unicode() >= 0x0590 && ch.unicode() <= 0x05ff;
    });
    const bool containsArabic = std::any_of(lineText.cbegin(), lineText.cend(), [](QChar ch) {
      return ch.unicode() >= 0x0600 && ch.unicode() <= 0x08ff;
    });
    const bool containsLatin = std::any_of(lineText.cbegin(), lineText.cend(), [](QChar ch) {
      const ushort code = ch.unicode();
      return (code >= 0x0041 && code <= 0x005a) || (code >= 0x0061 && code <= 0x007a);
    });
    if (allRtl && containsHebrew) {
      constexpr qreal kSequenceMixedRtlAdvanceScale = 73.469 / 71.75;
      constexpr qreal kSequenceHebrewAdvanceScale = 73.406 / 71.96875;
      const qreal scale = containsArabic ? kSequenceMixedRtlAdvanceScale
                                         : kSequenceHebrewAdvanceScale;
      line.width *= scale;
      for (auto& run : line.runs) {
        run.x *= scale;
        run.width *= scale;
      }
    } else if (allRtl && containsArabic && label.kind == SequenceLabelKind::Message) {
      constexpr qreal kSequenceArabicMessageScale = 77.094 / 78.625;
      line.width *= kSequenceArabicMessageScale;
      for (auto& run : line.runs) {
        run.x *= kSequenceArabicMessageScale;
        run.width *= kSequenceArabicMessageScale;
      }
    } else if (hasRtl && hasLtr && label.kind == SequenceLabelKind::Message) {
      constexpr qreal kSequenceMixedDirectionScale = 145.328 / 145.125;
      line.width *= kSequenceMixedDirectionScale;
      for (auto& run : line.runs) {
        run.x *= kSequenceMixedDirectionScale;
        run.width *= kSequenceMixedDirectionScale;
      }
    } else if (hasLtr && !hasRtl && containsLatin &&
               !label.richText.literalMarkdownMathFallback) {
      constexpr qreal kSequenceLatinSvgAdvanceScale = 183.375 / 183.1875;
      line.width *= kSequenceLatinSvgAdvanceScale;
      for (auto& run : line.runs) {
        run.x *= kSequenceLatinSvgAdvanceScale;
        run.width *= kSequenceLatinSvgAdvanceScale;
      }
    }
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
  const bool literalMarkdownMarkers = label.richText.text.contains(QLatin1Char('`')) ||
                                      label.richText.text.contains(QStringLiteral("**"));
  result.size.setWidth(label.richText.literalMarkdownMathFallback && !literalMarkdownMarkers
                           ? containerWidth
                           : literalMarkdownMarkers ? std::round(maximumWidth) : maximumWidth);
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
