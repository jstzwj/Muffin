#pragma once

#include "mermaid/classdiagram/ClassDiagram.h"

#include <optional>
#include <QMap>
#include <QPointF>
#include <QRectF>
#include <QSizeF>

namespace muffin::mermaid::classdiagram {

// createText()'s default width, the max-width of a relationship's label: a
// text wider than it is wrapped, and its box keeps the width. No other label
// of a class diagram wraps: a namespace's title and a cardinality are made by
// createLabel() without a max-width, a class's texts are given their node's
// width, and a note's carries `white-space: nowrap`.
inline constexpr qreal kClassLabelWidth = 200.0;

struct ClassLayoutMemberInput {
  QString text;
  QString cssStyle;
};

struct ClassLayoutNodeInput {
  QString id;
  QString label;
  QString text;
  QString shape;
  QString parentId;
  QString cssClasses;
  QStringList cssStyles;
  QStringList styles;
  QStringList annotations;
  QVector<ClassLayoutMemberInput> members;
  QVector<ClassLayoutMemberInput> methods;
  std::optional<qreal> padding;
  bool isGroup = false;
  QString look = QStringLiteral("classic");
  QString link;    // click/href interaction directive target
  QString tooltip; // click tooltip directive
};

struct ClassLayoutEdgeInput {
  QString id;
  QString start;
  QString end;
  QString label;
  QString pattern;
  QString arrowTypeStart;
  QString arrowTypeEnd;
  QString startLabelRight;
  QString endLabelLeft;
  QStringList style;
  QStringList labelStyle = {QStringLiteral("display: inline-block")};
  QString classes = QStringLiteral("relation");
  QString look = QStringLiteral("classic");
};

struct ClassLayoutInput {
  QVector<ClassLayoutNodeInput> nodes;
  QVector<ClassLayoutEdgeInput> edges;
  QString direction = QStringLiteral("TB");
  qreal nodeSpacing = 50.0;
  qreal rankSpacing = 50.0;
  qreal namespacePadding = 16.0;
  QStringList markers = {QStringLiteral("aggregation"), QStringLiteral("extension"), QStringLiteral("composition"),
                         QStringLiteral("dependency"), QStringLiteral("lollipop")};
};

struct ClassLayoutOptions {
  // class.padding when the diagram configures it. Mermaid's three shapes
  // that read it each fall back to their own value when it is not set.
  std::optional<qreal> padding;
  qreal classBoxPadding() const { return padding.value_or(12.0); }
  qreal notePadding() const { return padding.value_or(6.0); }
  qreal namespacePadding() const { return padding.value_or(16.0); }
  qreal nodeSpacing = 50.0;
  qreal rankSpacing = 50.0;
  bool hierarchicalNamespaces = true;
  QString look = QStringLiteral("classic");
  bool htmlLabels = true;
  bool hideEmptyMembersBox = false;
};

struct ClassTextMeasurement {
  QRectF bounds;
  qsizetype lineCount = 1;
  bool svgText = false;
};

struct ClassNodeMeasurements {
  QVector<ClassTextMeasurement> annotations;
  QVector<ClassTextMeasurement> labels;
  QVector<ClassTextMeasurement> members;
  QVector<ClassTextMeasurement> methods;
  qreal textPadding = 0.0;
};

struct ClassLabelMeasureOptions {
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal fontPixelSize = 16.0;
  qreal lineHeight = 24.0;
  bool htmlLabels = true;
};

using ClassLayoutMeasurements = QMap<QString, ClassNodeMeasurements>;

QString classBoxLabelMarkup(const QString& label, const QString& text);

struct ClassCompartmentGeometry {
  QRectF localBounds;
  QPointF translation;
};

struct ClassBoxGeometry {
  QString id;
  QRectF bounds;
  QRectF outerRect;
  ClassCompartmentGeometry annotation;
  ClassCompartmentGeometry label;
  ClassCompartmentGeometry members;
  ClassCompartmentGeometry methods;
  QVector<QRectF> dividers;
};

struct ClassDagreMeasurements {
  QMap<QString, QSizeF> nodes;
  QMap<QString, QSizeF> edgeLabels;
  QMap<QString, QSizeF> clusterLabels;
};

struct ClassPlacementNode {
  QString id;
  qreal x = 0.0;
  qreal y = 0.0;
  qreal width = 0.0;
  qreal height = 0.0;
  int rank = 0;
};

struct ClassPlacementEdge {
  QString id;
  QVector<QPointF> points;
  QVector<QVector<QPointF>> segments;
  std::optional<QPointF> labelPosition;
  // The centres of the two 0.1 x 0.1 nodes a relationship from a class to
  // itself is laid out through. The SVG draws them, and its bounding box
  // covers them.
  QVector<QPointF> loopMarks;
};

struct ClassPlacementCluster {
  QString id;
  qreal x = 0.0;
  qreal y = 0.0;
  qreal width = 0.0;
  qreal height = 0.0;
  qreal logicalWidth = 0.0;
  qreal logicalHeight = 0.0;
};

struct ClassPlacementResult {
  QVector<ClassPlacementNode> nodes;
  QVector<ClassPlacementEdge> edges;
  QVector<ClassPlacementCluster> clusters;
  // The size each namespace's title was measured at when its box was sized.
  QMap<QString, QSizeF> clusterLabels;
  QPointF sourceOrigin;
};

ClassLayoutInput buildClassLayoutInput(const ClassDiagramData& data, ClassLayoutOptions options = {});

QVector<ClassBoxGeometry> layoutClassBoxes(const ClassLayoutInput& input, const ClassLayoutMeasurements& measurements,
                                           ClassLayoutOptions options = {});

ClassLayoutMeasurements measureClassLayoutLabels(const ClassLayoutInput& input, ClassLabelMeasureOptions options = {});

ClassDagreMeasurements measureClassDagreInput(const ClassLayoutInput& input, const QVector<ClassBoxGeometry>& boxes,
                                              ClassLabelMeasureOptions options = {}, bool handDrawn = false,
                                              quint32 handDrawnSeed = 0);

ClassPlacementResult layoutClassDiagramDagre(const ClassLayoutInput& input, const ClassDagreMeasurements& measurements,
                                             bool handDrawn = false, quint32 handDrawnSeed = 0);

} // namespace muffin::mermaid::classdiagram
