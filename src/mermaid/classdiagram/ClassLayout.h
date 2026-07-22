#pragma once

#include "mermaid/classdiagram/ClassDiagram.h"

#include <optional>
#include <QMap>
#include <QPointF>
#include <QRectF>
#include <QSizeF>

namespace muffin::mermaid::classdiagram {

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
  QStringList markers = {QStringLiteral("aggregation"), QStringLiteral("extension"),
                         QStringLiteral("composition"), QStringLiteral("dependency"),
                         QStringLiteral("lollipop")};
};

struct ClassLayoutOptions {
  qreal padding = 12.0;
  qreal nodeSpacing = 50.0;
  qreal rankSpacing = 50.0;
  bool hierarchicalNamespaces = true;
  QString look = QStringLiteral("classic");
  bool htmlLabels = true;
  bool hideEmptyMembersBox = false;
};

struct ClassNodeMeasurements {
  QVector<QSizeF> annotations;
  QVector<QSizeF> labels;
  QVector<QSizeF> members;
  QVector<QSizeF> methods;
};

struct ClassLabelMeasureOptions {
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal fontPixelSize = 16.0;
  qreal lineHeight = 24.0;
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
  std::optional<QPointF> labelPosition;
};

struct ClassPlacementCluster {
  QString id;
  qreal x = 0.0;
  qreal y = 0.0;
  qreal width = 0.0;
  qreal height = 0.0;
};

struct ClassPlacementResult {
  QVector<ClassPlacementNode> nodes;
  QVector<ClassPlacementEdge> edges;
  QVector<ClassPlacementCluster> clusters;
};

ClassLayoutInput buildClassLayoutInput(const ClassDiagramData& data,
                                       ClassLayoutOptions options = {});

QVector<ClassBoxGeometry> layoutClassBoxes(
    const ClassLayoutInput& input, const ClassLayoutMeasurements& measurements,
    ClassLayoutOptions options = {});

ClassLayoutMeasurements measureClassLayoutLabels(
    const ClassLayoutInput& input, ClassLabelMeasureOptions options = {});

ClassDagreMeasurements measureClassDagreInput(
    const ClassLayoutInput& input, const QVector<ClassBoxGeometry>& boxes,
    ClassLabelMeasureOptions options = {});

ClassPlacementResult layoutClassDiagramDagre(
    const ClassLayoutInput& input, const ClassDagreMeasurements& measurements);

}  // namespace muffin::mermaid::classdiagram
