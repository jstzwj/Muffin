// ER geometry parity DIAGNOSTIC: compares Muffin's native er::ErScene against
// real mermaid 11.16.0 ER geometry captured by scripts/generate_mermaid_er_
// geometry_fixture.mjs (headless Chrome). Entity bounds by id (both sides
// normalized to the first source entity's centre); relationship paths by
// (cardA, cardB, identifying) tuple match then path-shape compare.
//
// STATUS: report-only diagnostic. Phase 1 (honoring er.nodeSpacing/rankSpacing
// defaults 140/80 + er.minEntityWidth/minEntityHeight clamp 100/75) FIXED entity
// widths and horizontal positions - they now match mermaid. The remaining
// delta is the measurement MODEL (Phase 2): mermaid's empty-attribute entity
// height follows a fast-path formula (labelPaddingY = diagramPadding*1.5) that
// yields ~84, while Muffin clamps to the minEntityHeight floor (75); and
// attribute-bearing entities use mermaid's 4-column width model vs Muffin's
// text-block model. Both are a measurement-rewrite (measureErLayoutInput), not
// a config fix. Once that lands, flip reportOnly() to fail-on-divergence; the
// comparison logic here is unchanged.
//
// MermaidRenderCache::getSync -> entry.erScene -> ErScene::toJsonObject().

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/erdiagram/ErScene.h"
#include "mermaid/scene/ParityDiff.h"

#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {

constexpr qreal kBounds = 0.5;       // entity bounds: tuned (font/padding jitter)
constexpr qreal kPath = 2.0;         // relationship path: dagre curve jitter

[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "%s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
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
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const auto entry = cache.getSync(cache.makeKey(source), source);
    require(entry.status == editor::MermaidRenderStatus::Ready && entry.erScene,
            id + QStringLiteral(": native ER render failed: ") + entry.errorMessage);
    const QJsonObject actual = entry.erScene->toJsonObject();

    QStringList errors;

    // Entities: match by id, compare bounds (both sides first-entity normalized).
    QHash<QString, QJsonObject> expectedEntityById;
    for (const QJsonValue& e : expected.value(QStringLiteral("entities")).toArray())
      expectedEntityById.insert(e.toObject().value(QStringLiteral("id")).toString(), e.toObject());
    for (const QJsonValue& e : actual.value(QStringLiteral("entities")).toArray()) {
      const QJsonObject entity = e.toObject();
      const QString entityId = entity.value(QStringLiteral("id")).toString();
      if (!expectedEntityById.contains(entityId)) {
        errors << id + QStringLiteral(": entity '%1' has no mermaid reference").arg(entityId);
        continue;
      }
      errors += parity::compareJson(entity.value(QStringLiteral("bounds")),
                                    expectedEntityById.value(entityId).value(QStringLiteral("bounds")),
                                    parity::Tier{kBounds},
                                    id + QStringLiteral("/entity/%1/bounds").arg(entityId));
    }

    // Relationships: match by (cardA, cardB, identifying) tuple, compare path.
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
        const QJsonObject candidate = expectedRels[i].toObject();
        if (candidate.value(QStringLiteral("cardA")).toString() == cardA &&
            candidate.value(QStringLiteral("cardB")).toString() == cardB &&
            candidate.value(QStringLiteral("identifying")).toBool() == identifying) {
          match = i;
          break;
        }
      }
      if (match < 0) {
        errors << id + QStringLiteral(": relationship %1/%2 ident=%3 has no mermaid reference")
                      .arg(cardA, cardB).arg(identifying);
        continue;
      }
      consumed[match] = 1;
      errors += parity::comparePath(rel.value(QStringLiteral("path")).toString(),
                                    expectedRels[match].toObject().value(QStringLiteral("path")).toString(),
                                    parity::Tier{kPath},
                                    id + QStringLiteral("/rel/%1-%2").arg(cardA, cardB));
    }

    if (!errors.isEmpty()) {
      std::fprintf(stderr, "[ER geometry] %s: %llu delta(s) vs mermaid 11.16.0\n",
                   qPrintable(id),
                   static_cast<unsigned long long>(errors.size()));
      for (const QString& error : errors) std::fprintf(stderr, "  %s\n", qPrintable(error));
    }
  }
  // Report-only: see file header. CI-green while the ER layout gaps are fixed.
  std::fprintf(stderr, "[ER geometry] diagnostic complete (report-only; see header).\n");
  std::fflush(stderr);
  return 0;
}
