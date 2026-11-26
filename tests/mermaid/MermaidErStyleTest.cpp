// Verifies that a classDef and an inline `style` reach a scene entity as its
// resolved paint. The paint of every theme and of styled tables is asserted
// against Mermaid itself by MermaidErGeometryOracleTest; this pins the path
// the editor takes.

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/erdiagram/ErScene.h"

#include <QGuiApplication>

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
  if (!condition)
    fail(message);
}
const er::ErSceneEntity* entityById(const editor::MermaidRenderEntry& e, const QString& id) {
  const auto* erScene = dynamic_cast<const er::ErScene*>(e.scene.get());
  require(erScene != nullptr, QStringLiteral("missing ER scene"));
  for (const auto& entity : erScene->entities)
    if (entity.id == id)
      return &entity;
  return nullptr;
}
} // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();

  editor::MermaidRenderCache cache;

  // classDef + class: CUSTOMER gets the "red" class.
  const QString classSource = QStringLiteral("erDiagram\n"
                                             "CUSTOMER ||--o{ ORDER : places\n"
                                             "classDef red fill:#ff0000,stroke:#00ff00\n"
                                             "class CUSTOMER red");
  const auto classEntry = cache.getSync(cache.makeKey(classSource), classSource);
  require(classEntry.status == editor::MermaidRenderStatus::Ready,
          QStringLiteral("classDef ER diagram did not render: ") + classEntry.errorMessage);
  // Entities are known by the id erDB numbers them with, by declaration.
  const er::ErSceneEntity* customer = entityById(classEntry, QStringLiteral("entity-CUSTOMER-0"));
  require(customer, QStringLiteral("CUSTOMER entity not found"));
  require(customer->table.fill == QLatin1String("#ff0000"),
          QStringLiteral("classDef fill did not resolve; got ") + customer->table.fill);
  require(customer->table.stroke == QLatin1String("#00ff00"),
          QStringLiteral("classDef stroke did not resolve; got ") + customer->table.stroke);

  // Inline style overrides.
  const QString styleSource = QStringLiteral("erDiagram\n"
                                             "CUSTOMER ||--o{ ORDER : places\n"
                                             "style CUSTOMER fill:#0000ff");
  const auto styleEntry = cache.getSync(cache.makeKey(styleSource), styleSource);
  require(styleEntry.status == editor::MermaidRenderStatus::Ready,
          QStringLiteral("inline-style ER diagram did not render"));
  const er::ErSceneEntity* styled = entityById(styleEntry, QStringLiteral("entity-CUSTOMER-0"));
  require(styled, QStringLiteral("CUSTOMER entity not found (style case)"));
  require(styled->table.fill == QLatin1String("#0000ff"),
          QStringLiteral("inline style fill did not resolve; got ") + styled->table.fill);

  // An entity with attributes: the classDef reaches its table, and the stroke
  // it declares its rows and its dividers too, while the rows keep the fills
  // of the theme, which the diagram without the classDef shows.
  const QString tableLines = QStringLiteral("    CUSTOMER {\n"
                                            "        string name\n"
                                            "        int age\n"
                                            "    }\n"
                                            "    CUSTOMER ||--o{ ORDER : places\n");
  const QString styledTable = QStringLiteral("erDiagram\n") + tableLines +
                              QStringLiteral("    classDef green stroke:#00aa00\n"
                                             "    class CUSTOMER green\n");
  const auto tableEntry = cache.getSync(cache.makeKey(styledTable), styledTable);
  require(tableEntry.status == editor::MermaidRenderStatus::Ready,
          QStringLiteral("classDef table ER diagram did not render: ") + tableEntry.errorMessage);
  const auto plainEntry = cache.getSync(cache.makeKey(QStringLiteral("erDiagram\n") + tableLines),
                                        QStringLiteral("erDiagram\n") + tableLines);
  require(plainEntry.status == editor::MermaidRenderStatus::Ready,
          QStringLiteral("plain table ER diagram did not render: ") + plainEntry.errorMessage);
  const er::ErSceneEntity* table = entityById(tableEntry, QStringLiteral("entity-CUSTOMER-0"));
  const er::ErSceneEntity* plain = entityById(plainEntry, QStringLiteral("entity-CUSTOMER-0"));
  require(table && table->rows.size() == 2, QStringLiteral("CUSTOMER with two attributes not found"));
  require(plain && plain->rows.size() == table->rows.size(), QStringLiteral("CUSTOMER without a class not found"));
  require(table->table.fill == QLatin1String("#ECECFF") && table->table.stroke == QLatin1String("#00aa00"),
          QStringLiteral("classDef stroke did not reach the table; got fill ") + table->table.fill +
              QStringLiteral(" stroke ") + table->table.stroke);
  for (qsizetype row = 0; row < table->rows.size(); ++row) {
    const er::ErPaint& styledPaint = table->rows.at(row).paint;
    const er::ErPaint& plainPaint = plain->rows.at(row).paint;
    require(styledPaint.fill == plainPaint.fill && !styledPaint.fill.isEmpty(),
            QStringLiteral("classDef changed a row's fill: ") + styledPaint.fill + QStringLiteral(" over ") +
                plainPaint.fill);
    require(styledPaint.stroke == QLatin1String("#00aa00") && plainPaint.stroke != styledPaint.stroke,
            QStringLiteral("classDef stroke did not reach row %1; got ").arg(row) + styledPaint.stroke);
  }
  require(table->dividerPaint.stroke == QLatin1String("#00aa00") &&
              plain->dividerPaint.stroke != table->dividerPaint.stroke,
          QStringLiteral("classDef stroke did not reach the dividers; got ") + table->dividerPaint.stroke);

  return 0;
}
