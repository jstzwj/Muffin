#include "SankeyTestSupport.h"

#include "mermaid/MermaidFontRegistry.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>

#include <cstdio>

using namespace muffin::mermaid;
using namespace sankey_test;

int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Sankey geometry fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("44e82a8131e3b6b6574259ae6eaa2bd9a05cff6fbe6f06"
                                "5536f34b0ca1f9c1b7"),
          QStringLiteral("Sankey geometry fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("11138e58e9e9726734f9d6182361ed87a44f28c4f0cba146ff"
                            "a0cf1ea8099b8b"),
          QStringLiteral("Sankey geometry provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 16, QStringLiteral("Expected 16 geometry cases"));
  for (const QJsonValue &value : cases)
    compareCase(value.toObject());
  std::puts("MermaidSankeyGeometryOracleTest: 16/16 passed");
  return 0;
}
