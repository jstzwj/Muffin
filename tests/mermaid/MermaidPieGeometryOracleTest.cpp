// pieDiagram geometry parity oracle: compares Muffin's native pie::PieScene
// against real mermaid 11.16.0 pieDiagram geometry captured by
// scripts/generate_mermaid_pie_geometry_fixture.mjs (headless Chrome). The arc
// path `d` strings are the BYTE-PARITY target (reproduces d3 exactly); angles,
// radii, counts, title and legend census are font-independent and asserted; the
// legend/title pixel positions and viewBox width are font-coupled (printed, not
// failed).
//
// editor::MermaidRenderCache::getSync -> entry.scene -> dynamic_cast to PieScene
// -> toJsonObject().
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/pie/PieScene.h"
#include "mermaid/scene/ParityDiff.h"

#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "%s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

QString accVal(const QJsonObject& o, const QString& key) {
  const QJsonValue v = o.value(key);
  return v.isNull() || v.isUndefined() ? QString() : v.toString();
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();

  require(argc == 2, QStringLiteral("Expected pie geometry fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString() == QLatin1String("11.16.0"),
          QStringLiteral("Pie geometry: mermaidVersion drifted"));

  const parity::Tier num{0.001};

  for (const QJsonValue& cv : root.value(QStringLiteral("cases")).toArray()) {
    const QJsonObject c = cv.toObject();
    const QString id = c.value(QStringLiteral("id")).toString();
    const QString source = c.value(QStringLiteral("source")).toString();
    const QJsonObject expected = c.value(QStringLiteral("expected")).toObject();

    editor::MermaidRenderCache cache;
    const auto entry = cache.getSync(cache.makeKey(source), source);
    const auto* scene = dynamic_cast<const pie::PieScene*>(entry.scene.get());
    require(entry.status == editor::MermaidRenderStatus::Ready && scene != nullptr,
            id + QStringLiteral(": native pie render failed: ") + entry.errorMessage);
    const QJsonObject actual = scene->toJsonObject();

    QStringList errors;

    // Font-independent counts + text.
    if (actual.value(QStringLiteral("legendCount")).toInt() !=
        expected.value(QStringLiteral("legendCount")).toInt())
      errors << id + QStringLiteral(": legendCount mismatch");
    if (actual.value(QStringLiteral("sliceCount")).toInt() !=
        expected.value(QStringLiteral("sliceCount")).toInt())
      errors << id + QStringLiteral(": sliceCount mismatch");
    if (actual.value(QStringLiteral("title")).toString() !=
        expected.value(QStringLiteral("title")).toString())
      errors << id + QStringLiteral(": title mismatch");
    if (accVal(actual, QStringLiteral("accTitle")) !=
        accVal(expected, QStringLiteral("accTitle")))
      errors << id + QStringLiteral(": accTitle mismatch");
    if (accVal(actual, QStringLiteral("accDescr")) !=
        accVal(expected, QStringLiteral("accDescr")))
      errors << id + QStringLiteral(": accDescr mismatch");
    if (actual.value(QStringLiteral("outerRingRadius")).toDouble() !=
        expected.value(QStringLiteral("outerRingRadius")).toDouble())
      errors << id + QStringLiteral(": outerRingRadius mismatch");
    // Canvas HEIGHT is deterministic (legendPosition top/bottom grow it by
    // n*legendHeight) — assert it against the oracle's viewBox height. (Width is
    // font-coupled, compared only loosely elsewhere.)
    {
      const QStringList vb = expected.value(QStringLiteral("viewBox")).toString().split(
          QRegularExpression(QStringLiteral("\\s+")));
      const QJsonArray bnds = actual.value(QStringLiteral("bounds")).toArray();
      if (vb.size() >= 4 && bnds.size() >= 4) {
        const qreal expH = vb.at(3).toDouble();
        errors += parity::compareNumber(bnds.at(3).toDouble(), expH, parity::Tier{0.001},
                                        id + QStringLiteral("/boundsHeight"));
      }
    }

    // Legend text census.
    const QJsonArray expLegends = expected.value(QStringLiteral("legends")).toArray();
    const QJsonArray actLegends = actual.value(QStringLiteral("legends")).toArray();
    if (actLegends.size() != expLegends.size()) {
      errors << id + QStringLiteral(": legend array length mismatch");
    } else {
      for (int i = 0; i < expLegends.size(); ++i)
        if (actLegends.at(i).toObject().value(QStringLiteral("text")).toString() !=
            expLegends.at(i).toObject().value(QStringLiteral("text")).toString())
          errors << id + QStringLiteral("/legend%1 text mismatch").arg(i);
    }

    // Sums (font-independent).
    errors += parity::compareNumber(
        actual.value(QStringLiteral("sums")).toObject().value(QStringLiteral("original")).toDouble(),
        expected.value(QStringLiteral("sums")).toObject().value(QStringLiteral("original")).toDouble(),
        num, id + "/sums/original");
    errors += parity::compareNumber(
        actual.value(QStringLiteral("sums")).toObject().value(QStringLiteral("filtered")).toDouble(),
        expected.value(QStringLiteral("sums")).toObject().value(QStringLiteral("filtered")).toDouble(),
        num, id + "/sums/filtered");

    // Per-slice geometry (pathD byte-exact; angles/radii/centroid within tol).
    const QJsonArray expSlices = expected.value(QStringLiteral("slices")).toArray();
    const QJsonArray actSlices = actual.value(QStringLiteral("slices")).toArray();
    if (actSlices.size() != expSlices.size()) {
      errors << id + QStringLiteral(": slice array length %1 != oracle %2")
                    .arg(actSlices.size()).arg(expSlices.size());
    } else {
      for (int i = 0; i < expSlices.size(); ++i) {
        const QJsonObject es = expSlices.at(i).toObject();
        const QJsonObject as = actSlices.at(i).toObject();
        const QString pfx = id + QStringLiteral("/slice%1").arg(i);
        // pathD: BYTE-EXACT (the headline parity proof).
        if (as.value(QStringLiteral("pathD")).toString() !=
            es.value(QStringLiteral("pathD")).toString())
          errors << pfx + QStringLiteral(": pathD mismatch\n  oracle: %1\n  native: %2")
                        .arg(es.value(QStringLiteral("pathD")).toString(),
                             as.value(QStringLiteral("pathD")).toString());
        if (as.value(QStringLiteral("label")).toString() != es.value(QStringLiteral("label")).toString())
          errors << pfx + QStringLiteral("/label mismatch");
        if (as.value(QStringLiteral("percentage")).toString() !=
            es.value(QStringLiteral("percentage")).toString())
          errors << pfx + QStringLiteral("/percentage mismatch");
        if (as.value(QStringLiteral("fill")).toString() != es.value(QStringLiteral("fill")).toString())
          errors << pfx + QStringLiteral("/fill mismatch");
        for (const QString& k : {QStringLiteral("startAngleDeg"), QStringLiteral("endAngleDeg"),
                                  QStringLiteral("midAngleDeg"), QStringLiteral("centroidX"),
                                  QStringLiteral("centroidY"), QStringLiteral("rawPercentage"),
                                  QStringLiteral("outerRadius"), QStringLiteral("innerRadius"),
                                  QStringLiteral("labelRadius")})
          errors += parity::compareNumber(as.value(k).toDouble(), es.value(k).toDouble(), num,
                                           pfx + QLatin1Char('/') + k);
      }
    }

    if (!errors.isEmpty()) {
      for (const QString& e : errors) std::fprintf(stderr, "%s\n", qPrintable(e));
      fail(id + QStringLiteral(": pie geometry parity regression"));
    }
  }
  return 0;
}
