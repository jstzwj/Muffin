#pragma once

#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/theme/FlowTheme.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace muffin::mermaid::csscascade {

struct ElementStyle {
  QString fill;
  QString stroke;
  QString strokeWidth;
  QString color;
  QString fontFamily;
  QString fontSize;
  QString fontWeight;
  QString fontStyle;
  QString lineHeight = QStringLiteral("normal");
  QString letterSpacing = QStringLiteral("normal");
  QString wordSpacing = QStringLiteral("0px");
  QString textDecoration = QStringLiteral("none");
  QString textTransform = QStringLiteral("none");
  QString backgroundColor = QStringLiteral("transparent");
  QString textAnchor = QStringLiteral("start");
  QString dominantBaseline = QStringLiteral("auto");
  QString display = QStringLiteral("inline");
  QString visibility = QStringLiteral("visible");
  QString opacity = QStringLiteral("1");
  QString fillOpacity = QStringLiteral("1");
  QString strokeOpacity = QStringLiteral("1");
  QString mixBlendMode = QStringLiteral("normal");
  bool ancestorRenderable = true;
  bool ancestorHasBox = true;
  qreal effectiveOpacity = 1.0;
  qreal effectiveFillOpacity = 1.0;
  qreal effectiveStrokeOpacity = 1.0;

  bool displayed() const {
    return ancestorRenderable &&
           display.compare(QStringLiteral("none"), Qt::CaseInsensitive) != 0 &&
           visibility.compare(QStringLiteral("hidden"), Qt::CaseInsensitive) != 0 &&
           visibility.compare(QStringLiteral("collapse"), Qt::CaseInsensitive) != 0;
  }
  bool hasBox() const {
    return ancestorHasBox &&
           display.compare(QStringLiteral("none"), Qt::CaseInsensitive) != 0;
  }
};

struct FlowchartProjection {
  QHash<QString, ElementStyle> nodes;
  QHash<QString, ElementStyle> nodeLabels;
  QHash<QString, ElementStyle> edges;
  QHash<QString, ElementStyle> edgeLabels;
  QHash<QString, ElementStyle> clusterGroups;
  QHash<QString, ElementStyle> clusters;
  QHash<QString, ElementStyle> swimlaneTitles;
  QHash<QString, ElementStyle> swimlaneBodies;
  QHash<QString, ElementStyle> clusterLabels;
};

// A compact description of an element in Mermaid's generated SVG DOM. This
// is used by non-flowchart families so they share the same selector parser,
// specificity, inheritance and !important implementation as flowcharts.
struct ElementInput {
  QString key;
  QString parentKey;
  QString tag;
  QString id;
  QStringList classes;
  QHash<QString, QString> attributes;
  ElementStyle fallback;
  QString inlineStyle;
  // SVG presentation attributes participate below author stylesheets and
  // inline style. Kept separate from fallback so inherit/currentColor and
  // normal themeCSS declarations follow the browser cascade.
  QString presentationStyle;
};

QHash<QString, ElementStyle> resolveElements(
    const QString& themeCss, const QVector<ElementInput>& elements,
    const QString& builtInCss = QString());

// Builds the lightweight SVG DOM that Mermaid's flowchart renderer creates,
// applies built-in rules followed by source themeCSS, then applies the actual
// inline !important classDef/style declarations. The result is shared by
// measurement and scene construction so CSS that changes text metrics also
// changes Dagre input exactly once.
FlowchartProjection resolveFlowchart(
    const flowchart::FlowchartData& data,
    const flowtheme::FlowThemeVariables& theme,
    const QString& themeCss,
    bool swimlane = false,
    const QString& look = QStringLiteral("classic"),
    bool htmlLabels = true);

}  // namespace muffin::mermaid::csscascade
