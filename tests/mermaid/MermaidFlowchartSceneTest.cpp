#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/flowchart/FlowchartLayout.h"
#include "mermaid/scene/FlowScene.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidColor.h"

#include <QDebug>
#include <QColor>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) { qCritical().noquote() << message; std::exit(1); }
void require(bool condition, const QString& message) { if (!condition) fail(message); }

QColor toColor(const QString& s) {
  QColor c(s.trimmed());
  if (c.isValid()) return c;
  static const QRegularExpression rgbaRe(
      QStringLiteral("rgba?\\(\\s*([\\d.]+)\\s*,\\s*([\\d.]+)\\s*,\\s*([\\d.]+)(?:\\s*,\\s*([\\d.]+))?\\)"),
      QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch m = rgbaRe.match(s.trimmed());
  if (m.hasMatch()) {
    QColor c2; c2.setRgb(qRound(m.captured(1).toDouble()), qRound(m.captured(2).toDouble()),
                         qRound(m.captured(3).toDouble()),
                         m.captured(4).isEmpty() ? 255 : qRound(m.captured(4).toDouble() * 255));
    return c2;
  }
  return QColor();
}
bool colorsEqual(const QString& native, const QString& golden) {
  if (native.isEmpty() && golden.isEmpty()) return true;
  const QColor a = color::toQColor(native), b = toColor(golden);
  if (!a.isValid() || !b.isValid()) return native == golden;
  return a.rgb() == b.rgb();
}
double px(const QString& s) {
  static const QRegularExpression re(QStringLiteral("(\\d+(?:\\.\\d+)?)"));
  const QRegularExpressionMatch m = re.match(s);
  return m.hasMatch() ? m.captured(1).toDouble() : -1.0;
}
// Extract the base marker type from mermaid's marker url, stripping the
// per-stroke-colour clone suffix (`pointEnd__0000ff` -> `pointEnd`).
QString baseMarkerType(const QString& url) {
  if (url.isEmpty()) return QString();
  const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("#([^)]+)")).match(url);
  if (!m.hasMatch()) return QString();
  QString id = m.captured(1);
  const int cut = id.indexOf(QStringLiteral("__"));
  if (cut >= 0) id = id.left(cut);
  const int v2 = id.indexOf(QStringLiteral("flowchart-v2-"));
  return v2 >= 0 ? id.mid(v2 + 13) : id;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected scene fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Could not open scene fixture"));
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("upstream")).toObject().value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("Scene fixture version drifted"));

  const flowchart::Flowchart chart = flowchart::Flowchart::parse(root.value(QStringLiteral("source")).toString());
  const QMap<QString, QSizeF> sizes = flowchart::measureFlowchartNodes(chart.data());
  flowchart::FlowLayoutOptions options;
  for (const flowchart::FlowEdge& e : chart.data().edges) {
    if (!e.text.isEmpty()) options.measuredEdgeLabels.insert(e.id, flowchart::measureLabel(e.text));
  }
  const flowchart::FlowLayoutResult layout = flowchart::layoutFlowchartNodes(chart.data(), sizes, options);
  const flowtheme::FlowThemeVariables theme = flowtheme::resolveFlowTheme(flowtheme::FlowThemeId::Default);
  const flowscene::FlowScene scene = flowscene::buildFlowScene(chart.data(), layout, theme);

  // Draw order: clusters, edges, nodes (mermaid SVG order). Verify counts + ids.
  const QJsonArray gClusters = root.value(QStringLiteral("clusters")).toArray();
  const QJsonArray gEdges = root.value(QStringLiteral("edges")).toArray();
  const QJsonArray gNodes = root.value(QStringLiteral("nodes")).toArray();
  require(scene.clusters.size() == gClusters.size(), QStringLiteral("cluster count mismatch"));
  require(scene.edges.size() == gEdges.size(), QStringLiteral("edge count mismatch"));
  require(scene.nodes.size() == gNodes.size(), QStringLiteral("node count mismatch"));

  for (qsizetype i = 0; i < scene.nodes.size(); ++i) {
    const QJsonObject g = gNodes.at(i).toObject();
    const flowscene::FlowSceneNode& n = scene.nodes.at(i);
    require(n.shapeKind == g.value(QStringLiteral("shapeKind")).toString(),
            QStringLiteral("Node %1 shapeKind mismatch: native=%2 golden=%3").arg(n.id, n.shapeKind, g.value(QStringLiteral("shapeKind")).toString()));
    require(colorsEqual(n.fill, g.value(QStringLiteral("fill")).toString()),
            QStringLiteral("Node %1 fill mismatch: native=%2 golden=%3").arg(n.id, n.fill, g.value(QStringLiteral("fill")).toString()));
    require(colorsEqual(n.stroke, g.value(QStringLiteral("stroke")).toString()),
            QStringLiteral("Node %1 stroke mismatch: native=%2 golden=%3").arg(n.id, n.stroke, g.value(QStringLiteral("stroke")).toString()));
    require(std::abs(px(n.strokeWidth) - px(g.value(QStringLiteral("strokeWidth")).toString())) <= 0.002,
            QStringLiteral("Node %1 strokeWidth mismatch: native=%2 golden=%3").arg(n.id, n.strokeWidth, g.value(QStringLiteral("strokeWidth")).toString()));
    require(n.label.text == g.value(QStringLiteral("label")).toObject().value(QStringLiteral("text")).toString(),
            QStringLiteral("Node %1 label text mismatch: native=%2 golden=%3").arg(n.id, n.label.text, g.value(QStringLiteral("label")).toObject().value(QStringLiteral("text")).toString()));
    // geometry (loose — layout fidelity is verified by the 22-case geometry golden)
    require(std::abs(n.width - g.value(QStringLiteral("width")).toDouble()) <= 0.2 &&
                std::abs(n.height - g.value(QStringLiteral("height")).toDouble()) <= 0.2,
            QStringLiteral("Node %1 size mismatch: native=%2x%3 golden=%4x%5")
                .arg(n.id).arg(n.width).arg(n.height).arg(g.value(QStringLiteral("width")).toDouble()).arg(g.value(QStringLiteral("height")).toDouble()));
  }

  for (qsizetype i = 0; i < scene.edges.size(); ++i) {
    const QJsonObject g = gEdges.at(i).toObject();
    const flowscene::FlowSceneEdge& e = scene.edges.at(i);
    require(e.markerEnd == baseMarkerType(g.value(QStringLiteral("markerEnd")).toString()),
            QStringLiteral("Edge %1 markerEnd mismatch: native=%2 golden=%3").arg(e.id, e.markerEnd, baseMarkerType(g.value(QStringLiteral("markerEnd")).toString())));
    require(e.markerStart == baseMarkerType(g.value(QStringLiteral("markerStart")).toString()),
            QStringLiteral("Edge %1 markerStart mismatch: native=%2 golden=%3").arg(e.id, e.markerStart, baseMarkerType(g.value(QStringLiteral("markerStart")).toString())));
    require(colorsEqual(e.stroke, g.value(QStringLiteral("stroke")).toString()),
            QStringLiteral("Edge %1 stroke mismatch: native=%2 golden=%3").arg(e.id, e.stroke, g.value(QStringLiteral("stroke")).toString()));
    require(std::abs(px(e.strokeWidth) - px(g.value(QStringLiteral("strokeWidth")).toString())) <= 0.002,
            QStringLiteral("Edge %1 strokeWidth mismatch: native=%2 golden=%3").arg(e.id, e.strokeWidth, g.value(QStringLiteral("strokeWidth")).toString()));
    require(e.label.text == g.value(QStringLiteral("label")).toObject().value(QStringLiteral("text")).toString(),
            QStringLiteral("Edge %1 label text mismatch: native=%2 golden=%3").arg(e.id, e.label.text, g.value(QStringLiteral("label")).toObject().value(QStringLiteral("text")).toString()));
  }

  for (qsizetype i = 0; i < scene.clusters.size(); ++i) {
    const QJsonObject g = gClusters.at(i).toObject();
    const flowscene::FlowSceneCluster& c = scene.clusters.at(i);
    require(colorsEqual(c.fill, g.value(QStringLiteral("fill")).toString()),
            QStringLiteral("Cluster %1 fill mismatch: native=%2 golden=%3").arg(c.id, c.fill, g.value(QStringLiteral("fill")).toString()));
    require(colorsEqual(c.stroke, g.value(QStringLiteral("stroke")).toString()),
            QStringLiteral("Cluster %1 stroke mismatch: native=%2 golden=%3").arg(c.id, c.stroke, g.value(QStringLiteral("stroke")).toString()));
    require(c.label.text == g.value(QStringLiteral("label")).toObject().value(QStringLiteral("text")).toString(),
            QStringLiteral("Cluster %1 label text mismatch: native=%2 golden=%3").arg(c.id, c.label.text, g.value(QStringLiteral("label")).toObject().value(QStringLiteral("text")).toString()));
  }

  qDebug().noquote() << "MermaidFlowchartSceneTest: scene matches golden";
  return 0;
}
