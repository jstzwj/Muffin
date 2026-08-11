#include "ArchitectureTestSupport.h"

#include "mermaid/MermaidFontRegistry.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>

using namespace architecture_test;

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Architecture config fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("2558c4d4c6c1cb644d74f26a6ca471c8f6dd9a56111a94580a01c9a7b71c751e"),
          QStringLiteral("Architecture config fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
                  QLatin1String("88462c6eb1186769901fbff4a9b115cacc82a2ba5dfdc7f63ad0ac0ad77865fe") &&
              root.value(QStringLiteral("upstream")).toObject()
                      .value(QStringLiteral("version")).toString() ==
                  QLatin1String("11.16.0"),
          QStringLiteral("Architecture config provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 41, QStringLiteral("Architecture config case count"));
  for (const QJsonValue& value : cases) compareConfigCase(value.toObject());
  std::puts("MermaidArchitectureEdgeParityTest: 41/41 passed");
  return 0;
}
