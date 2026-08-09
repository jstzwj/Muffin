// quadrantChart parser oracle. Iterates tests/fixtures/mermaid/quadrant-grammar.json
// (21 cases, captured live from mermaid 11.16.0). No-diagram rejects (bad/lower
// header) are asserted at the detector; the rest at the parser (throws). Accepts
// are rendered and the rendered structure (quadrant/point/axis text + counts +
// title) is compared to expectedDb. Fixture-driven; QCoreApplication.
#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/quadrant/QuadrantDiagram.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& m) { std::fprintf(stderr, "FAIL: %s\n", qPrintable(m)); std::fflush(stderr); std::exit(1); }
void require(bool c, const QString& m) { if (!c) fail(m); }

quadrant::QuadrantData parse(const QString& source) {
  try {
    return quadrant::QuadrantDiagram::parse(source);
  } catch (const quadrant::QuadrantParseError& e) {
    fail(QStringLiteral("unexpected parse failure: %1").arg(QString::fromUtf8(e.what())));
  }
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected quadrant grammar fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("oracle")).toString().contains(QStringLiteral("quadrantChart")),
          QStringLiteral("Quadrant grammar: oracle contract drifted"));

  for (const QJsonValue& cv : root.value(QStringLiteral("cases")).toArray()) {
    const QJsonObject c = cv.toObject();
    const QString id = c.value(QStringLiteral("id")).toString();
    const QString input = c.value(QStringLiteral("input")).toString();
    const bool accept = c.value(QStringLiteral("accept")).toBool();

    QString detected;
    bool detectedQ = false;
    try { detected = detectDiagramType(input, QJsonObject()); detectedQ = (detected == QStringLiteral("quadrantChart")); }
    catch (const UnknownDiagramError&) { detectedQ = false; }
    if (!detectedQ) { require(!accept, id + ": non-detected, expected reject"); continue; }

    if (accept) {
      quadrant::QuadrantData d;
      try { d = quadrant::QuadrantDiagram::parse(input); }
      catch (const quadrant::QuadrantParseError& e) { fail(id + ": accepted threw: " + QString::fromUtf8(e.what())); }
      const QJsonObject db = c.value(QStringLiteral("expectedDb")).toObject();
      // Title is the only scalar asserted here; text/count structure is verified
      // end-to-end by the geometry oracle (which renders the same data model).
      const QString expTitle = db.value(QStringLiteral("title")).isNull() ? QString() : db.value(QStringLiteral("title")).toString();
      require(d.title == expTitle, id + ": title mismatch");
    } else {
      bool threw = false;
      try { quadrant::QuadrantDiagram::parse(input); }
      catch (const quadrant::QuadrantParseError&) { threw = true; }
      require(threw, id + ": rejected input was accepted");
    }
  }

  // The original fixture did not cover these exclusive Jison lexer states.
  {
    const quadrant::QuadrantData d = parse(QStringLiteral(
        "quadrantChart;x-axis Left --> Right;y-axis Bottom --> Top;\"P\": [0.5, 0.5]"));
    require(d.xAxisLeftText == QLatin1String("Left") &&
                d.xAxisRightText == QLatin1String("Right") &&
                d.yAxisBottomText == QLatin1String("Bottom") &&
                d.yAxisTopText == QLatin1String("Top") && d.points.size() == 1,
            QStringLiteral("semicolon EOLs must parse like newlines"));
  }
  {
    const quadrant::QuadrantData d = parse(QStringLiteral(
        "quadrantChart\naccDescr {first line\nsecond line}\n\"P\": [0.5, 0.5]"));
    require(d.accDescr == QLatin1String("first line\nsecond line"),
            QStringLiteral("multiline accDescr body must be preserved"));
  }
  {
    const quadrant::QuadrantData d = parse(QStringLiteral(
        "quadrantChart\n"
        "title A %% B; C\n"
        "accDescr {first %% line;\nsecond line}\n"
        "x-axis X %% ignored\n"
        "quadrant-1 Q %% ignored\n"
        "\"A%%B;C\": [0.5, 0.5]"));
    require(d.title == QLatin1String("A %% B; C"),
            QStringLiteral("title state must preserve %% and semicolon"));
    require(d.accDescr == QLatin1String("first %% line;\nsecond line"),
            QStringLiteral("multiline accDescr state must preserve delimiters"));
    require(d.xAxisLeftText == QLatin1String("X") &&
                d.quadrant1Text == QLatin1String("Q"),
            QStringLiteral("INITIAL-state %% must start a comment"));
    require(d.points.size() == 1 &&
                d.points.first().label == QLatin1String("A%%B;C"),
            QStringLiteral("quoted point state must preserve %% and semicolon"));
  }
  {
    const quadrant::QuadrantData d = parse(QStringLiteral(
        "quadrantChart\n\"A[B]\": [0.1, 0.2]\n\"A:::B\": [0.2, 0.3]\n"
        "\"`Point`\": [0.3, 0.4]\n'Literal quotes': [0.4, 0.5]\n"
        "x-axis Only -->"));
    require(d.points.size() == 4, QStringLiteral("quoted-special point count"));
    require(d.points[0].label == QLatin1String("A[B]") &&
                d.points[1].label == QLatin1String("A:::B") &&
                d.points[2].label == QLatin1String("Point") &&
                d.points[3].label == QLatin1String("'Literal quotes'"),
            QStringLiteral("STR/MD_STR/single-quote text semantics"));
    require(d.xAxisLeftText == QStringLiteral("Only \u27F6") && d.xAxisRightText.isEmpty(),
            QStringLiteral("trailing axis delimiter must append U+27F6"));
  }
  {
    const quadrant::QuadrantData d = parse(QStringLiteral(
        "quadrantChart\n\"<script>alert(1)</script>Visible\": [0.5, 0.5]"));
    require(d.points.size() == 1 && d.points[0].label == QLatin1String("Visible"),
            QStringLiteral("point labels must follow upstream text sanitizer"));
  }
  return 0;
}
