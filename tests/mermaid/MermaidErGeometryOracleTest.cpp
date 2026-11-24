// Oracle for the erDiagram layout against real Mermaid 11.16.0.
//
// tests/fixtures/mermaid/er-geometry.json is captured by
// scripts/generate_mermaid_er_geometry_fixture.mjs in the bundled Noto fonts,
// which is also what the port measures text in, so nothing here depends on
// the machine's fonts and every number is asserted: entity width, height and
// position, each relationship's path, and the canvas. Entity bounds are
// matched by id and normalized to the first source-order entity's centre;
// relationships are matched by (cardA, cardB, identifying).
//
// The fixture only holds sources whose database the port reads like upstream.
// What it does not (aliases, `direction`, word cardinalities) is listed by
// MermaidErGrammarTest, and until those are fixed their layout is not
// comparable. kKnownDivergences holds the two structures whose layout still
// differs; the list may only shrink.

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/erdiagram/ErScene.h"
#include "mermaid/scene/ParityDiff.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace muffin::mermaid;

namespace {

constexpr qreal kGeometry = 0.05; // entity bounds, path coordinates and canvas (px)

// Structures whose layout still differs from upstream. Rendering follows the
// layout, so each is a visible difference for that diagram.
const QSet<QString> kKnownDivergences = {
    // A relationship from an entity to itself: Mermaid draws the loop below
    // the entity as three path segments and the diagram is 326 px tall. The
    // port draws one path and its diagram is no taller than the entity alone
    // (145 px), so the loop lies over the entity.
    QStringLiteral("self-relationship"),
    // A ring of relationships: every entity and path matches, but Mermaid's
    // canvas is 212 px wide and the port's is 185 px.
    QStringLiteral("ring"),
};

QStringList failures;

void require(bool condition, const QString& message) {
  if (!condition)
    failures << message;
}

qreal boundsField(const QJsonValue& entity, const char* field) {
  return entity.toObject().value(QStringLiteral("bounds")).toObject().value(QLatin1String(field)).toDouble();
}

// The largest coordinate difference between two SVG paths of the same shape.
qreal pathDistance(const QString& a, const QString& b) {
  static const QRegularExpression number(QStringLiteral("-?\\d+(?:\\.\\d+)?(?:e[-+]?\\d+)?"));
  const auto collect = [](const QString& s) {
    QVector<double> values;
    auto it = number.globalMatch(s);
    while (it.hasNext())
      values.append(it.next().captured(0).toDouble());
    return values;
  };
  const QVector<double> left = collect(a);
  const QVector<double> right = collect(b);
  if (left.size() != right.size())
    return std::numeric_limits<qreal>::infinity();
  qreal worst = 0;
  for (qsizetype i = 0; i < left.size(); ++i)
    worst = qMax(worst, qAbs(left.at(i) - right.at(i)));
  return worst;
}

} // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();

  require(argc == 2, QStringLiteral("Expected ER geometry fixture path"));
  if (argc != 2) {
    for (const QString& failure : failures)
      std::fprintf(stderr, "FAIL: %s\n", qPrintable(failure));
    return 1;
  }
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  QByteArray bytes = file.readAll();
  // The generator emits LF; a CRLF checkout must not change the provenance.
  bytes.replace("\r\n", "\n");
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("db377989c396dbd8840329c34729efac7ac9f41df7f295bd2521cf1ba9365d96"),
          QStringLiteral("ER geometry fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString() == QLatin1String("11.16.0"),
          QStringLiteral("ER geometry: mermaidVersion drifted"));
  require(root.value(QStringLiteral("oracle")).toString().startsWith(QLatin1String("erDiagram.render")),
          QStringLiteral("ER geometry: oracle contract drifted"));
  require(root.value(QStringLiteral("fontMode")).toString() == QLatin1String("bundled-noto-2.13b171"),
          QStringLiteral("ER geometry: fontMode drifted"));
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("ab302ceaf07ba096bb41406850d274658b95b2dfa13c9a984369759e040d61a6"),
          QStringLiteral("ER geometry fixture provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 17, QStringLiteral("ER geometry case count drifted: %1").arg(cases.size()));

  editor::MermaidRenderCache cache;
  QSet<QString> stillDiverging;
  qreal worstPathOverall = 0;
  for (const QJsonValue& caseValue : cases) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const auto entry = cache.getSync(cache.makeKey(source), source);
    const auto* erScene = dynamic_cast<const er::ErScene*>(entry.scene.get());
    if (!(entry.status == editor::MermaidRenderStatus::Ready && erScene != nullptr)) {
      failures << id + QStringLiteral(": native ER render failed: ") + entry.errorMessage;
      continue;
    }
    const QJsonObject actual = erScene->toJsonObject();
    QStringList divergences;

    // Entities, matched by id: count, then width, height and position.
    const QJsonArray expectedEntities = expected.value(QStringLiteral("entities")).toArray();
    const QJsonArray actualEntities = actual.value(QStringLiteral("entities")).toArray();
    if (actualEntities.size() != expectedEntities.size())
      divergences << QStringLiteral("%1: %2 entities, upstream %3")
                         .arg(id)
                         .arg(actualEntities.size())
                         .arg(expectedEntities.size());
    QHash<QString, QJsonValue> expectedEntityById;
    for (const QJsonValue& e : expectedEntities)
      expectedEntityById.insert(e.toObject().value(QStringLiteral("id")).toString(), e);
    for (const QJsonValue& e : actualEntities) {
      const QString entityId = e.toObject().value(QStringLiteral("id")).toString();
      if (!expectedEntityById.contains(entityId)) {
        divergences << id + QStringLiteral(": entity '%1' has no mermaid reference").arg(entityId);
        continue;
      }
      const QJsonValue exp = expectedEntityById.value(entityId);
      const QString prefix = id + QStringLiteral("/entity/%1").arg(entityId);
      for (const char* field : {"width", "height", "x", "y"})
        divergences += parity::compareNumber(boundsField(e, field), boundsField(exp, field), parity::Tier{kGeometry},
                                             prefix + QLatin1Char('/') + QLatin1String(field));
    }
    for (const QJsonValue& e : expectedEntities) {
      const QString entityId = e.toObject().value(QStringLiteral("id")).toString();
      bool found = false;
      for (const QJsonValue& a : actualEntities)
        found = found || a.toObject().value(QStringLiteral("id")).toString() == entityId;
      if (!found)
        divergences << QStringLiteral("%1: entity '%2' is missing natively").arg(id, entityId);
    }

    // Relationships, matched by (cardA, cardB, identifying): count, path, and
    // the painted dash and stroke of the non-identifying ones.
    const QJsonArray expectedRels = expected.value(QStringLiteral("relationships")).toArray();
    const QJsonArray actualRels = actual.value(QStringLiteral("relationships")).toArray();
    if (actualRels.size() != expectedRels.size())
      divergences << QStringLiteral("%1: %2 relationship paths, upstream %3")
                         .arg(id)
                         .arg(actualRels.size())
                         .arg(expectedRels.size());
    QVector<int> consumed(expectedRels.size(), 0);
    qreal worstPath = 0;
    for (const QJsonValue& r : actualRels) {
      const QJsonObject rel = r.toObject();
      const QString cardA = rel.value(QStringLiteral("cardA")).toString();
      const QString cardB = rel.value(QStringLiteral("cardB")).toString();
      const bool identifying = rel.value(QStringLiteral("identifying")).toBool();
      int match = -1;
      for (int i = 0; i < expectedRels.size(); ++i) {
        if (consumed[i])
          continue;
        const QJsonObject c = expectedRels[i].toObject();
        if (c.value(QStringLiteral("cardA")).toString() == cardA &&
            c.value(QStringLiteral("cardB")).toString() == cardB &&
            c.value(QStringLiteral("identifying")).toBool() == identifying) {
          match = i;
          break;
        }
      }
      const QString prefix = id + QStringLiteral("/rel/%1-%2").arg(cardA, cardB);
      if (match < 0) {
        divergences << prefix + QStringLiteral(": no mermaid reference for this cardinality pair");
        continue;
      }
      consumed[match] = 1;
      const QJsonObject matched = expectedRels[match].toObject();
      if (!identifying) {
        // Chrome computes stroke-dasharray "8px, 8px" @ 1px on
        // .edge-pattern-dashed (the er stylesheet's own 8,8 rule overrides the
        // common sheet's `3`).
        if (matched.value(QStringLiteral("strokeDasharray")).toString() != QLatin1String("8px, 8px") ||
            matched.value(QStringLiteral("strokeWidth")).toString() != QLatin1String("1px"))
          divergences << prefix + QStringLiteral(": expected non-identifying dash 8px,8px@1px, fixture says %1 @ %2")
                             .arg(matched.value(QStringLiteral("strokeDasharray")).toString(),
                                  matched.value(QStringLiteral("strokeWidth")).toString());
      }
      const QString nativePath = rel.value(QStringLiteral("path")).toString();
      const QString upstreamPath = matched.value(QStringLiteral("path")).toString();
      worstPath = qMax(worstPath, pathDistance(nativePath, upstreamPath));
      divergences += parity::comparePath(nativePath, upstreamPath, parity::Tier{kGeometry}, prefix);
    }

    // Canvas: the drawn diagram is as large as upstream's, and the image is
    // sized the way the browser screenshots it, to the nearest pixel.
    const QJsonObject canvas = expected.value(QStringLiteral("canvas")).toObject();
    const QJsonArray viewBox = canvas.value(QStringLiteral("viewBox")).toArray();
    const QRectF bounds = erScene->bounds;
    divergences += parity::compareNumber(bounds.width(), canvas.value(QStringLiteral("diagramWidth")).toDouble(),
                                         parity::Tier{kGeometry}, id + QStringLiteral("/canvas/width"));
    divergences += parity::compareNumber(bounds.height(), canvas.value(QStringLiteral("diagramHeight")).toDouble(),
                                         parity::Tier{kGeometry}, id + QStringLiteral("/canvas/height"));
    // The image is sized the way the browser screenshots it: the fractional
    // canvas rounded to the nearest pixel. The port rounds its own extent,
    // which matches upstream's within kGeometry; when that extent sits on a
    // half pixel (cjk-labels measures 0.04 px narrower) the two round apart
    // by one pixel, which is reported rather than counted as a divergence.
    const QSize rounded(qRound(bounds.width() + 2 * entry.metadata.diagramPadding),
                        qRound(bounds.height() + 2 * entry.metadata.diagramPadding));
    const bool extentMatches = divergences.isEmpty();
    if (entry.naturalSize != rounded) {
      divergences << QStringLiteral("%1/canvas/size: native %2x%3, its own extent rounds to %4x%5")
                         .arg(id)
                         .arg(entry.naturalSize.width())
                         .arg(entry.naturalSize.height())
                         .arg(rounded.width())
                         .arg(rounded.height());
    } else if (extentMatches && viewBox.size() == 4 && (rounded.width() != qRound(viewBox.at(2).toDouble()) ||
                                                        rounded.height() != qRound(viewBox.at(3).toDouble()))) {
      std::printf("[ER geometry] %s: canvas %dx%d, browser %dx%d: extents agree within %0.2f px and round apart\n",
                  qPrintable(id), rounded.width(), rounded.height(), qRound(viewBox.at(2).toDouble()),
                  qRound(viewBox.at(3).toDouble()), kGeometry);
    }

    if (divergences.isEmpty()) {
      if (kKnownDivergences.contains(id))
        failures << QStringLiteral("%1 no longer diverges; remove it from kKnownDivergences").arg(id);
      worstPathOverall = qMax(worstPathOverall, worstPath);
      continue;
    }
    if (!kKnownDivergences.contains(id)) {
      failures += divergences;
    } else {
      stillDiverging << id;
      for (const QString& divergence : divergences)
        std::fprintf(stderr, "[known] %s\n", qPrintable(divergence));
    }
  }
  for (const QString& id : kKnownDivergences)
    if (!stillDiverging.contains(id))
      failures << QStringLiteral("%1 is listed in kKnownDivergences but does not diverge in this fixture").arg(id);

  for (const QString& failure : failures)
    std::fprintf(stderr, "FAIL: %s\n", qPrintable(failure));
  if (!failures.isEmpty()) {
    std::fprintf(stderr, "MermaidErGeometryOracleTest: %lld divergence(s) from Mermaid 11.16.0 beyond the %lld known\n",
                 static_cast<long long>(failures.size()), static_cast<long long>(kKnownDivergences.size()));
    return 1;
  }
  std::printf("MermaidErGeometryOracleTest passed: 17 cases, %lld still differ from Mermaid 11.16.0 (worst path delta "
              "%0.3f px elsewhere)\n",
              static_cast<long long>(kKnownDivergences.size()), worstPathOverall);
  return 0;
}
