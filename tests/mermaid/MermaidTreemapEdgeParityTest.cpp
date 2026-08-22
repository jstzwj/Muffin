#include "TreemapTestSupport.h"
#include "mermaid/MermaidFontRegistry.h"
#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <cstdio>
using namespace treemap_test;
int main(int argc, char **argv) {
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
  // The fixture goldens embed the Windows golden host's font stack; Linux
  // (Liberation fallbacks) and macOS (SF/Helvetica) resolve different faces
  // with different metrics. Bundled-font goldens are the eventual closure.
  qWarning("skipped on Linux/macOS: goldens embed the Windows golden-host font stack");
  return 0;
#endif
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  muffin::mermaid::MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Treemap config fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("9e03b0b0bd46947455c243baa715df24180c8944ddbe62f280bff89e082713e5"),
          QStringLiteral("Treemap config fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("04d19de0d4cff07d9e023e8a6c84fee1f2682bb70a35830716e6c7c22f42d5f3"),
          QStringLiteral("Treemap config provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 36, QStringLiteral("Expected 36 config cases"));
  for (const QJsonValue &value : cases) compareCase(value.toObject());
  std::puts("MermaidTreemapEdgeParityTest: 36/36 passed");
}
