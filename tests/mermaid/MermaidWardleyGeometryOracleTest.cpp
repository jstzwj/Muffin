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
  muffin::mermaid::MermaidFontRegistry::ensureLoaded();
  ensureWardleyFonts();
  require(argc == 2, QStringLiteral("Expected Wardley geometry fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("ae1454223d21b37f94e414612bd3d62e6903e5a5fc70af4e9d1c7ee9952eed79"),
          QStringLiteral("Wardley geometry fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
                  QLatin1String("5387acc0af6177983478a4e4d6de571305cb9b55a94e6a33672ba7d0fa2053bf") &&
              root.value(QStringLiteral("upstream")).toObject()
                      .value(QStringLiteral("version")).toString() ==
                  QLatin1String("11.16.0"),
          QStringLiteral("Wardley geometry provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 16, QStringLiteral("Wardley geometry case count"));
  for (const QJsonValue& value : cases) compareCase(value.toObject());
  std::puts("MermaidWardleyGeometryOracleTest: 16/16 passed");
  return 0;
}
