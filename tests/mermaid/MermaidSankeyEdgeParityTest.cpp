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
  require(argc == 2, QStringLiteral("Expected Sankey config fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("d694756bb02e837539febbac678cb108101b669ea289a3"
                                "08038b115c5fe1c63d"),
          QStringLiteral("Sankey config fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("acb58058545464d48455f430e7bb46dab4c462c1dab7ff0fce"
                            "d6d7540fa0935f"),
          QStringLiteral("Sankey config provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 38, QStringLiteral("Expected 38 config cases"));
  for (const QJsonValue &value : cases)
    compareCase(value.toObject());
  std::puts("MermaidSankeyEdgeParityTest: 38/38 passed");
  return 0;
}
