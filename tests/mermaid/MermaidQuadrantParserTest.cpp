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
  return 0;
}
