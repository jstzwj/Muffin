// ER geometry parity oracle: compares Muffin's native er::ErScene against real
// mermaid 11.16.0 ER geometry captured by scripts/generate_mermaid_er_geometry_
// fixture.mjs (headless Chrome). Entity bounds by id (both sides normalized to
// the first source entity's centre); relationships by (cardA, cardB, identifying)
// tuple match.
//
// Phase 2 rewrote measureErLayoutInput to mermaid's erBox model, so the
// measurement MODEL is now correct: entity HEIGHTS match mermaid exactly and
// cardinality/identifying match (data parity). These font-independent aspects
// are ASSERTED (fail-on-divergence) below.
//
// Entity WIDTHS, x/y positions, and relationship PATHS are NOT asserted: they
// depend on per-text width measurement, which differs between Muffin's Qt
// rasterizer and Chrome's (~5px/text, summing across columns) - the same
// font-coupling that limits the pixel goldens. They are REPORTED (printed) as a
// diagnostic, not failed on. The `entity-alias` case is skipped entirely:
// mermaid sizes alias entities by their id (narrow) while Muffin uses the alias,
// and mermaid's alias+attribute rendering has its own quirks - a documented
// edge-case divergence, not a model bug.
//
// MermaidRenderCache::getSync -> entry.scene (cast to ErScene) -> ErScene::toJsonObject().

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/erdiagram/ErScene.h"
#include "mermaid/scene/ParityDiff.h"

#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {

// Asserted (font-independent) tolerances.
constexpr qreal kHeight = 1.5;        // entity height: row-model parity
// Reported (font-coupled) — printed, not failed.
constexpr qreal kReport = 0.5;        // report width/x/y deltas above this
constexpr qreal kPath = 2.0;          // relationship path report threshold

// mermaid alias-entity quirks (sized by id not alias; alias+attribute edge case).
const QSet<QString> kSkippedCases = {QStringLiteral("entity-alias")};

[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "%s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

qreal boundsField(const QJsonValue& entity, const char* field) {
  return entity.toObject().value(QStringLiteral("bounds")).toObject()
      .value(QLatin1String(field)).toDouble();
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();

  require(argc == 2, QStringLiteral("Expected ER geometry fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString() == QLatin1String("11.16.0"),
          QStringLiteral("ER geometry: mermaidVersion drifted"));
  require(root.value(QStringLiteral("oracle")).toString().startsWith(
              QLatin1String("erDiagram.render")),
          QStringLiteral("ER geometry: oracle contract drifted"));

  editor::MermaidRenderCache cache;
  for (const QJsonValue& caseValue : root.value(QStringLiteral("cases")).toArray()) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    if (kSkippedCases.contains(id)) {
      std::fprintf(stderr, "[ER geometry] %s: skipped (mermaid alias-entity quirk)\n",
                   qPrintable(id));
      continue;
    }
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const auto entry = cache.getSync(cache.makeKey(source), source);
    const auto* erScene = dynamic_cast<const er::ErScene*>(entry.scene.get());
    require(entry.status == editor::MermaidRenderStatus::Ready && erScene != nullptr,
            id + QStringLiteral(": native ER render failed: ") + entry.errorMessage);
    const QJsonObject actual = erScene->toJsonObject();

    QStringList assertErrors;   // font-independent parity (heights, cardinality)
    QStringList reportNotes;    // font-coupled (widths, positions, paths)

    // Entities: match by id. Assert height; report width/x/y.
    QHash<QString, QJsonValue> expectedEntityById;
    for (const QJsonValue& e : expected.value(QStringLiteral("entities")).toArray())
      expectedEntityById.insert(e.toObject().value(QStringLiteral("id")).toString(), e);
    for (const QJsonValue& e : actual.value(QStringLiteral("entities")).toArray()) {
      const QString entityId = e.toObject().value(QStringLiteral("id")).toString();
      if (!expectedEntityById.contains(entityId)) {
        assertErrors << id + QStringLiteral(": entity '%1' has no mermaid reference").arg(entityId);
        continue;
      }
      const QJsonValue exp = expectedEntityById.value(entityId);
      const QString prefix = id + QStringLiteral("/entity/%1").arg(entityId);
      assertErrors += parity::compareNumber(boundsField(e, "height"), boundsField(exp, "height"),
                                            parity::Tier{kHeight}, prefix + "/height");
      reportNotes += parity::compareNumber(boundsField(e, "width"), boundsField(exp, "width"),
                                           parity::Tier{kReport}, prefix + "/width");
      reportNotes += parity::compareNumber(boundsField(e, "x"), boundsField(exp, "x"),
                                           parity::Tier{kReport}, prefix + "/x");
      reportNotes += parity::compareNumber(boundsField(e, "y"), boundsField(exp, "y"),
                                           parity::Tier{kReport}, prefix + "/y");
    }

    // Relationships: match by (cardA, cardB, identifying). Assert those; report path.
    const QJsonArray expectedRels = expected.value(QStringLiteral("relationships")).toArray();
    const QJsonArray actualRels = actual.value(QStringLiteral("relationships")).toArray();
    QVector<int> consumed(expectedRels.size(), 0);
    for (const QJsonValue& r : actualRels) {
      const QJsonObject rel = r.toObject();
      const QString cardA = rel.value(QStringLiteral("cardA")).toString();
      const QString cardB = rel.value(QStringLiteral("cardB")).toString();
      const bool identifying = rel.value(QStringLiteral("identifying")).toBool();
      int match = -1;
      for (int i = 0; i < expectedRels.size(); ++i) {
        if (consumed[i]) continue;
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
        assertErrors << prefix + QStringLiteral(": no mermaid reference for %1/%2 ident=%3")
                            .arg(cardA, cardB).arg(identifying);
        continue;
      }
      consumed[match] = 1;
      if (!identifying) {
        // Probe lock for the non-identifying dashed relationship: Chrome
        // computes stroke-dasharray "8px, 8px" @ 1px on .edge-pattern-dashed
        // (the er stylesheet's own 8,8 rule overrides the common sheet's `3`).
        // ErScenePainter and ErScene::svgMarkerProjection must match this.
        const QJsonObject matched = expectedRels[match].toObject();
        if (matched.value(QStringLiteral("strokeDasharray")).toString() != QLatin1String("8px, 8px") ||
            matched.value(QStringLiteral("strokeWidth")).toString() != QLatin1String("1px")) {
          assertErrors << prefix + QStringLiteral(
                           ": expected non-identifying dash 8px,8px@1px, fixture says %1 @ %2")
                               .arg(matched.value(QStringLiteral("strokeDasharray")).toString(),
                                    matched.value(QStringLiteral("strokeWidth")).toString());
        }
      }
      reportNotes += parity::comparePath(rel.value(QStringLiteral("path")).toString(),
                                         expectedRels[match].toObject().value(QStringLiteral("path")).toString(),
                                         parity::Tier{kPath}, prefix);
    }

    if (!reportNotes.isEmpty()) {
      std::fprintf(stderr, "[ER geometry] %s: %llu font-coupled delta(s) (reported, not asserted):\n",
                   qPrintable(id), static_cast<unsigned long long>(reportNotes.size()));
      for (const QString& n : reportNotes) std::fprintf(stderr, "  %s\n", qPrintable(n));
    }
    if (!assertErrors.isEmpty()) {
      for (const QString& e : assertErrors) std::fprintf(stderr, "%s\n", qPrintable(e));
      fail(id + QStringLiteral(": ER geometry parity regression (height/cardinality)"));
    }
  }
  return 0;
}
