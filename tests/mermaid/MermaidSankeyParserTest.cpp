#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/sankey/SankeyDiagram.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;
namespace {
[[noreturn]] void fail(const QString &s) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(s));
  std::exit(1);
}
void req(bool v, const QString &s) {
  if (!v)
    fail(s);
}
QJsonObject graphJson(const sankey::SankeyData &data) {
  QJsonArray nodes, links;
  for (const auto &n : data.nodes)
    nodes.append(QJsonObject{{"id", n.id}});
  for (const auto &l : data.links)
    links.append(
        QJsonObject{{"source", l.source},
                    {"target", l.target},
                    {"value", std::isfinite(l.value) ? QJsonValue(l.value)
                                                     : QJsonValue()}});
  return {{"nodes", nodes}, {"links", links}};
}
} // namespace
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  req(argc == 2, "fixture");
  QFile f(QString::fromLocal8Bit(argv[1]));
  req(f.open(QIODevice::ReadOnly), f.errorString());
  const QByteArray bytes = f.readAll();
  req(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
          QByteArrayLiteral("1f03cb563a83009e00d0e39aef87709d244d0399eea73513a0"
                            "4613b12e6b966f"),
      "fixture bytes");
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  req(root.value("fixtureSha256").toString() ==
          QLatin1String("ed36a90ab8ef42082ebcf2c0f8ff4655c645f60dbb9a5ac0e5d143"
                        "5ba6e0b02e"),
      "fixture provenance");
  int visited = 0;
  for (const QJsonValue &v : root.value("cases").toArray()) {
    const QJsonObject c = v.toObject(), e = c.value("expected").toObject();
    const QString id = c.value("id").toString();
    const auto pre = preprocessDiagram(c.value("source").toString());
    bool detected = false;
    try {
      detected =
          detectDiagramType(pre.code, pre.config) == QLatin1String("sankey");
    } catch (const UnknownDiagramError &) {
    }
    if (!e.value("parse").toBool()) {
      if (!detected) {
        ++visited;
        continue;
      }
      bool threw = false;
      try {
        (void)sankey::SankeyDiagram::parse(pre.code);
      } catch (const sankey::SankeyParseError &) {
        threw = true;
      }
      req(threw, id + " accepted");
      ++visited;
      continue;
    }
    req(detected, id + " not detected");
    sankey::SankeyData data;
    try {
      data = sankey::SankeyDiagram::parse(pre.code);
    } catch (const std::exception &x) {
      fail(id + ": " + QString::fromUtf8(x.what()));
    }
    req(graphJson(data) == e.value("graph").toObject(),
        id + " graph actual=" +
            QString::fromUtf8(
                QJsonDocument(graphJson(data)).toJson(QJsonDocument::Compact)));
    ++visited;
  }
  req(visited == 37, "count");
  std::puts("MermaidSankeyParserTest: 37/37 passed");
}
