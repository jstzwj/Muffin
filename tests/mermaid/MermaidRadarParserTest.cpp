// Radar parser/DB oracle. Every case is captured from Mermaid 11.16.0's
// source-entry parser in tests/fixtures/mermaid/radar-grammar.json.
#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/radar/RadarDiagram.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

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

double oracleNumber(const QJsonValue& value) {
  if (value.isDouble()) return value.toDouble();
  const QString text = value.toString();
  if (text == QLatin1String("NaN"))
    return std::numeric_limits<double>::quiet_NaN();
  if (text == QLatin1String("Infinity") || text == QLatin1String("+Infinity"))
    return std::numeric_limits<double>::infinity();
  if (text == QLatin1String("-Infinity"))
    return -std::numeric_limits<double>::infinity();
  bool ok = false;
  const double result = text.toDouble(&ok);
  require(ok, QStringLiteral("Invalid oracle number: ") + text);
  return result;
}

void compareNumber(double actual, const QJsonValue& expected,
                   const QString& path) {
  const double wanted = oracleNumber(expected);
  if (std::isnan(wanted)) {
    require(std::isnan(actual), path + QStringLiteral(": expected NaN"));
    return;
  }
  if (std::isinf(wanted)) {
    require(std::isinf(actual) &&
                std::signbit(actual) == std::signbit(wanted),
            path + QStringLiteral(": infinity mismatch"));
    return;
  }
  require(std::isfinite(actual) && actual == wanted,
          path + QStringLiteral(": %1 != %2")
                     .arg(actual, 0, 'g', 17)
                     .arg(wanted, 0, 'g', 17));
  if (wanted == 0.0)
    require(std::signbit(actual) == std::signbit(wanted),
            path + QStringLiteral(": zero sign mismatch"));
}

void compareDb(const radar::RadarData& actual, const QJsonObject& expected,
               const QString& id) {
  require(actual.title == expected.value(QStringLiteral("title")).toString(),
          id + QStringLiteral("/title mismatch"));
  require(actual.accTitle ==
              expected.value(QStringLiteral("accTitle")).toString(),
          id + QStringLiteral("/accTitle mismatch"));
  require(actual.accDescr ==
              expected.value(QStringLiteral("accDescription")).toString(),
          id + QStringLiteral("/accDescription mismatch"));

  const QJsonArray axes = expected.value(QStringLiteral("axes")).toArray();
  require(actual.axes.size() == axes.size(),
          id + QStringLiteral("/axes count %1 != %2")
                   .arg(actual.axes.size())
                   .arg(axes.size()));
  for (qsizetype i = 0; i < actual.axes.size(); ++i) {
    const QJsonObject axis = axes.at(i).toObject();
    require(actual.axes.at(i).name ==
                    axis.value(QStringLiteral("name")).toString() &&
                actual.axes.at(i).label ==
                    axis.value(QStringLiteral("label")).toString(),
            id + QStringLiteral("/axes/%1 mismatch").arg(i));
  }

  const QJsonArray curves = expected.value(QStringLiteral("curves")).toArray();
  require(actual.curves.size() == curves.size(),
          id + QStringLiteral("/curves count %1 != %2")
                   .arg(actual.curves.size())
                   .arg(curves.size()));
  for (qsizetype i = 0; i < actual.curves.size(); ++i) {
    const radar::RadarCurve& curve = actual.curves.at(i);
    const QJsonObject oracle = curves.at(i).toObject();
    const QString path = id + QStringLiteral("/curves/%1").arg(i);
    require(curve.name == oracle.value(QStringLiteral("name")).toString() &&
                curve.label == oracle.value(QStringLiteral("label")).toString(),
            path + QStringLiteral(": identity mismatch"));
    const QJsonArray entries =
        oracle.value(QStringLiteral("entries")).toArray();
    require(curve.entries.size() == entries.size(),
            path + QStringLiteral("/entries count mismatch"));
    for (qsizetype j = 0; j < curve.entries.size(); ++j)
      compareNumber(curve.entries.at(j), entries.at(j),
                    path + QStringLiteral("/entries/%1").arg(j));
  }

  const QJsonObject options =
      expected.value(QStringLiteral("options")).toObject();
  require(actual.options.showLegend ==
              options.value(QStringLiteral("showLegend")).toBool(),
          id + QStringLiteral("/options/showLegend mismatch"));
  compareNumber(actual.options.ticks, options.value(QStringLiteral("ticks")),
                id + QStringLiteral("/options/ticks"));
  compareNumber(actual.options.min, options.value(QStringLiteral("min")),
                id + QStringLiteral("/options/min"));
  const QJsonValue maximum = options.value(QStringLiteral("max"));
  require(actual.options.hasMax == !maximum.isNull(),
          id + QStringLiteral("/options/hasMax mismatch"));
  if (!maximum.isNull())
    compareNumber(actual.options.max, maximum,
                  id + QStringLiteral("/options/max"));
  require(actual.options.graticule ==
              options.value(QStringLiteral("graticule")).toString(),
          id + QStringLiteral("/options/graticule mismatch"));
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected radar grammar fixture path"));

  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  QJsonParseError jsonError;
  const QJsonDocument document =
      QJsonDocument::fromJson(file.readAll(), &jsonError);
  require(jsonError.error == QJsonParseError::NoError,
          QStringLiteral("Radar grammar JSON: ") + jsonError.errorString());
  const QJsonObject root = document.object();
  require(root.value(QStringLiteral("upstream")).toObject()
                      .value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("Radar grammar Mermaid version drifted"));
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("fa6819c687cc72f40ac0f4efc26e790403606b320c17f5f1f69da3b5764d9708"),
          QStringLiteral("Radar grammar fixture changed; audit its contract"));

  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  int accepted = 0;
  int rejected = 0;
  for (const QJsonValue& caseValue : cases) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const bool accept = fixture.value(QStringLiteral("accept")).toBool();

    if (accept) {
      ++accepted;
      const MermaidPreprocessResult pre = preprocessDiagram(source);
      bool detected = false;
      try {
        detected = detectDiagramType(pre.code, pre.config) ==
                   QLatin1String("radar");
      } catch (const UnknownDiagramError&) {
      }
      require(detected, id + QStringLiteral(": accepted source was not detected"));

      try {
        compareDb(radar::RadarDiagram::parse(source),
                  fixture.value(QStringLiteral("expectedDb")).toObject(), id);
      } catch (const radar::RadarParseError& error) {
        fail(id + QStringLiteral(": accepted source threw at line %1: %2")
                      .arg(error.line)
                      .arg(QString::fromUtf8(error.what())));
      }
      continue;
    }

    ++rejected;
    bool threw = false;
    try {
      (void)radar::RadarDiagram::parse(source);
    } catch (const radar::RadarParseError& error) {
      threw = true;
      require(error.line >= 1 && QString::fromUtf8(error.what()).size() > 0,
              id + QStringLiteral(": rejection lacks a useful diagnostic"));
      const QString upstreamMessage =
          fixture.value(QStringLiteral("reject")).toObject()
              .value(QStringLiteral("message")).toString();
      if (upstreamMessage ==
              QLatin1String("Axes must be populated before curves for reference entries") ||
          upstreamMessage.startsWith(QLatin1String("Missing entry for axis ")))
        require(QString::fromUtf8(error.what()) == upstreamMessage,
                id + QStringLiteral(": semantic DB error mismatch"));
    }
    require(threw, id + QStringLiteral(": upstream-rejected source parsed"));
  }

  require(cases.size() == 55 && accepted == 29 && rejected == 26,
          QStringLiteral("Radar grammar table was not fully visited"));
  try {
    (void)radar::RadarDiagram::parse(QStringLiteral("not-radar\naxis A"));
    fail(QStringLiteral("Missing Radar header unexpectedly parsed"));
  } catch (const radar::RadarParseError& error) {
    require(error.line == 1 && error.column == 1,
            QStringLiteral("Radar start-of-source diagnostic column drifted"));
  }
  std::puts("MermaidRadarParserTest: 55 source-entry cases passed");
  return 0;
}
