#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/sequence/SequenceLabel.h"

#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) { qCritical().noquote() << message; std::exit(1); }
void require(bool condition, const QString& message) { if (!condition) fail(message); }
void near(qreal actual, qreal expected, qreal tolerance, const QString& context) {
  require(std::abs(actual - expected) <= tolerance,
          QStringLiteral("%1: native=%2 browser=%3 tolerance=%4")
              .arg(context).arg(actual).arg(expected).arg(tolerance));
}

sequence::SequenceLabelKind labelKind(const QString& value) {
  if (value == QLatin1String("participant")) return sequence::SequenceLabelKind::Participant;
  if (value == QLatin1String("note")) return sequence::SequenceLabelKind::Note;
  if (value == QLatin1String("fragment")) return sequence::SequenceLabelKind::Fragment;
  if (value == QLatin1String("box")) return sequence::SequenceLabelKind::Box;
  return sequence::SequenceLabelKind::Message;
}

struct VisualRun {
  qreal x = 0.0;
  qreal width = 0.0;
  bool rtl = false;
  bool math = false;
};

QVector<VisualRun> normalizedRuns(const sequence::SequenceLabelLineMetrics& line) {
  QVector<VisualRun> runs;
  for (const auto& source : line.runs) {
    VisualRun run{source.x, source.width, source.rightToLeft, source.math};
    if (!runs.isEmpty() && runs.last().rtl == run.rtl && runs.last().math == run.math) {
      const qreal left = std::min(runs.last().x, run.x);
      const qreal right = std::max(runs.last().x + runs.last().width, run.x + run.width);
      runs.last().x = left;
      runs.last().width = right - left;
    } else {
      runs.append(run);
    }
  }
  return runs;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected sequence label fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Cannot open sequence label fixture"));
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString() == QLatin1String("11.16.0") &&
              root.value(QStringLiteral("fontMode")).toString() ==
                  QLatin1String("bundled-noto-stix-two-math-2.13b171"),
          QStringLiteral("Sequence label oracle version/font drifted"));
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("97a5df110fe49851d3ea0ba4f2751562ec9d224635f5c71f6fe7492f5759b1d9"),
          QStringLiteral("Sequence label fixture changed; audit browser geometry and update digest"));

  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 24, QStringLiteral("Sequence label case count regressed"));
  int lineCount = 0, runCount = 0, rtlRuns = 0, mathRuns = 0;
  int svgVerticalCases = 0, foreignObjectVerticalCases = 0;
  const QString fontFamily = MermaidFontRegistry::cssFamilyStack();
  const flowchart::FlowLabelFontMetrics nativeFontMetrics =
      flowchart::flowLabelFontBoundingMetrics(fontFamily, 16.0);
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const bool complexMath = id.startsWith(QLatin1String("note-math-"));
    auto document = sequence::parseSequenceLabel(fixture.value(QStringLiteral("label")).toString(),
                                                 labelKind(fixture.value(QStringLiteral("kind")).toString()));
    const qreal wrapWidth = fixture.value(QStringLiteral("wrapWidth")).toDouble();
    if (wrapWidth > 0.0)
      document = sequence::wrapSequenceLabel(std::move(document), fontFamily, 16.0, wrapWidth);
    const auto native = sequence::layoutSequenceLabel(document, fontFamily, 16.0, 22.0);
    const QJsonArray expectedLines = fixture.value(QStringLiteral("lines")).toArray();
    require(native.lines.size() == expectedLines.size(),
            QStringLiteral("%1 line count: native=%2 browser=%3")
                .arg(id).arg(native.lines.size()).arg(expectedLines.size()));
    if (document.richText.math.isEmpty())
      require(document.richText.text == fixture.value(QStringLiteral("text")).toString(),
              QStringLiteral("%1 visible text/newline mismatch: native=[%2] browser=[%3]")
                  .arg(id, document.richText.text,
                       fixture.value(QStringLiteral("text")).toString()));
    const QJsonObject expectedBox = fixture.value(QStringLiteral("box")).toObject();
    const QJsonObject vertical =
        fixture.value(QStringLiteral("vertical")).toObject();
    const QJsonObject browserFontMetrics =
        vertical.value(QStringLiteral("fontMetrics")).toObject();
    near(nativeFontMetrics.ascent,
         browserFontMetrics.value(QStringLiteral("fontBoundingBoxAscent")).toDouble(),
         0.01, id + QStringLiteral(" OpenType hhea ascent"));
    near(nativeFontMetrics.descent,
         browserFontMetrics.value(QStringLiteral("fontBoundingBoxDescent")).toDouble(),
         0.01, id + QStringLiteral(" OpenType hhea descent"));
    const bool svgText =
        vertical.value(QStringLiteral("context")).toString() ==
        QLatin1String("svg-text");
    svgVerticalCases += svgText;
    foreignObjectVerticalCases += !svgText;
    near(native.size.width(), expectedBox.value(QStringLiteral("width")).toDouble(),
         complexMath ? 0.2 : 2.0,
         id + QStringLiteral(" box width"));
    near(native.size.height(), expectedBox.value(QStringLiteral("height")).toDouble(),
         complexMath ? 0.2 : 2.0,
         id + QStringLiteral(" box height"));

    for (qsizetype lineIndex = 0; lineIndex < native.lines.size(); ++lineIndex) {
      const auto& line = native.lines[lineIndex];
      const QJsonObject expected = expectedLines[lineIndex].toObject();
      const QString context = QStringLiteral("%1 line %2").arg(id).arg(lineIndex);
      if (document.richText.math.isEmpty() &&
          fixture.value(QStringLiteral("logicalComparable")).toBool(true)) {
        require(line.start == expected.value(QStringLiteral("start")).toInteger() &&
                    line.length == expected.value(QStringLiteral("length")).toInteger(),
                context + QStringLiteral(" logical range mismatch"));
      }
      near(line.width, expected.value(QStringLiteral("width")).toDouble(),
           0.2,
           context + QStringLiteral(" width"));
      near(line.baseline, expected.value(QStringLiteral("baseline")).toDouble(),
           0.25,
           context + QStringLiteral(" baseline"));
      near(line.ascent, expected.value(QStringLiteral("ascent")).toDouble(),
           0.25,
           context + QStringLiteral(" ascent"));
      near(line.descent, expected.value(QStringLiteral("descent")).toDouble(),
           0.25,
           context + QStringLiteral(" descent"));
      if (svgText) {
        const QString baseline =
            vertical.value(QStringLiteral("style")).toObject()
                .value(QStringLiteral("dominantBaseline")).toString();
        if (baseline == QLatin1String("middle"))
          near(line.baseline, nativeFontMetrics.middleBaseline(), 0.02,
               context + QStringLiteral(" SVG middle baseline"));
        else if (baseline == QLatin1String("central"))
          near(line.baseline, line.height / 2.0, 0.01,
               context + QStringLiteral(" SVG central baseline"));
        else if (baseline == QLatin1String("auto"))
          near(line.baseline, nativeFontMetrics.ascent, 0.01,
               context + QStringLiteral(" SVG alphabetic baseline"));
      }

      const QVector<VisualRun> runs = normalizedRuns(line);
      const QJsonArray expectedRuns = expected.value(QStringLiteral("runs")).toArray();
      require(runs.size() == expectedRuns.size(),
              context + QStringLiteral(" visual run count mismatch: native=%1 browser=%2")
                            .arg(runs.size()).arg(expectedRuns.size()));
      for (qsizetype runIndex = 0; runIndex < runs.size(); ++runIndex) {
        const QJsonObject expectedRun = expectedRuns[runIndex].toObject();
        const QString runContext = context + QStringLiteral(" run %1").arg(runIndex);
        require(runs[runIndex].rtl == expectedRun.value(QStringLiteral("rightToLeft")).toBool() &&
                    runs[runIndex].math == expectedRun.value(QStringLiteral("math")).toBool(),
                runContext + QStringLiteral(" direction/type mismatch"));
        near(runs[runIndex].x, expectedRun.value(QStringLiteral("x")).toDouble(),
             0.2,
             runContext + QStringLiteral(" x"));
        near(runs[runIndex].width, expectedRun.value(QStringLiteral("width")).toDouble(),
             0.2,
             runContext + QStringLiteral(" width"));
        rtlRuns += runs[runIndex].rtl;
        mathRuns += runs[runIndex].math;
      }
      ++lineCount;
      runCount += runs.size();
    }
  }
  require(lineCount >= 38 && runCount >= 58 && rtlRuns >= 8 && mathRuns >= 9,
          QStringLiteral("Sequence label line/run coverage regressed"));
  require(svgVerticalCases >= 17 && foreignObjectVerticalCases >= 7,
          QStringLiteral("Sequence vertical formatting-context coverage regressed"));
  qDebug() << "MermaidSequenceLabelOracleTest:" << cases.size() << "cases," << lineCount
           << "lines and" << runCount << "browser runs passed";
  return 0;
}
