#include "mermaid/scene/FlowMarkers.h"

namespace muffin::mermaid::flowscene {

// Edge type -> marker type (chunk-4F4KDU6L.mjs arrowTypesMap + addEdgeMarker).
//   arrow_point      -> pointEnd
//   arrow_cross      -> crossEnd
//   arrow_circle     -> circleEnd
//   arrow_open       -> (none)  [--- and invisible ~~~]
//   double_arrow_*   -> End + Start of the same shape
QString markerEndType(const QString& edgeType) {
  if (edgeType == QLatin1String("arrow_point") || edgeType == QLatin1String("double_arrow_point"))
    return QStringLiteral("pointEnd");
  if (edgeType == QLatin1String("arrow_cross") || edgeType == QLatin1String("double_arrow_cross"))
    return QStringLiteral("crossEnd");
  if (edgeType == QLatin1String("arrow_circle") || edgeType == QLatin1String("double_arrow_circle"))
    return QStringLiteral("circleEnd");
  return QString();  // arrow_open / unknown -> no marker
}

QString markerStartType(const QString& edgeType) {
  if (edgeType == QLatin1String("double_arrow_point")) return QStringLiteral("pointStart");
  if (edgeType == QLatin1String("double_arrow_cross")) return QStringLiteral("crossStart");
  if (edgeType == QLatin1String("double_arrow_circle")) return QStringLiteral("circleStart");
  return QString();  // single-direction edges have no start marker
}

// Marker <def> geometry (chunk-4F4KDU6L.mjs L887-948, captured via the rendered
// SVG). The classic (non-margin) variants; neo's margin variants are an F3 look
// concern (look=neo, deferred).
MarkerGeometry markerGeometry(const QString& type) {
  MarkerGeometry g;
  if (type == QLatin1String("pointEnd")) {
    g.viewBox = QStringLiteral("0 0 10 10"); g.refX = 9; g.refY = 5;
    g.markerWidth = 8; g.markerHeight = 8; g.tag = QStringLiteral("path");
    g.pathData = QStringLiteral("M 0 0 L 10 5 L 0 10 z");
  } else if (type == QLatin1String("pointStart")) {
    g.viewBox = QStringLiteral("0 0 10 10"); g.refX = 1; g.refY = 5;
    g.markerWidth = 8; g.markerHeight = 8; g.tag = QStringLiteral("path");
    g.pathData = QStringLiteral("M 0 5 L 10 10 L 10 0 z");
  } else if (type == QLatin1String("circleEnd") || type == QLatin1String("circleStart")) {
    g.viewBox = QStringLiteral("0 0 10 10");
    g.refX = type == QLatin1String("circleEnd") ? 11 : -1; g.refY = 5;
    g.markerWidth = 11; g.markerHeight = 11; g.tag = QStringLiteral("circle");
    g.cx = 5; g.cy = 5; g.r = 5;
  } else if (type == QLatin1String("crossEnd") || type == QLatin1String("crossStart")) {
    g.viewBox = QStringLiteral("0 0 15 15");
    g.refX = type == QLatin1String("crossEnd") ? 12 : -3; g.refY = 7.5;
    g.markerWidth = 12; g.markerHeight = 12; g.tag = QStringLiteral("path");
    g.pathData = QStringLiteral("M 1,1 L 14,14 M 1,14 L 14,1");
  }
  return g;
}

}  // namespace muffin::mermaid::flowscene
