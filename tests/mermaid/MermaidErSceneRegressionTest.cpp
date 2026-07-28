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

// Pin this disclaimer so nobody mistakes the fixture for real-mermaid parity.
const QString kOracle =
    QStringLiteral("ErScene::toJsonObject (Muffin self-snapshot, not real-mermaid)");

struct ErCase {
  const char* id;
  const char* source;
};

// Regression corpus (single source of truth). This is a REGRESSION GUARD, not a
// parity oracle: each `expected` block is Muffin's own ErScene::toJsonObject()
// output, captured via MUFFIN_GENERATE_ER_FIXTURE=1. It proves the ER scene is
// stable across builds. Real mermaid 11.16.0 parity is deferred until the
// sibling G:/github/mermaid-cli checkout (puppeteer + mermaid + dagre-d3-es +
// Chrome) is restored — see docs/mermaid-architecture.md step 5. When that
// checkout is restored, replace this with an oracle derived from mermaid-cli
// and rename the test to ...OracleTest.
const ErCase kCases[] = {
    {"two-entities-identifying", "erDiagram\nCUSTOMER ||--|| ORDER : places"},
    {"cardinality-types",
     "erDiagram\n"
     "A ||--o{ B : one_to_many\n"
     "A ||--o| C : one_to_zero_or_one\n"
     "A }|--|{ D : one_or_more\n"
     "A }o--o{ E : zero_or_more"},
    {"entity-attributes-keys",
     "erDiagram\n"
     "CUSTOMER {\n"
     "  string id PK \"primary key\"\n"
     "  string email UK\n"
     "  int account_id FK\n"
     "  string name \"display name\"\n"
     "}\n"
     "CUSTOMER ||--o{ ORDER : places"},
    {"multi-entity-roles",
     "erDiagram\n"
     "CUSTOMER ||--o{ ORDER : places\n"
     "ORDER ||--|{ LINE_ITEM : contains\n"
     "PRODUCT ||--o{ LINE_ITEM : \"ordered as\""},
    {"non-identifying", "erDiagram\nCUSTOMER }o..o{ ORDER : has"},
    {"single-entity",
     "erDiagram\n"
     "ACCOUNT {\n"
     "  string id PK\n"
     "  string name\n"
     "}"},
};

[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "%s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

QJsonObject renderEr(editor::MermaidRenderCache& cache, const QString& source,
                     const QString& id) {
  const auto entry = cache.getSync(cache.makeKey(source), source);
  require(entry.status == editor::MermaidRenderStatus::Ready && entry.erScene,
          id + QStringLiteral(": native ER scene failed: ") + entry.errorMessage);
  return entry.erScene->toJsonObject();
}

// MUFFIN_GENERATE_ER_FIXTURE=1 path: write the full manifest (indented JSON) to
// the fixture path passed by ctest (argv[1]), so regeneration is just
// `MUFFIN_GENERATE_ER_FIXTURE=1 ctest -R ErSceneRegression`.
int emitFixture(const QString& outPath) {
  editor::MermaidRenderCache cache;
  QJsonArray cases;
  for (const ErCase& c : kCases) {
    const QString id = QString::fromLatin1(c.id);
    const QString source = QString::fromUtf8(c.source);
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("source")] = source;
    o[QStringLiteral("expected")] = renderEr(cache, source, id);
    cases.append(o);
  }
  QJsonObject upstream;
  upstream[QStringLiteral("package")] = QStringLiteral("mermaid");
  upstream[QStringLiteral("version")] = QStringLiteral("11.16.0");
  upstream[QStringLiteral("notes")] = QStringLiteral(
      "REGRESSION GUARD ONLY. Generated from Muffin's own er::ErScene::toJsonObject(); "
      "NOT a real-mermaid reference. Real puppeteer/mermaid 11.16.0 parity deferred "
      "until the G:/github/mermaid-cli checkout is restored "
      "(docs/mermaid-architecture.md step 5).");
  QJsonObject root;
  root[QStringLiteral("upstream")] = upstream;
  root[QStringLiteral("mermaidVersion")] = QStringLiteral("11.16.0");
  root[QStringLiteral("fontMode")] = QStringLiteral("bundled-noto-2.13b171");
  root[QStringLiteral("oracle")] = kOracle;
  root[QStringLiteral("cases")] = cases;
  QFile out(outPath);
  require(out.open(QIODevice::WriteOnly | QIODevice::Truncate),
          QStringLiteral("Could not write fixture: ") + outPath + " (" + out.errorString() + ")");
  out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();

  require(argc == 2, QStringLiteral("Expected ER scene regression fixture path"));
  const QString fixturePath = QString::fromLocal8Bit(argv[1]);
  if (!qgetenv("MUFFIN_GENERATE_ER_FIXTURE").isEmpty()) return emitFixture(fixturePath);

  QFile file(fixturePath);
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString() == QLatin1String("11.16.0"),
          QStringLiteral("ER regression: mermaidVersion drifted"));
  require(root.value(QStringLiteral("fontMode")).toString() ==
              QLatin1String("bundled-noto-2.13b171"),
          QStringLiteral("ER regression: fontMode drifted"));
  require(root.value(QStringLiteral("oracle")).toString() == kOracle,
          QStringLiteral("ER regression: oracle disclaimer drifted"));

  // Index fixture cases by id so the in-tree kCases stays authoritative; a stale
  // fixture (edited case/source, or a new case not yet snapshotted) is caught.
  QHash<QString, QJsonObject> fixtureById;
  for (const QJsonValue& value : root.value(QStringLiteral("cases")).toArray())
    fixtureById.insert(value.toObject().value(QStringLiteral("id")).toString(), value.toObject());

  editor::MermaidRenderCache cache;
  int totalEntities = 0, totalRelationships = 0, totalAttributes = 0;
  for (const ErCase& c : kCases) {
    const QString id = QString::fromLatin1(c.id);
    const QString source = QString::fromUtf8(c.source);
    require(fixtureById.contains(id),
            id + QStringLiteral(": missing from fixture (regenerate with "
                                "MUFFIN_GENERATE_ER_FIXTURE=1)"));
    const QJsonObject fixture = fixtureById.value(id);
    require(fixture.value(QStringLiteral("source")).toString() == source,
            id + QStringLiteral(": fixture source drifted"));

    const QJsonObject actual = renderEr(cache, source, id);
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const QStringList errors = parity::compareJson(actual, expected, parity::kCoordinate, id,
                                                    {},
                                                    {QStringLiteral("path")});
    if (!errors.isEmpty()) {
      for (const QString& error : errors) std::fprintf(stderr, "%s\n", qPrintable(error));
      fail(id + QStringLiteral(": ER scene regression mismatch"));
    }
    totalEntities += actual.value(QStringLiteral("entities")).toArray().size();
    totalRelationships += actual.value(QStringLiteral("relationships")).toArray().size();
    for (const QJsonValue& entity : actual.value(QStringLiteral("entities")).toArray())
      totalAttributes += entity.toObject().value(QStringLiteral("attributes")).toArray().size();
  }

  require(totalEntities >= 8,
          QStringLiteral("ER coverage regressed: %1 entities").arg(totalEntities));
  require(totalRelationships >= 4,
          QStringLiteral("ER coverage regressed: %1 relationships").arg(totalRelationships));
  require(totalAttributes >= 5,
          QStringLiteral("ER coverage regressed: %1 attributes").arg(totalAttributes));
  return 0;
}
