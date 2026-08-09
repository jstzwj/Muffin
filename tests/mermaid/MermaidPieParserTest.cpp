// pieDiagram parser-leniency oracle. Iterates the frozen 33-case
// tests/fixtures/mermaid/pie-grammar.json (accept/reject verdicts captured live
// from mermaid 11.16.0) and asserts the native pipeline reproduces every
// verdict AND the accepted DB state (sections/showData/title/accTitle/accDescr
// + the renderer-side draw/legend counts).
//
// No-diagram rejects (missing/wrong `pie` keyword) are asserted at the detector
// (detectDiagramType throws UnknownDiagramError); the rest are asserted at the
// parser (PieDiagram::parse throws PieParseError). The runtime negative-value
// reject additionally checks the addSection error phrase. Exact Langium
// lexer/parser error messages are implementation artifacts and are NOT
// reproduced — only the verdicts and parsed state are parity-relevant.
//
// Fixture-driven (argv[1]); QCoreApplication (parser-only, no GUI).
#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/pie/PieDiagram.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

QString accVal(const QJsonObject& db, const QString& key) {
  // Oracle null accTitle/accDescr maps to the parser's empty string.
  const QJsonValue v = db.value(key);
  return v.isNull() || v.isUndefined() ? QString() : v.toString();
}

bool nearEqual(double a, double b) { return std::fabs(a - b) < 1e-9; }
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected pie grammar fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("oracle")).toString().contains(QStringLiteral("pieDiagram")),
          QStringLiteral("Pie grammar: oracle contract drifted"));

  for (const QJsonValue& cv : root.value(QStringLiteral("cases")).toArray()) {
    const QJsonObject c = cv.toObject();
    const QString id = c.value(QStringLiteral("id")).toString();
    const QString input = c.value(QStringLiteral("input")).toString();
    const bool accept = c.value(QStringLiteral("accept")).toBool();

    // Detection: no-diagram rejects never reach the parser.
    QString detected;
    bool detectedPie = false;
    try {
      detected = detectDiagramType(input, QJsonObject());
      detectedPie = (detected == QStringLiteral("pie"));
    } catch (const UnknownDiagramError&) {
      detectedPie = false;
    }
    if (!detectedPie) {
      const QString rejectClass =
          c.value(QStringLiteral("reject")).toObject().value(QStringLiteral("class")).toString();
      require(rejectClass == QStringLiteral("no-diagram"),
              id + QStringLiteral(": non-pie detection, expected no-diagram reject, got class=") +
                  rejectClass);
      continue;
    }

    if (accept) {
      pie::PieData d;
      try {
        d = pie::PieDiagram::parse(input);
      } catch (const pie::PieParseError& e) {
        fail(id + QStringLiteral(": accepted input threw: ") + QString::fromUtf8(e.what()));
      }
      const QJsonObject db = c.value(QStringLiteral("expectedDb")).toObject();
      // Sections: label + value, in order.
      const QJsonArray expSections = db.value(QStringLiteral("sections")).toArray();
      require(d.sections.size() == expSections.size(),
              id + QStringLiteral(": section count %1 != oracle %2")
                      .arg(d.sections.size()).arg(expSections.size()));
      for (int i = 0; i < expSections.size(); ++i) {
        const QJsonObject es = expSections.at(i).toObject();
        require(d.sections.at(i).label == es.value(QStringLiteral("label")).toString(),
                id + QStringLiteral("/section%1 label '%2' != oracle '%3'")
                        .arg(i).arg(d.sections.at(i).label, es.value(QStringLiteral("label")).toString()));
        require(nearEqual(d.sections.at(i).value, es.value(QStringLiteral("value")).toDouble()),
                id + QStringLiteral("/section%1 value mismatch").arg(i));
      }
      require(d.showData == db.value(QStringLiteral("showData")).toBool(),
              id + QStringLiteral(": showData mismatch"));
      require(d.title == db.value(QStringLiteral("title")).toString(),
              id + QStringLiteral(": title mismatch"));
      require(d.accTitle == accVal(db, QStringLiteral("accTitle")),
              id + QStringLiteral(": accTitle mismatch"));
      require(d.accDescr == accVal(db, QStringLiteral("accDescr")),
              id + QStringLiteral(": accDescr mismatch"));
      // Renderer-side counts.
      require(d.sections.size() == db.value(QStringLiteral("legendCount")).toDouble(),
              id + QStringLiteral(": legendCount mismatch"));
      require(pie::pieDrawCount(d.sections) == db.value(QStringLiteral("drawCount")).toDouble(),
              id + QStringLiteral(": drawCount mismatch"));
    } else {
      const QJsonObject reject = c.value(QStringLiteral("reject")).toObject();
      const QString rejectClass = reject.value(QStringLiteral("class")).toString();
      bool threw = false;
      QString msg;
      try {
        pie::PieDiagram::parse(input);
      } catch (const pie::PieParseError& e) {
        threw = true;
        msg = QString::fromUtf8(e.what());
      }
      require(threw, id + QStringLiteral(": rejected input was accepted by the native parser"));
      // Runtime negative-value rejects reproduce the addSection error phrase.
      if (rejectClass == QStringLiteral("runtime"))
        require(msg.contains(QStringLiteral("Negative values")),
                id + QStringLiteral(": runtime reject message missing phrase: ") + msg);
    }
  }

  // Boundaries found by differential probing against the real Langium parser.
  // Keep these local until the frozen grammar fixture is regenerated from the
  // updated probe: they prevent the native parser from silently drifting back
  // to JSON-number or ad-hoc string semantics.
  {
    const pie::PieData d = pie::PieDiagram::parse(
        QStringLiteral("pie\n\"leading\" : 01.5\n\"fraction\" : 00.5"));
    require(d.sections.size() == 2, QStringLiteral("leading-zero fractions accepted"));
    require(nearEqual(d.sections.at(0).value, 1.5) &&
                nearEqual(d.sections.at(1).value, 0.5),
            QStringLiteral("leading-zero fraction values decoded"));
  }
  {
    const pie::PieData d = pie::PieDiagram::parse(
        QStringLiteral("pie\n\"A\\nB\\tC\\rD\\bE\\fF\\vG\" : 1"));
    QString expected = QStringLiteral("A\nB\tC\rD");
    expected += QChar(u'\b');
    expected += QStringLiteral("E");
    expected += QChar(u'\f');
    expected += QStringLiteral("F");
    expected += QChar(u'\v');
    expected += QStringLiteral("G");
    require(d.sections.size() == 1 && d.sections.first().label == expected,
            QStringLiteral("quoted label escape decoding"));
  }
  {
    const pie::PieData d = pie::PieDiagram::parse(
        QStringLiteral("pie\naccDescr {first line\nsecond line}\n\"A\" : 1"));
    require(d.accDescr == QStringLiteral("first line\nsecond line"),
            QStringLiteral("multiline accDescr body"));
  }
  {
    bool threw = false;
    try {
      pie::PieDiagram::parse(QStringLiteral("pie\n\"A%%B\" : 1"));
    } catch (const pie::PieParseError&) {
      threw = true;
    }
    require(threw, QStringLiteral("%% starts a comment even inside a quoted label"));
  }
  return 0;
}
