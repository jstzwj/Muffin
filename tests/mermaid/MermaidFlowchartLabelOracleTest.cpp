#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/flowchart/FlowLabel.h"

#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <cstdlib>
#include <algorithm>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
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
  qDebug() << "MermaidFlowchartLabelOracleTest:" << cases.size()
           << "cases and" << mathSpanCount << "Math spans passed";
  return 0;
}
