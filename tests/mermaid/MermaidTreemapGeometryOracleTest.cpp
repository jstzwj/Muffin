#include "TreemapTestSupport.h"
#include "mermaid/MermaidFontRegistry.h"
#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <cstdio>
using namespace treemap_test;
int main(int argc, char **argv) {
#if defined(Q_OS_MACOS)
  // The fixture goldens embed the Windows golden host's font stack; macOS
  // (SF/Helvetica) resolves different faces with different metrics.
  // Bundled-font goldens are the eventual closure.
  qWarning("skipped on macOS: goldens embed the Windows golden-host font stack");
  return 0;
#endif
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  muffin::mermaid::MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Treemap geometry fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("aa27ed9f242e5c13a99e7265dbff132be44fc4f46e827e9d0ea8c7ddc6f30b14"),
          QStringLiteral("Treemap geometry fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("0cf92a922c2c1461e137aa395aabef600183ee4c6378c129ede76ceb4a5ff6ea"),
          QStringLiteral("Treemap geometry provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 16, QStringLiteral("Expected 16 geometry cases"));
  for (const QJsonValue &value : cases) compareCase(value.toObject());
  std::puts("MermaidTreemapGeometryOracleTest: 16/16 passed");
}
