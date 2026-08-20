#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/flowchart/FlowLabel.h"

#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  // qCritical alone is swallowed without QT_FORCE_STDERR_LOGGING (the ctest
  // preset does not set it) — flush assertions to stderr directly.
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

void near(qreal actual, qreal expected, qreal tolerance,
          const QString& context) {
  require(std::abs(actual - expected) <= tolerance,
          QStringLiteral("%1: native=%2 browser=%3 tolerance=%4")
              .arg(context).arg(actual).arg(expected).arg(tolerance));
}

struct TextRun {
  qsizetype start = 0;
  qsizetype length = 0;
  qreal x = 0.0;
  qreal width = 0.0;
  bool rtl = false;
  qsizetype item = -1;
  qsizetype textNode = -1;
};

QVector<TextRun> normalizedTextRuns(
    const flowchart::FlowLabelDocument& document,
    const flowchart::FlowLabelLayoutMetrics& layout) {
  QVector<TextRun> result;
  for (const auto& line : layout.lines) {
    for (const auto& source : line.runs) {
      if (source.math) continue;
      qsizetype itemIndex = -1;
      qsizetype textNode = 0;
      for (qsizetype index = 0; index < document.domItems.size(); ++index) {
        const auto& item = document.domItems.at(index);
        if (source.start >= item.start &&
            source.start < item.start + item.length) {
          itemIndex = index;
          break;
        }
        if (item.kind != flowchart::FlowLabelDomItemKind::Math)
          ++textNode;
      }
      TextRun run{source.start, source.length, source.x, source.width,
                  source.rightToLeft, itemIndex, textNode};
      const qsizetype previousEnd = result.isEmpty()
          ? 0 : result.last().start + result.last().length;
      const qsizetype runEnd = run.start + run.length;
      if (!result.isEmpty() && result.last().item == run.item &&
          result.last().rtl == run.rtl &&
          previousEnd >= run.start && runEnd >= result.last().start) {
        const qreal left = std::min(result.last().x, run.x);
        const qreal right = std::max(result.last().x + result.last().width,
                                     run.x + run.width);
        const qsizetype logicalStart = std::min(result.last().start, run.start);
        const qsizetype logicalEnd = std::max(
            result.last().start + result.last().length,
            run.start + run.length);
        result.last().start = logicalStart;
        result.last().length = logicalEnd - logicalStart;
        result.last().x = left;
        result.last().width = right - left;
      } else {
        result.push_back(run);
      }
    }
  }
  for (auto& run : result) {
    const auto& item = document.domItems.at(run.item);
    qsizetype itemVisibleStart = item.start;
    const qsizetype itemEnd = item.start + item.length;
    while (itemVisibleStart < itemEnd &&
           document.text.at(itemVisibleStart).isSpace())
      ++itemVisibleStart;
    qsizetype logicalStart = run.start;
    qsizetype logicalEnd = run.start + run.length;
    while (logicalStart < logicalEnd && document.text.at(logicalStart).isSpace())
      ++logicalStart;
    while (logicalEnd > logicalStart && document.text.at(logicalEnd - 1).isSpace())
      --logicalEnd;
    run.start = logicalStart - itemVisibleStart;
    run.length = logicalEnd - logicalStart;
  }
  return result;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected flowchart label fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly),
          QStringLiteral("Cannot open flowchart label fixture"));
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString() ==
                  QLatin1String("11.16.0") &&
              root.value(QStringLiteral("fontMode")).toString() ==
                  QLatin1String("bundled-noto-stix-two-math-2.13b171"),
          QStringLiteral("Flowchart label oracle version/font drifted"));
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("427d54b1c9f4754a978323b8bc3d4e0ffa79c2e706c1404ae14ac46e527274aa"),
          QStringLiteral("Flowchart label fixture changed; audit DOM/run geometry and update digest"));

  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 21,
          QStringLiteral("Flowchart label oracle case count regressed"));
  const QString fontFamily = MermaidFontRegistry::cssFamilyStack();
  int mathSpanCount = 0;
  int mixedMathCases = 0;
  int characterBoxes = 0;
  int visualBidiRuns = 0;
  int rtlCases = 0;
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    auto document = flowchart::parseFlowLabel(
        fixture.value(QStringLiteral("label")).toString(),
        fixture.value(QStringLiteral("labelType")).toString());
    flowchart::prepareFlowLabelMath(document, 16.0);
    const auto native = flowchart::layoutFlowLabel(
        document, fontFamily, 16.0, 24.0);
    const QJsonArray expectedCharacters =
        fixture.value(QStringLiteral("chars")).toArray();
    const QJsonArray expectedVisualRuns =
        fixture.value(QStringLiteral("visualRuns")).toArray();
    bool browserHasRtl = false;
    for (const QJsonValue& runValue : expectedVisualRuns) {
      const QJsonObject run = runValue.toObject();
      require(run.value(QStringLiteral("width")).toDouble() > 0.0 &&
                  run.value(QStringLiteral("fontFamily")).toString().contains(
                      QStringLiteral("Noto Sans")),
              QStringLiteral("%1 browser visual run/font contract drifted").arg(id));
      browserHasRtl = browserHasRtl ||
          run.value(QStringLiteral("rightToLeft")).toBool();
    }
    const bool nativeHasRtl = std::any_of(
        native.lines.cbegin(), native.lines.cend(), [](const auto& line) {
          return std::any_of(line.runs.cbegin(), line.runs.cend(),
                             [](const auto& run) {
                               return !run.math && run.rightToLeft;
                             });
        });
    require(browserHasRtl == nativeHasRtl,
            QStringLiteral("%1 visual bidi direction drifted").arg(id));
    const QVector<TextRun> nativeTextRuns = normalizedTextRuns(document, native);
    QStringList nativeRunSummary;
    for (const auto& run : nativeTextRuns)
      nativeRunSummary << QStringLiteral("%1:%2")
                              .arg(run.rtl ? QStringLiteral("rtl")
                                           : QStringLiteral("ltr"))
                              .arg(QStringLiteral("%1+%2@%3/%4")
                                       .arg(run.start).arg(run.length)
                                       .arg(run.x).arg(run.width));
    require(nativeTextRuns.size() == expectedVisualRuns.size(),
            QStringLiteral("%1 text visual run count: native=%2 [%3] browser=%4")
                .arg(id).arg(nativeTextRuns.size()).arg(nativeRunSummary.join(", "))
                .arg(expectedVisualRuns.size()));
    for (qsizetype runIndex = 0; runIndex < nativeTextRuns.size(); ++runIndex) {
      const auto& actual = nativeTextRuns.at(runIndex);
      const QJsonObject expected = expectedVisualRuns.at(runIndex).toObject();
      const QString context = QStringLiteral("%1 text run %2")
                                  .arg(id).arg(runIndex);
      require(actual.rtl == expected.value(QStringLiteral("rightToLeft")).toBool(),
              context + QStringLiteral(" direction mismatch"));
      require(actual.textNode ==
                  expected.value(QStringLiteral("textNodeIndex")).toInteger() &&
              actual.start == expected.value(QStringLiteral("start")).toInteger() &&
              actual.length == expected.value(QStringLiteral("length")).toInteger(),
              context + QStringLiteral(
                  " logical text-node range mismatch: native=%1:%2+%3 browser=%4:%5+%6")
                  .arg(actual.textNode).arg(actual.start).arg(actual.length)
                  .arg(expected.value(QStringLiteral("textNodeIndex")).toInteger())
                  .arg(expected.value(QStringLiteral("start")).toInteger())
                  .arg(expected.value(QStringLiteral("length")).toInteger()) +
                  QStringLiteral(" all native=[%1]")
                      .arg(nativeRunSummary.join(", ")));
      near(actual.x, expected.value(QStringLiteral("x")).toDouble(), 0.25,
           context + QStringLiteral(" x"));
      near(actual.width, expected.value(QStringLiteral("width")).toDouble(),
           0.25, context + QStringLiteral(" width"));
    }
    characterBoxes += expectedCharacters.size();
    visualBidiRuns += expectedVisualRuns.size();
    rtlCases += browserHasRtl;
    const QJsonArray expectedLines = fixture.value(QStringLiteral("lines")).toArray();
    require(native.lines.size() == expectedLines.size(),
            QStringLiteral("%1 line count: native=%2 browser=%3")
                .arg(id).arg(native.lines.size()).arg(expectedLines.size()));
    mixedMathCases += document.text.size() > document.math.size();

    const QJsonObject expectedBox = fixture.value(QStringLiteral("box")).toObject();
    const qreal horizontalTolerance = 0.25;
    near(native.size.width(), expectedBox.value(QStringLiteral("width")).toDouble(),
         horizontalTolerance, id + QStringLiteral(" box width"));
    near(native.size.height(), expectedBox.value(QStringLiteral("height")).toDouble(),
         0.25, id + QStringLiteral(" box height"));
    for (qsizetype lineIndex = 0; lineIndex < native.lines.size(); ++lineIndex) {
      const auto& line = native.lines.at(lineIndex);
      const QJsonObject expected = expectedLines.at(lineIndex).toObject();
      const QString context = QStringLiteral("%1 line %2").arg(id).arg(lineIndex);
      near(line.width, expected.value(QStringLiteral("width")).toDouble(),
           horizontalTolerance, context + QStringLiteral(" width"));
      near(line.blockHeight, expected.value(QStringLiteral("height")).toDouble(),
           0.25, context + QStringLiteral(" height"));
    }
    QVector<flowchart::FlowLabelVisualRun> mathRuns;
    for (const auto& line : native.lines)
      for (const auto& run : line.runs)
        if (run.math) mathRuns.append(run);
    const QJsonArray expectedMath = fixture.value(QStringLiteral("math")).toArray();
    require(mathRuns.size() == expectedMath.size(),
            QStringLiteral("%1 Math run count: native=%2 browser=%3")
                .arg(id).arg(mathRuns.size()).arg(expectedMath.size()));
    for (qsizetype mathIndex = 0; mathIndex < mathRuns.size(); ++mathIndex) {
      const auto& run = mathRuns.at(mathIndex);
      const QJsonObject expected = expectedMath.at(mathIndex).toObject();
      const QString context = QStringLiteral("%1 Math %2").arg(id).arg(mathIndex);
      near(run.x, expected.value(QStringLiteral("x")).toDouble(),
           horizontalTolerance, context + QStringLiteral(" x"));
      near(run.width, expected.value(QStringLiteral("width")).toDouble(),
           0.25, context + QStringLiteral(" width"));
      near(run.mathBoxHeight, expected.value(QStringLiteral("height")).toDouble(),
           0.25, context + QStringLiteral(" height"));
      const qreal nativeTop =
          (native.lines.first().blockHeight - run.mathBoxHeight) / 2.0;
      near(nativeTop, expected.value(QStringLiteral("y")).toDouble(),
           0.25, context + QStringLiteral(" centered y"));
    }
    mathSpanCount += document.math.size();
  }
  require(mathSpanCount >= 20 && mixedMathCases >= 12,
          QStringLiteral("Flowchart label oracle coverage regressed"));
  require(characterBoxes >= 150 && visualBidiRuns >= 20 && rtlCases >= 5,
          QStringLiteral("Flowchart glyph/bidi oracle coverage regressed"));
#ifdef Q_OS_WIN
  // Plain Arial must follow Chromium's system-font fallback mapping before
  // shaping. These line widths were captured from Mermaid 11.16 in Chrome;
  // they cover CJK, RTL, mixed scripts, emoji, Korean and Greek. Measuring
  // the whole line through Qt's fallback chain misses these by up to 0.8px.
  {
    const auto plainWidth = [](const QString& text) {
      auto document = flowchart::parseFlowLabel(text, QStringLiteral("text"));
      flowchart::prepareFlowLabelMath(document, 16.0);
      return flowchart::layoutFlowLabel(
                 document, QStringLiteral("Arial"), 16.0, 24.0)
          .size.width();
    };
    near(plainWidth(QStringLiteral("\u4e2d\u6587\u6807\u7b7e")),
         64.0, 0.01, QStringLiteral("Arial Chinese fallback width"));
    near(plainWidth(QStringLiteral("\u65e5\u672c\u8a9e\u30c6\u30ad\u30b9\u30c8")),
         111.53125, 0.01, QStringLiteral("Arial Japanese fallback width"));
    near(plainWidth(QStringLiteral("\u05e9\u05dc\u05d5\u05dd \u05e2\u05d5\u05dc\u05dd")),
         65.890625, 0.01, QStringLiteral("Arial Hebrew fallback width"));
    near(plainWidth(QStringLiteral("English \u0627\u0644\u0639\u0631\u0628\u064a\u0629")),
         91.484375, 0.01,
         QStringLiteral("Arial Latin-Arabic fallback width"));
    near(plainWidth(QStringLiteral("Arial \u4e2d\u6587 \u0627\u0644\u0639\u0631\u0628\u064a\u0629 \U0001f600")),
         133.890625, 0.01,
         QStringLiteral("Arial CJK-Arabic-emoji fallback width"));
    near(plainWidth(QStringLiteral("\ud55c\uae00 \u0395\u03bb\u03bb\u03b7\u03bd\u03b9\u03ba\u03ac")),
         100.625, 0.02,
         QStringLiteral("Arial Korean-Greek fallback width"));
    auto arabic = flowchart::parseFlowLabel(
        QStringLiteral("\u0645\u0631\u062d\u0628\u0627 \u0628\u0627\u0644\u0639\u0627\u0644\u0645"),
        QStringLiteral("text"));
    near(flowchart::measureFlowSvgTextBounds(
             arabic, QStringLiteral("Arial"), 16.0).width(),
         64.90625, 0.02,
         QStringLiteral("Arial Arabic SVG glyph-ink width"));
  }
  // The styled inline box measures through REAL shaping (the review's kern
  // counterexample): Arial Bold "AV" at 16px is 21.046875px in Chrome —
  // HarfBuzz 1/128px advances with GPOS kerning — while the nominal hmtx sum
  // is 22.234375 and an unshaped DWrite float sum 21.0390625 (the per-glyph
  // 1/64 rounding lands the A+kern advance on a half sixty-fourth).
  {
    auto document = flowchart::parseFlowLabel(
        QStringLiteral("**AV**"), QStringLiteral("markdown"));
    flowchart::prepareFlowLabelMath(document, 16.0);
    const auto native = flowchart::layoutFlowLabel(
        document, QStringLiteral("Arial"), 16.0, 24.0);
    near(native.size.width(), 21.046875, 0.01,
         QStringLiteral("Arial bold AV kern-shaped width"));
  }
  // RTL segments shape with their RESOLVED direction (AnalyzeBidi feeds
  // SetBidiLevel; AnalyzeScript never does). The advance sum is
  // order-invariant, so Chrome's inline box width is reproducible without
  // reordering: bold "אב" 18.53125, contextual-form Arabic "سلام" 24.15625,
  // mixed "אA" 20.890625 — all Chrome-recorded at Arial Bold 16px.
  {
    auto heb = flowchart::parseFlowLabel(
        QStringLiteral("**אב**"), QStringLiteral("markdown"));
    flowchart::prepareFlowLabelMath(heb, 16.0);
    near(flowchart::layoutFlowLabel(heb, QStringLiteral("Arial"), 16.0, 24.0)
             .size.width(),
         18.53125, 0.01, QStringLiteral("Arial bold Hebrew shaped width"));
    auto arab = flowchart::parseFlowLabel(
        QStringLiteral("**سلام**"),
        QStringLiteral("markdown"));
    flowchart::prepareFlowLabelMath(arab, 16.0);
    near(flowchart::layoutFlowLabel(arab, QStringLiteral("Arial"), 16.0, 24.0)
             .size.width(),
         24.15625, 0.01,
         QStringLiteral("Arial bold Arabic contextual-form width"));
    auto mixed = flowchart::parseFlowLabel(
        QStringLiteral("**אA**"), QStringLiteral("markdown"));
    flowchart::prepareFlowLabelMath(mixed, 16.0);
    near(flowchart::layoutFlowLabel(mixed, QStringLiteral("Arial"), 16.0, 24.0)
             .size.width(),
         20.890625, 0.01,
         QStringLiteral("Arial bold mixed-direction shaped width"));
  }
  // CSS spacing on the styled segment: letter-spacing applies after EVERY
  // grapheme cluster (trailing one and spaces included; a combining mark
  // joins its base), word-spacing after every separator (U+0020/U+00A0), and
  // the two ADD. Chrome-recorded Arial Bold 16px: "A V" 26.078125 plain,
  // 29.078125 letter 1px, 29.078125 word 3px, 32.078125 both — the previous
  // else-if arithmetic gave 29.078125 for the both case (word dropped).
  {
    auto document = flowchart::parseFlowLabel(
        QStringLiteral("**A V**"), QStringLiteral("markdown"));
    flowchart::prepareFlowLabelMath(document, 16.0);
    document.letterSpacingPx = 1.0;
    document.wordSpacingPx = 3.0;
    near(flowchart::layoutFlowLabel(document, QStringLiteral("Arial"), 16.0,
                                    24.0)
             .size.width(),
         32.078125, 0.01,
         QStringLiteral("Arial bold A V letter+word spacing width"));
    document.wordSpacingPx = 0.0;
    near(flowchart::layoutFlowLabel(document, QStringLiteral("Arial"), 16.0,
                                    24.0)
             .size.width(),
         29.078125, 0.01,
         QStringLiteral("Arial bold A V letter-only spacing width"));
    document.letterSpacingPx = 0.0;
    document.wordSpacingPx = 3.0;
    near(flowchart::layoutFlowLabel(document, QStringLiteral("Arial"), 16.0,
                                    24.0)
             .size.width(),
         29.078125, 0.01,
         QStringLiteral("Arial bold A V word-only spacing width"));
    // One letter unit for A + U+0301 (one grapheme cluster): the per-scalar
    // count added 2 and measured 13.5625.
    auto combining = flowchart::parseFlowLabel(
        QStringLiteral("**Á**"), QStringLiteral("markdown"));
    flowchart::prepareFlowLabelMath(combining, 16.0);
    combining.letterSpacingPx = 1.0;
    near(flowchart::layoutFlowLabel(combining, QStringLiteral("Arial"), 16.0,
                                    24.0)
             .size.width(),
         12.5625, 0.01,
         QStringLiteral("Arial bold combining-mark letter unit width"));
    // Grapheme clusters beyond combining marks — all Chrome-recorded at
    // Arial Bold 16px. ZWJ joins (or stands alone and the shaper drops it):
    // "A"+ZWJ+"B" gains exactly TWO letter units; a LONE LEADING mark is its
    // own cluster ("´A" gains 2 — a category-skip counter gave 1); bidi
    // embedding controls are default-ignorable and receive NO unit
    // (RLO+"AB"+PDF gains 2, not 4).
    {
      const auto width = [](flowchart::FlowLabelDocument document,
                            qreal letter) {
        flowchart::prepareFlowLabelMath(document, 16.0);
        document.letterSpacingPx = letter;
        return flowchart::layoutFlowLabel(
                   document, QStringLiteral("Arial"), 16.0, 24.0)
            .size.width();
      };
      const QString zwj = QStringLiteral("**A") + QChar(0x200D) +
                          QStringLiteral("B**");
      near(width(flowchart::parseFlowLabel(zwj, QStringLiteral("markdown")),
                 0.0),
           23.109375, 0.01,
           QStringLiteral("Arial bold A-ZWJ-B zero-width joiner width"));
      near(width(flowchart::parseFlowLabel(zwj, QStringLiteral("markdown")),
                 1.0),
           25.109375, 0.01,
           QStringLiteral("Arial bold A-ZWJ-B letter units (2)"));
      const QString leading = QStringLiteral("**") + QChar(0x0301) +
                              QStringLiteral("A**");
      near(width(flowchart::parseFlowLabel(leading,
                                           QStringLiteral("markdown")), 0.0),
           11.5625, 0.01,
           QStringLiteral("Arial bold leading-mark half-up rounding"));
      near(width(flowchart::parseFlowLabel(leading,
                                           QStringLiteral("markdown")), 1.0),
           13.5625, 0.01,
           QStringLiteral("Arial bold leading-mark letter units (2)"));
      const QString embedding = QStringLiteral("**") + QChar(0x202E) +
                                QStringLiteral("AB") + QChar(0x202C) +
                                QStringLiteral("**");
      near(width(flowchart::parseFlowLabel(embedding,
                                           QStringLiteral("markdown")), 1.0),
           25.109375, 0.01,
           QStringLiteral("Arial bold bidi-embedding controls unspaceable"));
    }
    // Script/bidi INTERSECTION: a digit inside Hebrew keeps one script run
    // but its own bidi level — each atomic run rounds to LayoutUnit
    // independently (Chrome "א1ב" = 597+570+590 = 27.453125; one whole-run
    // rounding would give 27.4375). With letter-spacing: 3 more units.
    {
      auto heb1heb = flowchart::parseFlowLabel(
          QStringLiteral("**א1ב**"), QStringLiteral("markdown"));
      flowchart::prepareFlowLabelMath(heb1heb, 16.0);
      near(flowchart::layoutFlowLabel(heb1heb, QStringLiteral("Arial"), 16.0,
                                      24.0)
               .size.width(),
           27.453125, 0.01,
           QStringLiteral("Arial bold digit-in-Hebrew per-run rounding"));
      heb1heb.letterSpacingPx = 1.0;
      near(flowchart::layoutFlowLabel(heb1heb, QStringLiteral("Arial"), 16.0,
                                      24.0)
               .size.width(),
           30.453125, 0.01,
           QStringLiteral("Arial bold digit-in-Hebrew letter units (3)"));
    }
    // Variation selectors are Mn — NOT Cf: the letter receiver test is the
    // Blink TreatAsZeroWidthSpace UNIT check, so a standalone VS cluster
    // gains NO unit (Chrome: VS16/VS1/FVS1 all 0 wide, plain and
    // letter-spaced alike — the shaper hides them), while attached to a
    // base it joins that base's cluster and glyph ("A"+VS16 11.5625 plain,
    // +1 unit at 1px). The unit check reads a single UTF-16 unit, so
    // NON-BMP ignorables (language tag U+E0001) test their high surrogate
    // and still receive a unit when shaped — a Cf-category scan is wrong in
    // BOTH directions.
    {
      const auto vsWidth = [](const QString& label, qreal letter) {
        auto document = flowchart::parseFlowLabel(
            label, QStringLiteral("markdown"));
        flowchart::prepareFlowLabelMath(document, 16.0);
        document.letterSpacingPx = letter;
        return flowchart::layoutFlowLabel(
                   document, QStringLiteral("Arial"), 16.0, 24.0)
            .size.width();
      };
      const auto vsAlone = [](QChar selector) {
        return QStringLiteral("**") + selector + QStringLiteral("**");
      };
      for (const QChar selector : {QChar(0xFE0F), QChar(0xFE00),
                                   QChar(0x180B)}) {
        const QString hex = QString::number(int(selector.unicode()), 16);
        near(vsWidth(vsAlone(selector), 0.0), 0.0, 0.01,
             QStringLiteral("Arial bold U+%1 hidden width").arg(hex));
        near(vsWidth(vsAlone(selector), 1.0), 0.0, 0.01,
             QStringLiteral("Arial bold U+%1 no letter unit").arg(hex));
      }
      const QString aVs16 = QStringLiteral("**A") + QChar(0xFE0F) +
                            QStringLiteral("**");
      near(vsWidth(aVs16, 0.0), 11.5625, 0.01,
           QStringLiteral("Arial bold A+VS16 cluster width"));
      near(vsWidth(aVs16, 1.0), 12.5625, 0.01,
           QStringLiteral("Arial bold A+VS16 letter units (1)"));
    }
  }
#endif
  qDebug() << "MermaidFlowchartLabelOracleTest:" << cases.size()
           << "cases and" << mathSpanCount << "Math spans passed";
  return 0;
}
