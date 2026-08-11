#include "CynefinTestSupport.h"

#include "mermaid/cynefin/CynefinDiagram.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>

using namespace cynefin_test;

int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Cynefin config fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral(
                  "fd5f1b13c0f1dd491cdad7885d90c6e78f9d6df1acaed873abcb337a525eafd5"),
          QStringLiteral("Cynefin config fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
                  QLatin1String(
                      "ee118be127423cbac5b450131c65a2587195c23cf18c55ca6061d2423959777a") &&
              root.value(QStringLiteral("upstream")).toObject()
                      .value(QStringLiteral("version")).toString() ==
                  QLatin1String("11.16.0"),
          QStringLiteral("Cynefin config provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 56, QStringLiteral("Cynefin config case count"));
  for (const QJsonValue &value : cases) compareCase(value.toObject());

  const auto repeated = cynefin::CynefinDiagram::parse(QStringLiteral(
      "cynefin-beta\ncomplex\n\"first\"\nclear\n\"keep\"\ncomplex\n\"last\""));
  require(repeated.domains.size() == 2 &&
              repeated.domains.at(0).name == QLatin1String("complex") &&
              repeated.domains.at(0).items.size() == 1 &&
              repeated.domains.at(0).items.front().label == QLatin1String("last") &&
              repeated.domains.at(1).name == QLatin1String("clear"),
          QStringLiteral("Cynefin repeated-domain Map ordering drifted"));
  const auto loops = cynefin::CynefinDiagram::parse(QStringLiteral(
      "cynefin-beta\ncomplex --> complex : \"ignored\"\ncomplex --> clear"));
  require(loops.transitions.size() == 1 &&
              loops.transitions.front().from == QLatin1String("complex") &&
              loops.transitions.front().to == QLatin1String("clear"),
          QStringLiteral("Cynefin self-loop filtering drifted"));

  std::puts("MermaidCynefinEdgeParityTest: 56 config cases plus DB edges passed");
  return 0;
}
