#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/scene/SvgStroke.h"
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

// Raw CSS dash tokens (no width division): "4 2" and "4px, 2px" both give
// {4,2}; "none"/empty gives {}. Chrome reports the common sheet's solid-edge
// `stroke-dasharray: 0` as "0px" while native carries no dasharray at all, so
// the equality below treats all-zero and empty as the same solid line.
QVector<qreal> cssDashTokens(const QString& s) {
  const QString value = s.trimmed();
  if (value.isEmpty() || value.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0)
    return {};
  QVector<qreal> tokens;
  for (QString part : value.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts)) {
    if (part.endsWith(QLatin1String("px"), Qt::CaseInsensitive)) part.chop(2);
    bool ok = false;
    const qreal number = part.trimmed().toDouble(&ok);
    if (!ok) return {};
    tokens.append(number);
  }
  return tokens;
}

bool dashArraysEqual(const QString& native, const QString& golden) {
  const QVector<qreal> a = cssDashTokens(native);
  const QVector<qreal> b = cssDashTokens(golden);
  const auto allZero = [](const QVector<qreal>& v) {
    for (qreal value : v)
      if (value != 0.0) return false;
    return true;
  };
  const bool aSolid = a.isEmpty() || allZero(a);
  const bool bSolid = b.isEmpty() || allZero(b);
  if (aSolid || bSolid) return aSolid == bSolid;
  if (a.size() != b.size()) return false;
  for (qsizetype i = 0; i < a.size(); ++i)
    if (std::abs(a.at(i) - b.at(i)) > 0.002) return false;
  return true;
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
    require(dashArraysEqual(r.strokeDasharray, e.value(QStringLiteral("strokeDasharray")).toString()),
            QStringLiteral("Edge L%1 strokeDasharray mismatch: native=%2 golden=%3")
                .arg(i).arg(r.strokeDasharray, e.value(QStringLiteral("strokeDasharray")).toString()));
  }

  // classDef `default` auto-applies to every vertex (mermaid.js:48113 prepends
  // ["default","node"] in getCompiledStyles), so a class-less node still picks
  // up a user-authored `classDef default fill:...`.
  {
    flowchart::FlowClass defaultClass;
    defaultClass.id = QStringLiteral("default");
    defaultClass.styles = QStringList{QStringLiteral("fill:#112233")};
    QVector<flowchart::FlowClass> classes = chart.data().classes;
    classes.append(defaultClass);
    flowchart::FlowVertex vertex;
    vertex.id = QStringLiteral("__default_probe__");
    const flowstyle::ResolvedNodeStyle r = flowstyle::resolveNodeStyle(vertex, classes, theme);
    require(colorsEqual(r.fill, QStringLiteral("#112233")),
            QStringLiteral("classDef default did not propagate to class-less node: %1").arg(r.fill));
  }

  // compiledClassStyles: the edge/subgraph classDef resolver (setClass analogue).
  {
    flowchart::FlowClass thick;
    thick.id = QStringLiteral("thick");
    thick.styles = QStringList{QStringLiteral("stroke:#ff0000"), QStringLiteral("stroke-width:4px")};
    const QStringList merged = flowstyle::compiledClassStyles(
        QStringList{QStringLiteral("thick")}, QVector<flowchart::FlowClass>{thick});
    require(merged.contains(QStringLiteral("stroke:#ff0000")),
            QStringLiteral("compiledClassStyles lost stroke: %1").arg(merged.join(QLatin1Char(','))));
    require(merged.contains(QStringLiteral("stroke-width:4px")),
            QStringLiteral("compiledClassStyles lost stroke-width: %1").arg(merged.join(QLatin1Char(','))));
  }

  // SvgStroke painter-level dash contract (the cascade above only carries the
  // raw CSS token string; the painters consume this normalizer):
  //  - CSS lengths divide by the pen width, odd lists duplicate (SVG "3" = 3,3)
  //  - "none"/unparseable/negative/all-zero patterns and width <= 0 -> solid
  //  - the ER non-identifying probe value (er-geometry.json) round-trips.
  {
    using scene::parseAndNormalizeSvgDashPattern;
    require(parseAndNormalizeSvgDashPattern(QStringLiteral("2"), 1.0) == QVector<qreal>{2.0, 2.0},
            QStringLiteral("dash '2'@1px must normalize to {2,2}"));
    require(parseAndNormalizeSvgDashPattern(QStringLiteral("6px"), 3.0) == QVector<qreal>{2.0, 2.0},
            QStringLiteral("dash '6px'@3px must normalize to {2,2}"));
    require(parseAndNormalizeSvgDashPattern(QStringLiteral("5"), 2.0) == QVector<qreal>{2.5, 2.5},
            QStringLiteral("dash '5'@2px must normalize to {2.5,2.5}"));
    require(parseAndNormalizeSvgDashPattern(QStringLiteral("8,8"), 1.0) == QVector<qreal>{8.0, 8.0},
            QStringLiteral("dash '8,8'@1px must normalize to {8,8}"));
    require(parseAndNormalizeSvgDashPattern(QStringLiteral("none"), 1.0).isEmpty(),
            QStringLiteral("dash 'none' must be solid"));
    require(parseAndNormalizeSvgDashPattern(QStringLiteral("garbage"), 1.0).isEmpty(),
            QStringLiteral("unparseable dash must be solid"));
    require(parseAndNormalizeSvgDashPattern(QStringLiteral("4 -2"), 1.0).isEmpty(),
            QStringLiteral("negative dash entry must be solid"));
    require(parseAndNormalizeSvgDashPattern(QStringLiteral("0 0"), 1.0).isEmpty(),
            QStringLiteral("all-zero dash must be solid"));
    require(parseAndNormalizeSvgDashPattern(QStringLiteral("4 2"), 0.0).isEmpty(),
            QStringLiteral("zero pen width must be solid"));
  }

  qDebug().noquote() << "MermaidFlowStyleResolveTest: cascade matches golden";
  return 0;
}
