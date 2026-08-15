#include "WardleyTestSupport.h"

#include "mermaid/MermaidFontRegistry.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>

using namespace wardley_test;

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  ensureWardleyFonts();
  require(argc == 2, QStringLiteral("Expected Wardley config fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral(
                  "8b81b4a00e342398e0a80f63eedbaf6c22b6840bfb55e822a652198ef6761828"),
          QStringLiteral("Wardley config fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
                  QLatin1String(
                      "7d8c9a8b7ad401661e7109a3b3b6ceb2ce36e5cfe962d98b2603b60e39bba163") &&
              root.value(QStringLiteral("upstream")).toObject()
                      .value(QStringLiteral("version")).toString() ==
                  QLatin1String("11.16.0"),
          QStringLiteral("Wardley config provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 45, QStringLiteral("Wardley config case count"));
  for (const QJsonValue& value : cases) compareCase(value.toObject());
  std::puts("MermaidWardleyEdgeParityTest: 45 config cases passed");
  return 0;
}
