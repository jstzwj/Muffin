// Level-1 error-category golden (milestone G1). Asserts the native parser throws
// FlowchartParseError with the curated category (+ line where determinable) for
// each malformed input in flowchart-errors.json. The contract compared against
// upstream is the CATEGORY + position, not the human message (messages diverge
// between a JS grammar and a C++ hand-written parser; docs/mermaid-flowchart-
// remaining-plan.md §10). The fixture also records whether upstream mermaid
// rejects the input; for resource-limit cases upstream parses fine (it has no
// such limits) — Muffin's protection is intentionally stricter (milestone H).

#include "mermaid/flowchart/Flowchart.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdlib>

using namespace muffin::mermaid::flowchart;

namespace {
[[noreturn]] void fail(const QString& message) { qCritical().noquote() << message; std::exit(1); }
void require(bool condition, const QString& message) { if (!condition) fail(message); }

FlowchartErrorCategory parseCategory(const QString& name) {
  static const QHash<QString, FlowchartErrorCategory> map = {
      {QStringLiteral("Syntax"), FlowchartErrorCategory::Syntax},
      {QStringLiteral("MissingHeader"), FlowchartErrorCategory::MissingHeader},
      {QStringLiteral("UnclosedSubgraph"), FlowchartErrorCategory::UnclosedSubgraph},
      {QStringLiteral("UnexpectedEnd"), FlowchartErrorCategory::UnexpectedEnd},
      {QStringLiteral("InvalidNode"), FlowchartErrorCategory::InvalidNode},
      {QStringLiteral("InvalidDirective"), FlowchartErrorCategory::InvalidDirective},
      {QStringLiteral("LinkStyleBounds"), FlowchartErrorCategory::LinkStyleBounds},
      {QStringLiteral("LimitExceeded"), FlowchartErrorCategory::LimitExceeded},
      {QStringLiteral("SecurityViolation"), FlowchartErrorCategory::SecurityViolation},
  };
  const auto it = map.constFind(name);
  require(it != map.constEnd(), QStringLiteral("Unknown error category in fixture: %1").arg(name));
  return it.value();
}
}  // namespace

int main(int argc, char** argv) {
  require(argc == 2, QStringLiteral("Expected error fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Could not open error fixture"));
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("upstream")).toObject().value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("Error fixture version drifted"));

  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const FlowchartErrorCategory expectedCategory = parseCategory(fixture.value(QStringLiteral("expectedCategory")).toString());
    const QString expectedStage = fixture.value(QStringLiteral("expectedStage")).toString();
    const QString expectedCode = fixture.value(QStringLiteral("expectedCode")).toString();
    const int expectedLine = fixture.value(QStringLiteral("expectedLine")).toInt();
    const int expectedColumn = fixture.value(QStringLiteral("expectedColumn")).toInt();

    FlowchartParseOptions options;
    if (fixture.contains(QStringLiteral("options"))) {
      const QJsonObject overrides = fixture.value(QStringLiteral("options")).toObject();
      if (overrides.contains(QStringLiteral("maxTextSize"))) options.maxTextSize = overrides.value(QStringLiteral("maxTextSize")).toInt();
      if (overrides.contains(QStringLiteral("maxEdges"))) options.maxEdges = overrides.value(QStringLiteral("maxEdges")).toInt();
    }
    FlowchartLimits limits;
    if (fixture.contains(QStringLiteral("limits"))) {
      const QJsonObject overrides = fixture.value(QStringLiteral("limits")).toObject();
      if (overrides.contains(QStringLiteral("maxVertices"))) limits.maxVertices = overrides.value(QStringLiteral("maxVertices")).toInt();
      if (overrides.contains(QStringLiteral("maxClasses"))) limits.maxClasses = overrides.value(QStringLiteral("maxClasses")).toInt();
      if (overrides.contains(QStringLiteral("maxSubgraphs"))) limits.maxSubgraphs = overrides.value(QStringLiteral("maxSubgraphs")).toInt();
      if (overrides.contains(QStringLiteral("maxSubgraphDepth"))) limits.maxSubgraphDepth = overrides.value(QStringLiteral("maxSubgraphDepth")).toInt();
      if (overrides.contains(QStringLiteral("maxTooltips"))) limits.maxTooltips = overrides.value(QStringLiteral("maxTooltips")).toInt();
      if (overrides.contains(QStringLiteral("maxLineLength"))) limits.maxLineLength = overrides.value(QStringLiteral("maxLineLength")).toInt();
      if (overrides.contains(QStringLiteral("maxNodeIdLength"))) limits.maxNodeIdLength = overrides.value(QStringLiteral("maxNodeIdLength")).toInt();
      if (overrides.contains(QStringLiteral("maxStylesPerVertex"))) limits.maxStylesPerVertex = overrides.value(QStringLiteral("maxStylesPerVertex")).toInt();
    }

    bool threw = false;
    try {
      Flowchart::parse(source, options, limits);
    } catch (const FlowchartParseError& error) {
      threw = true;
      require(error.category() == expectedCategory,
              QStringLiteral("Case %1 category mismatch: got %2 expected %3 (line %4)")
                  .arg(id).arg(static_cast<int>(error.category())).arg(static_cast<int>(expectedCategory)).arg(error.line()));
      require(flowchartErrorStageName(error.stage()) == expectedStage,
              QStringLiteral("Case %1 stage mismatch: got %2 expected %3")
                  .arg(id, flowchartErrorStageName(error.stage()), expectedStage));
      require(flowchartErrorCodeName(error.code()) == expectedCode,
              QStringLiteral("Case %1 code mismatch: got %2 expected %3")
                  .arg(id, flowchartErrorCodeName(error.code()), expectedCode));
      // expectedLine == 0 means "not determinable" (e.g. thrown before/after the line loop);
      // only assert the position where the contract pins it.
      if (expectedLine > 0)
        require(error.line() == expectedLine,
                QStringLiteral("Case %1 line mismatch: got %2 expected %3").arg(id).arg(error.line()).arg(expectedLine));
      if (expectedColumn > 0)
        require(error.column() == expectedColumn,
                QStringLiteral("Case %1 column mismatch: got %2 expected %3")
                    .arg(id).arg(error.column()).arg(expectedColumn));
    }
    require(threw, QStringLiteral("Case %1 should have thrown %2").arg(id).arg(fixture.value(QStringLiteral("expectedCategory")).toString()));
  }

  qDebug().noquote() << "MermaidParserErrorTest:" << cases.size()
                     << "error cases match category, stage, code, and stable position";
  return 0;
}
