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

  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 20,
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
