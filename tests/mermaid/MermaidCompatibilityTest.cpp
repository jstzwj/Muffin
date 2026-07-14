#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
    } catch (const UnknownDiagramError&) {
      require(expected.isNull(), QStringLiteral("Mermaid detector %1 unexpectedly rejected a known diagram").arg(id));
    }
  }
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
  return 0;
}
