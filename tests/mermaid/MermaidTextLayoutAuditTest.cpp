#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <cstdlib>

namespace {
[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

QString readSource(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly),
          QStringLiteral("Cannot read text-layout source: %1").arg(path));
  return QString::fromUtf8(file.readAll());
}
}  // namespace

int main(int argc, char** argv) {
  require(argc == 2, QStringLiteral("Expected FlowLabel.cpp path"));
  const QFileInfo flowInfo(QString::fromLocal8Bit(argv[1]));
  const QDir sourceRoot(flowInfo.absoluteDir().absoluteFilePath(
      QStringLiteral("../../..")));
  const QString flow = readSource(flowInfo.absoluteFilePath());
  const QString sequence = readSource(sourceRoot.filePath(
      QStringLiteral("src/mermaid/sequence/SequenceLabel.cpp")));
  const QString layout = readSource(sourceRoot.filePath(
      QStringLiteral("src/mermaid/flowchart/FlowchartLayout.cpp")));
  const QString combined = flow + sequence + layout;

  const QStringList forbidden = {
      QStringLiteral("chromiumFallbackWidth"),
      QStringLiteral("kSequenceMathTextScale"),
      QStringLiteral("kWrappedInlineAdvanceScale"),
      QStringLiteral("kSvgWordMeasureScale"),
      QStringLiteral("kSequenceMixedRtlAdvanceScale"),
      QStringLiteral("kSequenceHebrewAdvanceScale"),
      QStringLiteral("kSequenceArabicMessageScale"),
      QStringLiteral("kSequenceMixedDirectionScale"),
      QStringLiteral("kSequenceLatinSvgAdvanceScale"),
      QStringLiteral("33.672 / 34.421875"),
      QStringLiteral("86.281 / 90.359375"),
      QStringLiteral("171.921875 / 170.640625"),
      QStringLiteral("literalMarkdownMathFallback"),
      QStringLiteral("sequenceMathMlModel"),
      QStringLiteral("12.719 / 22.0"),
      QStringLiteral("actualLineHeight, 34.0"),
      QStringLiteral("lineWidth - 1.0"),
      QStringLiteral("containerHeight / 2.0 + 6.0"),
      QStringLiteral("0x2e80"),
      QStringLiteral("0x0600"),
  };
  for (const QString& token : forbidden)
    require(!combined.contains(token),
            QStringLiteral("Forbidden empirical text-layout correction returned: %1")
                .arg(token));

  require(sequence.contains(QStringLiteral("flowchart::layoutFlowLabel")) &&
              sequence.contains(QStringLiteral("measureFlowTextInkWidth")) &&
              !sequence.contains(QStringLiteral("QTextLayout")) &&
              !sequence.contains(QStringLiteral("QFontMetrics")) &&
              !sequence.contains(QStringLiteral("QGlyphRun")),
          QStringLiteral("Sequence label measurement bypassed the shared shaping path"));
  require(layout.contains(QStringLiteral("measureFlowTextAdvanceWidth")) &&
              !layout.contains(QStringLiteral("AdvanceScale")),
          QStringLiteral("Flowchart edge wrapping bypassed unified glyph advances"));
  require(flow.contains(QStringLiteral("OpenTypeHorizontalMetrics")) &&
              flow.contains(QStringLiteral("openTypeFontBoundingMetrics")) &&
              flow.contains(QStringLiteral("mathMlInlineInkRight")) &&
              flow.contains(QStringLiteral("visibleDomTextRanges")) &&
              flow.contains(QStringLiteral("boundingRect(glyphs.at(i))")),
          QStringLiteral("Chromium text box model lost font-table, DOM, or glyph-bound inputs"));

  qDebug() << "MermaidTextLayoutAuditTest: unified text-layout invariants passed";
  return 0;
}
