#include "ArchitectureTestSupport.h"

#include "mermaid/MermaidFontRegistry.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>

using namespace architecture_test;

int main(int argc, char** argv) {
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
  // The fixture goldens embed the Windows golden host's font stack; Linux
  // (Liberation fallbacks) and macOS (SF/Helvetica) resolve different faces
  // with different metrics. Bundled-font goldens are the eventual closure.
  qWarning("skipped on Linux/macOS: goldens embed the Windows golden-host font stack");
  return 0;
#endif
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Architecture geometry fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("e7afc07b4bb9b4c05199b33d8d6de7930ec9b14d92cd094104ce0c73c98984e8"),
          QStringLiteral("Architecture geometry fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  const QJsonObject upstream = root.value(QStringLiteral("upstream")).toObject();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
                  QLatin1String("986f8453a377daa4a8e8eb8c47cf61b02c30ced198969109b421132347534383") &&
              upstream.value(QStringLiteral("version")).toString() ==
                  QLatin1String("11.16.0") &&
              upstream.value(QStringLiteral("architectureModuleSha256")).toString() ==
                  QLatin1String("d6f8424fba961c50f2cfcbd4e1c5f53f37311d83cc768bcf41afd8874c0454ba") &&
              upstream.value(QStringLiteral("cytoscapeVersion")).toString() ==
                  QLatin1String("3.34.0") &&
              upstream.value(QStringLiteral("fcoseVersion")).toString() ==
                  QLatin1String("2.2.0"),
          QStringLiteral("Architecture geometry provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 18, QStringLiteral("Architecture geometry case count"));
  for (const QJsonValue& value : cases) compareGeometryCase(value.toObject());
  std::puts("MermaidArchitectureGeometryOracleTest: 18/18 passed");
  return 0;
}
