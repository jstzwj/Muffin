#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/editor/MermaidDiagrams.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <cstdlib>

using namespace muffin::mermaid;

namespace {

[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

QString compactJson(const QJsonValue& value) {
  if (value.isObject()) return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
  if (value.isArray()) return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
  return QStringLiteral("<non-container>");
}

void testDetection(const QJsonArray& cases) {
  require(cases.size() >= 45, QStringLiteral("Mermaid detection golden must cover every registered built-in family"));
  QSet<QString> registeredIds;
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QJsonObject config = fixture.value(QStringLiteral("config")).toObject();
    const QJsonValue expected = fixture.value(QStringLiteral("diagramType"));
    try {
      const QString actual = detectDiagramType(source, config);
      require(!expected.isNull(), QStringLiteral("Mermaid detector %1 should reject unknown input, got %2").arg(id, actual));
      require(actual == expected.toString(),
              QStringLiteral("Mermaid detector %1 mismatch: native=%2 upstream=%3")
                  .arg(id, actual, expected.toString()));
      if (actual != QLatin1String("error") && actual != QLatin1String("---"))
        registeredIds.insert(actual);
    } catch (const UnknownDiagramError&) {
      require(expected.isNull(), QStringLiteral("Mermaid detector %1 unexpectedly rejected a known diagram").arg(id));
    }
  }
  require(registeredIds.size() == 38,
          QStringLiteral("Expected all 38 Mermaid 11.16 detector IDs, found %1")
              .arg(registeredIds.size()));
  for (const QString& id : registeredIds)
    require(editor::findMermaidDiagram(id) != nullptr,
            QStringLiteral("Registered Mermaid ID has no native adapter: %1")
                .arg(id));
}

void testPreprocess(const QJsonArray& cases) {
  require(cases.size() >= 10, QStringLiteral("Mermaid preprocessing golden is unexpectedly small"));
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const MermaidPreprocessResult actual = preprocessDiagram(fixture.value(QStringLiteral("source")).toString());
    require(actual.code == expected.value(QStringLiteral("code")).toString(),
            QStringLiteral("Mermaid preprocess %1 code mismatch:\nnative:   <%2>\nupstream: <%3>")
                .arg(id, actual.code, expected.value(QStringLiteral("code")).toString()));
    const bool expectedTitle = expected.contains(QStringLiteral("title"));
    require(actual.hasTitle == expectedTitle,
            QStringLiteral("Mermaid preprocess %1 title presence mismatch").arg(id));
    if (expectedTitle) {
      require(actual.title == expected.value(QStringLiteral("title")).toString(),
              QStringLiteral("Mermaid preprocess %1 title mismatch").arg(id));
    }
    const QJsonObject expectedConfig = expected.value(QStringLiteral("config")).toObject();
    require(actual.config == expectedConfig,
            QStringLiteral("Mermaid preprocess %1 config mismatch:\nnative:   %2\nupstream: %3")
                .arg(id, compactJson(actual.config), compactJson(expectedConfig)));
  }
}

void requireJourneySourceConfig(const MermaidPreprocessResult& result, const QString& label) {
  const QJsonObject themeVariables =
      result.config.value(QStringLiteral("themeVariables")).toObject();
  require(themeVariables.value(QStringLiteral("fillType0")).toString() ==
              QLatin1String("#123456"),
          label + QStringLiteral(": fillType0 was not retained"));
  require(themeVariables.value(QStringLiteral("textColor")).toString() ==
              QLatin1String("#abcdef"),
          label + QStringLiteral(": textColor was not retained"));
  require(!themeVariables.contains(QStringLiteral("actor0")),
          label + QStringLiteral(": actor0 must be filtered from source config"));

  const QJsonObject journey = result.config.value(QStringLiteral("journey")).toObject();
  require(!journey.contains(QStringLiteral("actorColours")),
          label + QStringLiteral(": actorColours must be filtered from source config"));
  require(!journey.contains(QStringLiteral("sectionFills")),
          label + QStringLiteral(": sectionFills must be filtered from source config"));
  require(!journey.contains(QStringLiteral("sectionColours")),
          label + QStringLiteral(": sectionColours must be filtered from source config"));
}

void testJourneySourceConfigSanitization() {
  // Mermaid's detectInit has an intentional asymmetry: it invokes the full
  // allowed-key sanitizer here only for multiple init directives. A single
  // directive/frontmatter is sanitized later by addDirective, outside this
  // preprocessing port's result boundary.
  const QString directive = QStringLiteral(
      "%%{init: {\"themeVariables\": {\"fillType0\": \"#123456\", "
      "\"textColor\": \"#abcdef\", \"actor0\": \"#fedcba\"}, \"journey\": {"
      "\"actorColours\": [\"#111111\"], \"sectionFills\": [\"#222222\"], "
      "\"sectionColours\": [\"#333333\"]}}}%%\n"
      "%%{init: {\"theme\": \"default\"}}%%\njourney\nsection S\nT: 1");
  requireJourneySourceConfig(preprocessDiagram(directive),
                             QStringLiteral("multiple-init sanitizer"));
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected path to Mermaid compatibility fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Could not open Mermaid fixture: %1").arg(file.fileName()));
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
  require(error.error == QJsonParseError::NoError && document.isObject(),
          QStringLiteral("Invalid Mermaid fixture JSON: %1").arg(error.errorString()));
  const QJsonObject root = document.object();
  require(root.value(QStringLiteral("upstream")).toObject().value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("Mermaid compatibility fixture version drifted"));
  testDetection(root.value(QStringLiteral("detection")).toArray());
  testPreprocess(root.value(QStringLiteral("preprocess")).toArray());
  testJourneySourceConfigSanitization();
  return 0;
}
