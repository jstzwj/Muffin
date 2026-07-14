#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/FlowStyleResolve.h"
#include "mermaid/theme/MermaidColor.h"

#include <QDebug>
#include <QColor>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <cmath>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) { qCritical().noquote() << message; std::exit(1); }
void require(bool condition, const QString& message) { if (!condition) fail(message); }

// Parse a CSS color string (the golden's computed "rgb(r, g, b)" or a hex/hsl)
// to a QColor for value comparison. Qt's QColor() rejects "rgb(r, g, b)" with
// spaces, so parse the channels explicitly.
QColor toColor(const QString& s) {
  QColor c(s.trimmed());
  if (c.isValid()) return c;
  static const QRegularExpression rgbaRe(
      QStringLiteral("rgba?\\(\\s*([\\d.]+)\\s*,\\s*([\\d.]+)\\s*,\\s*([\\d.]+)(?:\\s*,\\s*([\\d.]+))?\\)"),
      QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch m = rgbaRe.match(s.trimmed());
  if (m.hasMatch()) {
    const int r = qRound(m.captured(1).toDouble());
    const int g = qRound(m.captured(2).toDouble());
    const int b = qRound(m.captured(3).toDouble());
    const double a = m.captured(4).isEmpty() ? 1.0 : m.captured(4).toDouble();
    QColor c2; c2.setRgb(r, g, b, qRound(a * 255)); return c2;
  }
  return QColor();
}

bool colorsEqual(const QString& native, const QString& golden) {
  const QColor a = color::toQColor(native);
  const QColor b = toColor(golden);
  if (!a.isValid() || !b.isValid()) return native == golden;
  return a.rgb() == b.rgb();
}

// Parse "3px" / "3" -> 3.0; "" -> -1.
double pxToNumber(const QString& s) {
  static const QRegularExpression re(QStringLiteral("(\\d+(?:\\.\\d+)?)"));
  const QRegularExpressionMatch m = re.match(s);
  return m.hasMatch() ? m.captured(1).toDouble() : -1.0;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected style-cascade fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Could not open style-cascade fixture"));
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("upstream")).toObject().value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("Style-cascade fixture version drifted"));

  const flowchart::Flowchart chart = flowchart::Flowchart::parse(root.value(QStringLiteral("source")).toString());
  const flowtheme::FlowThemeVariables theme = flowtheme::resolveFlowTheme(flowtheme::FlowThemeId::Default);

  // Nodes: verify the cascade output (nodeStyles/labelStyles exact) + resolved
  // fill/stroke/color (QColor equality with the golden's computed rgb).
  for (const QJsonValue& v : root.value(QStringLiteral("nodes")).toArray()) {
    const QJsonObject n = v.toObject();
    const QString id = n.value(QStringLiteral("id")).toString();
    const flowchart::FlowVertex* vertex = nullptr;
    for (const flowchart::FlowVertex& vx : chart.data().vertices)
      if (vx.id == id) { vertex = &vx; break; }
    require(vertex != nullptr, QStringLiteral("Node %1 not in parsed chart").arg(id));
    const flowstyle::ResolvedNodeStyle r = flowstyle::resolveNodeStyle(*vertex, chart.data().classes, theme);
    require(r.nodeStyles == n.value(QStringLiteral("containerStyleAttr")).toString(),
            QStringLiteral("Node %1 nodeStyles mismatch: native=%2 golden=%3")
                .arg(id, r.nodeStyles, n.value(QStringLiteral("containerStyleAttr")).toString()));
    require(r.labelStyles == n.value(QStringLiteral("labelStyleAttr")).toString(),
            QStringLiteral("Node %1 labelStyles mismatch: native=%2 golden=%3")
                .arg(id, r.labelStyles, n.value(QStringLiteral("labelStyleAttr")).toString()));
    require(colorsEqual(r.fill, n.value(QStringLiteral("fill")).toString()),
            QStringLiteral("Node %1 fill mismatch: native=%2 golden=%3")
                .arg(id, r.fill, n.value(QStringLiteral("fill")).toString()));
    require(colorsEqual(r.stroke, n.value(QStringLiteral("stroke")).toString()),
            QStringLiteral("Node %1 stroke mismatch: native=%2 golden=%3")
                .arg(id, r.stroke, n.value(QStringLiteral("stroke")).toString()));
    require(colorsEqual(r.color, n.value(QStringLiteral("color")).toString()),
            QStringLiteral("Node %1 color mismatch: native=%2 golden=%3")
                .arg(id, r.color, n.value(QStringLiteral("color")).toString()));
    require(std::abs(pxToNumber(r.strokeWidth) - pxToNumber(n.value(QStringLiteral("strokeWidth")).toString())) <= 0.002,
            QStringLiteral("Node %1 strokeWidth mismatch: native=%2 golden=%3")
                .arg(id, r.strokeWidth, n.value(QStringLiteral("strokeWidth")).toString()));
  }

  // Edges: verify linkStyle cascade (stroke QColor + strokeWidth).
  const QJsonArray goldenEdges = root.value(QStringLiteral("edges")).toArray();
  require(goldenEdges.size() == chart.data().edges.size(),
          QStringLiteral("Edge count mismatch: native=%1 golden=%2")
              .arg(chart.data().edges.size()).arg(goldenEdges.size()));
  for (qsizetype i = 0; i < chart.data().edges.size(); ++i) {
    const QJsonObject e = goldenEdges.at(i).toObject();
    const flowstyle::ResolvedEdgeStyle r = flowstyle::resolveEdgeStyle(chart.data().edges.at(i), theme);
    require(colorsEqual(r.stroke, e.value(QStringLiteral("stroke")).toString()),
            QStringLiteral("Edge L%1 stroke mismatch: native=%2 golden=%3")
                .arg(i).arg(r.stroke, e.value(QStringLiteral("stroke")).toString()));
    require(std::abs(pxToNumber(r.strokeWidth) - pxToNumber(e.value(QStringLiteral("strokeWidth")).toString())) <= 0.002,
            QStringLiteral("Edge L%1 strokeWidth mismatch: native=%2 golden=%3")
                .arg(i).arg(r.strokeWidth).arg(e.value(QStringLiteral("strokeWidth")).toString()));
  }

  qDebug().noquote() << "MermaidFlowStyleResolveTest: cascade matches golden";
  return 0;
}
