#include "CynefinTestSupport.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>

using namespace cynefin_test;

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
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Cynefin geometry fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral(
                  "be39fee7029e55cad29dd6b2cffcd2f1cc8ad6e48be2f7b68926f6237169ec79"),
          QStringLiteral("Cynefin geometry fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
                  QLatin1String(
                      "959f179febb8fc138d494b8882f9e9ee0d3162623a7eb01b08420b0fcb804672") &&
              root.value(QStringLiteral("upstream")).toObject()
                      .value(QStringLiteral("version")).toString() ==
                  QLatin1String("11.16.0"),
          QStringLiteral("Cynefin geometry provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 16, QStringLiteral("Cynefin geometry case count"));
  for (const QJsonValue &value : cases) compareCase(value.toObject());
  std::puts("MermaidCynefinGeometryOracleTest: 16/16 passed");
  return 0;
}
